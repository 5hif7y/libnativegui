// Automated checks for the LibGW extensions (feat/portmaster-ui), Win32
// and X11. It synthesizes window messages (Win32) or puts X events back on
// the queue (X11) to test the event paths. Never shows the window. The
// user's clipboard text is saved and restored. --tray also adds a tray icon
// for a moment. Font: $GW_TEST_FONT or the platform default (test_font).
//
// Build (Developer prompt, from the repo root, after building the library):
//   cl /nologo /Iinclude examples\gw_ext_test.c build\nativegui.lib user32.lib gdi32.lib comdlg32.lib shell32.lib /Fe:build\gw_ext_test.exe
// Linux (X11):
//   cc -Iinclude -Isrc examples/gw_ext_test.c build/libnativegui.a -lX11 -lm -lpthread -o build/gw_ext_test
#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <pthread.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "gw.h"
#include "gw_internal.h"

static int fails = 0, checks = 0;
/* Test font: $GW_TEST_FONT, else a platform default. */
static const char* test_font(void) {
    const char* f = getenv("GW_TEST_FONT");
    if (f && *f) return f;
#ifdef _WIN32
    return "C:\\Windows\\Fonts\\segoeui.ttf";
#else
    return "/usr/share/fonts/TTF/DejaVuSans.ttf";
#endif
}
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t px(GW_Window* w, int x, int y) {
    int bw, bh;
    uint32_t* bb = GW_GetBackbuffer(w, &bw, &bh);
    return bb[y * bw + x] & 0x00FFFFFF;
}

