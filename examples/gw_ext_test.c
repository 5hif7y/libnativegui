// Automated checks for the LibGW extensions (feat/portmaster-ui).
// Windows only for now: it synthesizes window messages to test the event
// paths. Never shows the window. The user's clipboard text is saved and
// restored. --tray also adds a tray icon for a moment.
//
// Build (Developer prompt, from the repo root, after building the library):
//   cl /nologo /Iinclude examples\gw_ext_test.c build\nativegui.lib user32.lib gdi32.lib comdlg32.lib shell32.lib /Fe:build\gw_ext_test.exe
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gw.h"
#include "gw_internal.h"

static int fails = 0, checks = 0;
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

static DWORD WINAPI poster(LPVOID p) {
    Sleep(30);
    GW_PostUserEvent((GW_Window*)p, 42, (void*)(intptr_t)0x1234);
    return 0;
}

int main(int argc, char** argv) {
    int with_tray = argc > 1 && strcmp(argv[1], "--tray") == 0;
    GW_Window* w = GW_CreateWindow("gw_ext_test", 200, 120);
    if (!w) { printf("FAIL: no window\n"); return 1; }
    HWND hwnd = (HWND)w->hwnd;
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
    FILE* f = fopen("C:\\Windows\\Fonts\\segoeui.ttf", "rb");
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
    CHECK(font != NULL, "font from memory");
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
        GW_MeasureText(font, L"\xD83D\xDE00", &we2, &he2);          // same as a surrogate pair
        CHECK(we == we2, "surrogate pair = one codepoint: %d %d", we, we2);
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
    HANDLE th = CreateThread(NULL, 0, poster, w, 0, NULL);
    CHECK(wait_for(&ev, GW_EVENT_USER, 2000) && ev.user.code == 42 && ev.user.data == (void*)(intptr_t)0x1234 && ev.window == w,
          "user event from a thread");
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    CHECK(GW_SetTimer(w, 7, 40), "set timer");
    int ticks = 0;
    for (int i = 0; i < 3; i++) if (wait_for(&ev, GW_EVENT_TIMER, 1000) && ev.timer.id == 7) ticks++;
    CHECK(ticks == 3, "3 timer ticks, got %d", ticks);
    GW_KillTimer(w, 7);

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

    /* misc */
    GW_SetCursor(w, GW_CURSOR_HAND);
    float scale = GW_GetDpiScale(w);
    CHECK(scale >= 1.0f && scale <= 4.0f, "dpi scale %.2f", scale);
    GW_SetMinWindowSize(w, 300, 200);
    SetWindowPos(hwnd, NULL, 0, 0, 50, 50, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GW_GetBackbuffer(w, &bw, &bh);
    CHECK(bw >= 300 && bh >= 200, "min size enforced on SetWindowPos: %dx%d", bw, bh);
    CHECK(!GW_IsWindowVisible(w), "never shown");

    if (with_tray) {
        GW_Image* ti = GW_CreateImage(2, 2, raw);
        GW_TrayIcon* t = GW_CreateTrayIcon(w, 5, ti, "gw_ext_test");
        CHECK(t != NULL, "tray icon");
        GW_SetTrayTooltip(t, "gw_ext_test \xe2\x9c\x93");
        while (GW_PollEvent(&ev)) {} // drop the resize events of the min-size check
        SendMessageW(hwnd, GW_WM_TRAY, 5, WM_RBUTTONUP);
        CHECK(GW_PollEvent(&ev) && ev.type == GW_EVENT_TRAY && ev.tray.id == 5 && ev.tray.action == GW_TRAY_RIGHT_CLICK, "tray event");
        Sleep(500);
        GW_DestroyTrayIcon(t);
        GW_FreeImage(ti);
    }

    GW_DestroyWindow(w);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