static int wait_for(GW_Event* ev, GW_EventType type, int timeout_ms) {
    uint64_t end = GW_GetTicks() + timeout_ms;
    while (GW_GetTicks() < end) {
        int left = (int)(end - GW_GetTicks());
        if (GW_WaitEventTimeout(ev, left > 0 ? left : 0) && ev->type == type) return 1;
    }
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI poster(LPVOID p) {
    Sleep(30);
    GW_PostUserEvent((GW_Window*)p, 42, (void*)(intptr_t)0x1234);
    return 0;
}
#else
static void* poster(void* p) {
    usleep(30 * 1000);
    GW_PostUserEvent((GW_Window*)p, 42, (void*)(intptr_t)0x1234);
    return NULL;
}

/* Puts a synthetic button event at the head of the window's X queue. */
static void put_button(GW_Window* w, int type, unsigned button, int x, int y, unsigned state, Time t) {
    XEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.xbutton.type = type;
    xe.xbutton.display = (Display*)w->dpy;
    xe.xbutton.window = (Window)w->win;
    xe.xbutton.button = button;
    xe.xbutton.x = x;
    xe.xbutton.y = y;
    xe.xbutton.state = state;
    xe.xbutton.time = t;
    XPutBackEvent((Display*)w->dpy, &xe);
}

/* Same for a key press of the key that types keysym (0 if not in the keymap). */
static int put_key(GW_Window* w, KeySym keysym, unsigned state) {
    Display* d = (Display*)w->dpy;
    KeyCode kc = XKeysymToKeycode(d, keysym);
    if (!kc) return 0;
    XEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.xkey.type = KeyPress;
    xe.xkey.display = d;
    xe.xkey.window = (Window)w->win;
    xe.xkey.keycode = kc;
    xe.xkey.state = state;
    XPutBackEvent(d, &xe);
    return 1;
}
#endif

int main(int argc, char** argv) {
    int with_tray = argc > 1 && strcmp(argv[1], "--tray") == 0;
    GW_Window* w = GW_CreateWindow("gw_ext_test", 200, 120);
    if (!w) { printf("FAIL: no window\n"); return 1; }
#ifdef _WIN32
    HWND hwnd = (HWND)w->hwnd;
#endif
    int bw, bh;
    GW_GetBackbuffer(w, &bw, &bh);
    CHECK(bw == 200 && bh == 120, "backbuffer %dx%d", bw, bh);

    /* clip rect */
    GW_Clear(w, 0x000000);
    GW_SetClipRect(w, 10, 10, 20, 20);
    int cx, cy, cw, ch;
    GW_GetClipRect(w, &cx, &cy, &cw, &ch);
    CHECK(cx == 10 && cy == 10 && cw == 20 && ch == 20, "clip %d,%d %dx%d", cx, cy, cw, ch);
    GW_FillRect(w, 0, 0, 100, 100, 0xFF0000);
    CHECK(px(w, 5, 5) == 0, "outside clip drawn: %06x", px(w, 5, 5));
    CHECK(px(w, 15, 15) == 0xFF0000, "inside clip: %06x", px(w, 15, 15));
    CHECK(px(w, 30, 30) == 0, "clip is exclusive at x+w: %06x", px(w, 30, 30));
    GW_DrawLine(w, 0, 20, 199, 20, 0x00FF00);
    CHECK(px(w, 5, 20) == 0 && px(w, 12, 20) == 0x00FF00, "line clipped: %06x %06x", px(w, 5, 20), px(w, 12, 20));
    GW_SetClipRect(w, 190, 110, 50, 50); // partly outside the window
    GW_GetClipRect(w, &cx, &cy, &cw, &ch);
    CHECK(cx == 190 && cy == 110 && cw == 10 && ch == 10, "clip intersected: %d,%d %dx%d", cx, cy, cw, ch);
    GW_ResetClipRect(w);
    GW_GetClipRect(w, &cx, &cy, &cw, &ch);
    CHECK(cx == 0 && cy == 0 && cw == 200 && ch == 120, "reset clip");

    /* blending and shapes */
    GW_Clear(w, 0x000000);
    GW_FillRectBlend(w, 0, 0, 10, 10, 0x80FFFFFF);
    uint32_t g = px(w, 5, 5);
    CHECK((g & 0xFF) >= 0x7F && (g & 0xFF) <= 0x81 && (g >> 16) == (g & 0xFF), "50%% white over black: %06x", g);
    GW_FillRectBlend(w, 0, 0, 10, 10, 0x00FFFFFF);
    CHECK(px(w, 5, 5) == g, "alpha 0 is a no-op");
    GW_Clear(w, 0x000000);
    GW_FillRoundRect(w, 20, 20, 60, 40, 12, 0xFF3366CC);
    CHECK(px(w, 50, 40) == 0x3366CC, "round rect center: %06x", px(w, 50, 40));
    CHECK(px(w, 20, 20) == 0, "round rect corner is empty: %06x", px(w, 20, 20));
    CHECK(px(w, 20, 40) == 0x3366CC, "round rect straight edge is exact: %06x", px(w, 20, 40));
    uint32_t edge = px(w, 23, 23); // center (23.5, 23.5) is 12.02 px from the arc center (32, 32): on the edge
    CHECK(edge != 0 && edge != 0x3366CC, "corner is anti-aliased: %06x", edge);
    GW_Clear(w, 0x000000);
    GW_DrawRoundRect(w, 20, 20, 60, 40, 8, 2, 0xFFFFFFFF);
    CHECK(px(w, 50, 40) == 0, "stroke keeps the middle empty: %06x", px(w, 50, 40));
    CHECK(px(w, 50, 20) == 0xFFFFFF, "stroke top edge: %06x", px(w, 50, 20));
    GW_Clear(w, 0x000000);
    GW_FillCircle(w, 50, 50, 10, 0xFF00FF00);
    CHECK(px(w, 50, 50) == 0x00FF00 && px(w, 50, 38) == 0, "circle: %06x %06x", px(w, 50, 50), px(w, 50, 38));
    GW_Clear(w, 0x000000);
    GW_DrawLineAA(w, 10.5f, 10.5f, 90.5f, 10.5f, 2.0f, 0xFFFFFFFF);
    CHECK(px(w, 50, 10) == 0xFFFFFF && px(w, 50, 14) == 0, "AA line: %06x %06x", px(w, 50, 10), px(w, 50, 14));

    /* images */
    const char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect x='0' y='0' width='10' height='10' fill='#ff0000'/></svg>";
    GW_Image* si = GW_LoadSVGFromMemory(svg, (int)strlen(svg), 20, 20);
    CHECK(si && si->w == 20 && si->h == 20, "svg from memory");
    if (si) {
        CHECK((si->pixels[10 * 20 + 10] & 0xFFFFFFFF) == 0xFFFF0000, "svg pixel: %08x", si->pixels[10 * 20 + 10]);
        GW_Clear(w, 0x000000);
        GW_DrawImageTinted(w, si, 0, 0, 20, 20, 0xFF00FF00);
        CHECK(px(w, 10, 10) == 0x00FF00, "tinted: %06x", px(w, 10, 10));
        GW_FreeImage(si);
    }
    uint32_t raw[4] = { 0xFF112233, 0xFF445566, 0x00000000, 0x80FFFFFF };
    GW_Image* ci = GW_CreateImage(2, 2, raw);
    CHECK(ci && ci->pixels[1] == 0xFF445566, "create image");
    GW_SetWindowIcon(w, ci);
    GW_FreeImage(ci);
    CHECK(GW_LoadImageFromMemory((const unsigned char*)"not an image", 12) == NULL, "bad image -> NULL, silently");

    /* fonts and text */
    FILE* f = fopen(test_font(), "rb");
    GW_Font* font = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char* data = (unsigned char*)malloc(n);
        fread(data, 1, n, f);
        fclose(f);
        font = GW_LoadFontFromMemory(data, (int)n, 16.0f);
        free(data); // the font keeps its own copy
    }
    CHECK(font != NULL, "font from memory (%s)", test_font());
    if (font) {
        int a, d, lh, w1, h1, w2, h2;
        GW_GetFontMetrics(font, &a, &d, &lh);
        CHECK(a > 0 && d < 0 && lh >= a - d, "metrics %d %d %d", a, d, lh);
        GW_MeasureTextUTF8(font, "Hola \xc3\xb1" "and\xc3\xba", &w1, &h1);
        GW_MeasureText(font, L"Hola \x00f1" L"and\x00fa", &w2, &h2);
        CHECK(w1 == w2 && w1 > 0 && h1 == h2, "utf8 vs wide: %d %d", w1, w2);
        int we, he;
        GW_MeasureTextUTF8(font, "\xf0\x9f\x98\x80", &we, &he);   // U+1F600 as UTF-8
        int we2, he2;
#ifdef _WIN32
        GW_MeasureText(font, L"\xD83D\xDE00", &we2, &he2);          // same as a surrogate pair
        CHECK(we == we2, "surrogate pair = one codepoint: %d %d", we, we2);
#else
        GW_MeasureText(font, L"\U0001F600", &we2, &he2);            // wchar_t is UTF-32 here
        CHECK(we == we2, "utf-8 emoji = one wide codepoint: %d %d", we, we2);
#endif
        {
            wchar_t wb[16];
            char ub[32];
            int nw = GW_UTF8ToWide("\xc3\xb1" "a\xf0\x9f\x98\x80", wb, 16);
            int nu = GW_WideToUTF8(wb, ub, 32);
            CHECK(strcmp(ub, "\xc3\xb1" "a\xf0\x9f\x98\x80") == 0 && nu == 8, "utf-8 -> wide -> utf-8 round trip (%d, %d)", nw, nu);
            GW_UTF8ToWide("abcdef", wb, 4);
            CHECK(wcscmp(wb, L"abc") == 0, "utf8->wide truncates and terminates");
        }
        GW_MeasureTextUTF8(font, "\xff\xfe", &we, &he);             // invalid UTF-8: no crash
        GW_Clear(w, 0x000000);
        GW_SetClipRect(w, 0, 0, 30, 120);
        GW_DrawTextUTF8(w, font, 2, 2, "WWWWWWWWWWWW", 0xFFFFFF);
        int lit_in = 0, lit_out = 0;
        for (int y = 0; y < 30; y++) for (int x = 0; x < 200; x++) {
            if (px(w, x, y)) { if (x < 30) lit_in++; else lit_out++; }
        }
        CHECK(lit_in > 0 && lit_out == 0, "text respects clip: in %d out %d", lit_in, lit_out);
        GW_ResetClipRect(w);
        GW_FreeFont(font);
    }

    /* events: user event from another thread, timeout, timer */
    GW_Event ev;
    uint64_t t0 = GW_GetTicks();
    CHECK(GW_WaitEventTimeout(&ev, 80) == 0 && ev.type == GW_EVENT_NONE, "idle wait -> NONE (%d)", ev.type);
    uint64_t dt = GW_GetTicks() - t0;
    CHECK(dt >= 70 && dt < 1000, "timeout took %llu ms", (unsigned long long)dt);
#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, poster, w, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, poster, w);
#endif
    CHECK(wait_for(&ev, GW_EVENT_USER, 2000) && ev.user.code == 42 && ev.user.data == (void*)(intptr_t)0x1234 && ev.window == w,
          "user event from a thread");
#ifdef _WIN32
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
#else
    pthread_join(th, NULL);
#endif
    CHECK(GW_SetTimer(w, 7, 40), "set timer");
    int ticks = 0;
    for (int i = 0; i < 3; i++) if (wait_for(&ev, GW_EVENT_TIMER, 1000) && ev.timer.id == 7) ticks++;
    CHECK(ticks == 3, "3 timer ticks, got %d", ticks);
    GW_KillTimer(w, 7);

#ifdef _WIN32
    /* wheel: legacy keys by default, real events when asked */
    while (GW_PollEvent(&ev)) {}
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), MAKELPARAM(0, 0));
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_KEY_DOWN && ev.key.keycode == '+', "legacy wheel -> '+' (%d)", ev.type);
    GW_SetWheelMode(w, GW_WHEEL_EVENTS);
    POINT sp = { 15, 25 };
    ClientToScreen(hwnd, &sp);
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(MK_SHIFT, (WORD)(short)-240), MAKELPARAM(sp.x, sp.y));
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_WHEEL && ev.wheel.delta == -240 && ev.wheel.x == 15 && ev.wheel.y == 25 && !ev.wheel.horizontal,
          "wheel event: type %d delta %d at %d,%d", ev.type, ev.wheel.delta, ev.wheel.x, ev.wheel.y);

    /* double click and button details */
    SendMessageW(hwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(3, 4));
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_BUTTON_DOWN && ev.mouse_button.clicks == 2 && ev.mouse_button.button == 1,
          "double click -> DOWN clicks=2");
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(3, 4));
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_BUTTON_UP && ev.mouse_button.clicks == 1, "button up");

    /* clipboard round trip (restoring the user's text) */
    char* saved = GW_GetClipboardText(w);
    CHECK(GW_SetClipboardText(w, "h\xc3\xa9llo \xe2\x9c\x93"), "set clipboard");
    char* got = GW_GetClipboardText(w);
    CHECK(got && strcmp(got, "h\xc3\xa9llo \xe2\x9c\x93") == 0, "clipboard round trip: %s", got ? got : "(null)");
    GW_Free(got);
    if (saved) { GW_SetClipboardText(w, saved); GW_Free(saved); }

#else
    /* wheel: legacy buttons 4/5 by default, real events when asked */
    while (GW_PollEvent(&ev)) {}
    put_button(w, ButtonPress, 4, 0, 0, 0, 1000);
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_BUTTON_DOWN && ev.mouse_button.button == 4, "legacy wheel -> button 4 (%d)", ev.type);
    GW_SetWheelMode(w, GW_WHEEL_EVENTS);
    put_button(w, ButtonPress, 5, 15, 25, ShiftMask, 2000);
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_WHEEL && ev.wheel.delta == -120 && ev.wheel.x == 15 && ev.wheel.y == 25 && !ev.wheel.horizontal && (ev.wheel.mod & GW_MOD_SHIFT),
          "wheel event: type %d delta %d at %d,%d", ev.type, ev.wheel.delta, ev.wheel.x, ev.wheel.y);
    put_button(w, ButtonRelease, 5, 15, 25, 0, 2001);
    CHECK(!GW_PollEvent(&ev), "wheel release is dropped (%d)", ev.type);
    put_button(w, ButtonPress, 6, 15, 25, 0, 2100);
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_WHEEL && ev.wheel.horizontal, "button 6 -> horizontal wheel");

    /* double click and button details */
    put_button(w, ButtonPress, 1, 3, 4, ControlMask, 5000);
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_BUTTON_DOWN && ev.mouse_button.clicks == 1 && (ev.mouse_button.mod & GW_MOD_CTRL),
          "first click: clicks=1 with ctrl");
    put_button(w, ButtonPress, 1, 4, 5, 0, 5200);
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_BUTTON_DOWN && ev.mouse_button.clicks == 2 && ev.mouse_button.button == 1,
          "second click within 400 ms -> DOWN clicks=2");
    put_button(w, ButtonRelease, 1, 4, 5, 0, 5250);
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_MOUSE_BUTTON_UP && ev.mouse_button.clicks == 1, "button up");
    put_button(w, ButtonPress, 1, 4, 5, 0, 7000);
    CHECK(GW_PollEvent(&ev) && ev.mouse_button.clicks == 1, "late click starts over");

    /* keys: text comes from the keysym (no locale needed); Ctrl+key is not text */
    while (GW_PollEvent(&ev)) {}
    if (put_key(w, XK_a, 0)) {
        int down = GW_PollEvent(&ev) && ev.type == GW_EVENT_KEY_DOWN && ev.key.keycode == 'a';
        CHECK(down && GW_PollEvent(&ev) && ev.type == GW_EVENT_TEXT_INPUT && ev.text.character == L'a', "key 'a' -> KEY_DOWN + text 'a'");
        put_key(w, XK_a, ControlMask);
        CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_KEY_DOWN && (ev.key.mod & GW_MOD_CTRL), "ctrl+a -> KEY_DOWN with ctrl");
        CHECK(!GW_PollEvent(&ev), "ctrl+a types no text (%d)", ev.type);
    } else {
        CHECK(0, "keymap has no 'a'");
    }
    put_key(w, XK_Return, 0);
    CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_KEY_DOWN && ev.key.keycode == GW_KEY_ENTER && !GW_PollEvent(&ev), "enter is a key, not text");

    /* clipboard: not implemented on X11 yet (documented stub) */
    CHECK(GW_SetClipboardText(w, "x") == 0 && GW_GetClipboardText(w) == NULL, "clipboard stub");

    /* mouse position (screen and client) */
    {
        int sx = -1, sy = -1, cx2 = -1, cy2 = -1;
        GW_GetMousePosition(w, &sx, &sy, &cx2, &cy2);
        CHECK(sx >= 0 && sy >= 0, "mouse position %d,%d (client %d,%d)", sx, sy, cx2, cy2);
        GW_GetMousePosition(NULL, &sx, &sy, NULL, NULL);
        CHECK(sx >= 0 && sy >= 0, "mouse position without a window %d,%d", sx, sy);
    }

#endif

    /* misc */
    GW_SetCursor(w, GW_CURSOR_HAND);
    float scale = GW_GetDpiScale(w);
    CHECK(scale >= 1.0f && scale <= 4.0f, "dpi scale %.2f", scale);
    GW_SetMinWindowSize(w, 300, 200);
#ifdef _WIN32
    SetWindowPos(hwnd, NULL, 0, 0, 50, 50, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GW_GetBackbuffer(w, &bw, &bh);
    CHECK(bw >= 300 && bh >= 200, "min size enforced on SetWindowPos: %dx%d", bw, bh);
#else
    {
        // X11: the window manager enforces it; check WM_NORMAL_HINTS.
        XSizeHints hints;
        long supplied = 0;
        Status ok = XGetWMNormalHints((Display*)w->dpy, (Window)w->win, &hints, &supplied);
        CHECK(ok && (hints.flags & PMinSize) && hints.min_width == 300 && hints.min_height == 200,
              "min size hint: %d %dx%d", (int)ok, hints.min_width, hints.min_height);
    }
#endif
    CHECK(!GW_IsWindowVisible(w), "never shown");

    if (with_tray) {
        GW_Image* ti = GW_CreateImage(2, 2, raw);
        GW_TrayIcon* t = GW_CreateTrayIcon(w, 5, ti, "gw_ext_test");
#ifdef _WIN32
        CHECK(t != NULL, "tray icon");
        GW_SetTrayTooltip(t, "gw_ext_test \xe2\x9c\x93");
        while (GW_PollEvent(&ev)) {} // drop the resize events of the min-size check
        SendMessageW(hwnd, GW_WM_TRAY, 5, WM_RBUTTONUP);
        CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_TRAY && ev.tray.id == 5 && ev.tray.action == GW_TRAY_RIGHT_CLICK, "tray event");
        Sleep(500);
#else
        CHECK(t == NULL, "tray icon: NULL on X11 (documented stub)");
#endif
        GW_DestroyTrayIcon(t);
        GW_FreeImage(ti);
    }

    GW_DestroyWindow(w);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
