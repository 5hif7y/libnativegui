// Automated checks for the X11 desktop parts: built-in dialogs and popup
// menu (gw_x11_dialogs.c), clipboard and tray icon (gw_x11_ext.c). A
// helper thread with its own X connection waits for each dialog window (by
// title) and sends it key presses with XSendEvent, like a user typing.
// Needs a display (WSLg, Xvfb...) and a font (GW_FONT or GW_TEST_FONT, or a
// system DejaVu / Noto / Liberation). X11 only.
//
//   cc -Iinclude -Isrc examples/gw_dialog_test.c build/libnativegui.a -lX11 -lm -lpthread -o build/gw_dialog_test
#ifdef _WIN32
#include <stdio.h>
int main(void) { printf("X11 only\n"); return 0; }
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "gw.h"
#include "gw_internal.h" // the X window id of a GW_Window

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the "user": finds a window by title and types into it -------------- */

static Window find_by_name(Display* d, Window w, const char* name) {
    char* wn = NULL;
    if (XFetchName(d, w, &wn) && wn) {
        int eq = strcmp(wn, name) == 0;
        XFree(wn);
        if (eq) return w;
    }
    Window root, parent, *kids = NULL;
    unsigned n = 0;
    if (!XQueryTree(d, w, &root, &parent, &kids, &n)) return 0;
    Window found = 0;
    for (unsigned i = 0; i < n && !found; i++) found = find_by_name(d, kids[i], name);
    if (kids) XFree(kids);
    return found;
}

static Window wait_window(Display* d, const char* name) {
    for (int i = 0; i < 100; i++) { // 5 s
        Window w = find_by_name(d, DefaultRootWindow(d), name);
        if (w) {
            XWindowAttributes a;
            if (XGetWindowAttributes(d, w, &a) && a.map_state == IsViewable) {
                usleep(250 * 1000); // let it draw its first frame
                return w;
            }
        }
        usleep(50 * 1000);
    }
    return 0;
}

// The release goes after the press; when the press closes the dialog (Enter,
// Escape) the window is gone by then and the server answers BadWindow to
// this helper connection. The X error handler is per process, so LibGW's
// handler prints it ("LibGW X11 error: BadWindow ... request 25"): expected.
static void send_key(Display* d, Window w, KeySym ks, unsigned state) {
    XEvent e;
    memset(&e, 0, sizeof(e));
    e.xkey.type = KeyPress;
    e.xkey.display = d;
    e.xkey.window = w;
    e.xkey.root = DefaultRootWindow(d);
    e.xkey.keycode = XKeysymToKeycode(d, ks);
    e.xkey.state = state;
    e.xkey.same_screen = True;
    XSendEvent(d, w, True, KeyPressMask, &e);
    e.xkey.type = KeyRelease;
    XSendEvent(d, w, True, KeyReleaseMask, &e);
    XFlush(d);
    usleep(30 * 1000);
}

static void type_text(Display* d, Window w, const char* s) {
    for (; *s; s++) {
        KeySym ks = (*s == '.') ? XK_period : (*s == '_') ? XK_underscore : (KeySym)(unsigned char)*s;
        send_key(d, w, ks, *s == '_' ? ShiftMask : 0);
    }
}

typedef struct { const char* title; const char* text; KeySym final_key;
                 const char* confirm_title; KeySym confirm_key; int ok; KeySym then_key;
                 KeySym keys[8]; int click_x, click_y; } Script;

static void send_click(Display* d, Window w, int x, int y) {
    XEvent e;
    memset(&e, 0, sizeof(e));
    e.xbutton.type = ButtonPress;
    e.xbutton.display = d;
    e.xbutton.window = w;
    e.xbutton.button = 1;
    e.xbutton.x = x;
    e.xbutton.y = y;
    e.xbutton.same_screen = True;
    XSendEvent(d, w, True, ButtonPressMask, &e);
    XFlush(d);
    usleep(60 * 1000);
    e.xbutton.type = ButtonRelease;
    XSendEvent(d, w, True, ButtonReleaseMask, &e);
    XFlush(d);
    usleep(30 * 1000);
}

static void* user(void* p) {
    Script* sc = (Script*)p;
    Display* d = XOpenDisplay(NULL);
    if (!d) return NULL;
    Window w = wait_window(d, sc->title);
    if (w) {
        if (sc->text) type_text(d, w, sc->text);
        for (int i = 0; i < 8 && sc->keys[i]; i++) { send_key(d, w, sc->keys[i], 0); usleep(90 * 1000); }
        if (sc->click_y) send_click(d, w, sc->click_x, sc->click_y);
        if (sc->final_key) send_key(d, w, sc->final_key, 0);
        if (sc->then_key) { usleep(300 * 1000); send_key(d, w, sc->then_key, 0); }
        if (sc->confirm_title) {
            Window c = wait_window(d, sc->confirm_title);
            if (c) send_key(d, c, sc->confirm_key, 0);
            sc->ok = c != 0;
        } else {
            sc->ok = 1;
        }
    }
    XCloseDisplay(d);
    return NULL;
}

static pthread_t start_user(Script* sc) {
    pthread_t t;
    pthread_create(&t, NULL, user, sc);
    return t;
}

/* ---- another X client: clipboard owner / reader, fake tray manager ------- */

static Atom atom(Display* d, const char* n) { return XInternAtom(d, n, False); }

// Waits for an event of type on w (own connection), up to ms.
static int next_typed(Display* d, Window w, int type, XEvent* e, int ms) {
    for (int i = 0; i < ms / 5; i++) {
        if (XCheckTypedWindowEvent(d, w, type, e)) return 1;
        usleep(5 * 1000);
    }
    return 0;
}

typedef struct { const char* text; size_t len; int incr; int served; int ready; Window want; } ClipOwner;

// Owns CLIPBOARD and answers UTF8_STRING requests for up to 4 s, until the
// one from co->want (the window under test) is served; INCR in 4 KiB chunks
// for that one if co->incr. Other requestors (e.g. WSLg's clipboard bridge,
// which reads every new selection) get a refusal.
static void* clip_owner(void* p) {
    ClipOwner* co = (ClipOwner*)p;
    Display* d = XOpenDisplay(NULL);
    if (!d) return NULL;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    Atom clip = atom(d, "CLIPBOARD"), utf8 = atom(d, "UTF8_STRING"), incr = atom(d, "INCR");
    XSetSelectionOwner(d, clip, w, CurrentTime);
    XSync(d, False);
    co->ready = 1;
    for (int round = 0; round < 800 && !co->served; round++) {
        XEvent e;
        if (!XCheckTypedWindowEvent(d, w, SelectionRequest, &e)) { usleep(5 * 1000); continue; }
        XSelectionRequestEvent rq = e.xselectionrequest;
        XSelectionEvent rep;
        memset(&rep, 0, sizeof(rep));
        rep.type = SelectionNotify; rep.requestor = rq.requestor; rep.selection = rq.selection;
        rep.target = rq.target; rep.time = rq.time; rep.property = None;
        if (rq.requestor != co->want || rq.target != utf8) {
            XSendEvent(d, rq.requestor, False, NoEventMask, (XEvent*)&rep);
            XFlush(d);
            continue;
        }
        if (!co->incr) {
            XChangeProperty(d, rq.requestor, rq.property, utf8, 8, PropModeReplace, (const unsigned char*)co->text, (int)co->len);
            rep.property = rq.property;
            XSendEvent(d, rq.requestor, False, NoEventMask, (XEvent*)&rep);
            XFlush(d);
            co->served = 1;
            continue;
        }
        // INCR: announce, then one chunk per deletion of the property.
        XSelectInput(d, rq.requestor, PropertyChangeMask);
        long total = (long)co->len;
        XChangeProperty(d, rq.requestor, rq.property, incr, 32, PropModeReplace, (unsigned char*)&total, 1);
        rep.property = rq.property;
        XSendEvent(d, rq.requestor, False, NoEventMask, (XEvent*)&rep);
        XFlush(d);
        size_t off = 0;
        for (;;) {
            XEvent pe;
            if (!next_typed(d, rq.requestor, PropertyNotify, &pe, 5000)) break;
            if (pe.xproperty.atom != rq.property || pe.xproperty.state != PropertyDelete) continue;
            size_t n = co->len - off > 4096 ? 4096 : co->len - off;
            XChangeProperty(d, rq.requestor, rq.property, utf8, 8, PropModeReplace, (const unsigned char*)co->text + off, (int)n);
            XFlush(d);
            off += n;
            if (n == 0) { co->served = 1; break; }
        }
    }
    usleep(100 * 1000);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return NULL;
}

typedef struct { char got[256]; int ok; } ClipReader;

// Asks the current CLIPBOARD owner for UTF8_STRING (as another app would).
static void* clip_reader(void* p) {
    ClipReader* cr = (ClipReader*)p;
    Display* d = XOpenDisplay(NULL);
    if (!d) return NULL;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    Atom clip = atom(d, "CLIPBOARD"), utf8 = atom(d, "UTF8_STRING"), prop = atom(d, "READER_PROP");
    XConvertSelection(d, clip, utf8, prop, w, CurrentTime);
    XFlush(d);
    XEvent e;
    if (next_typed(d, w, SelectionNotify, &e, 3000) && e.xselection.property != None) {
        Atom type; int fmt; unsigned long n, after; unsigned char* data = NULL;
        if (XGetWindowProperty(d, w, prop, 0, 1 << 20, True, AnyPropertyType, &type, &fmt, &n, &after, &data) == Success && data) {
            snprintf(cr->got, sizeof(cr->got), "%.*s", (int)n, (char*)data);
            cr->ok = 1;
            XFree(data);
        }
    }
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return NULL;
}

typedef struct { int docked; Window icon; int stop; int ready; } FakeTray;

// Owns _NET_SYSTEM_TRAY_S<screen>, docks the first icon that asks, then
// clicks it: left, right, and a double click.
static void* fake_tray(void* p) {
    FakeTray* ft = (FakeTray*)p;
    Display* d = XOpenDisplay(NULL);
    if (!d) return NULL;
    char sel[64];
    snprintf(sel, sizeof(sel), "_NET_SYSTEM_TRAY_S%d", DefaultScreen(d));
    Window m = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 64, 32, 0, 0, 0);
    XSetSelectionOwner(d, atom(d, sel), m, CurrentTime);
    XSync(d, False);
    ft->ready = 1;
    XEvent e;
    Atom opcode = atom(d, "_NET_SYSTEM_TRAY_OPCODE");
    for (int i = 0; i < 1000 && !ft->stop; i++) {
        if (XCheckTypedWindowEvent(d, m, ClientMessage, &e) && e.xclient.message_type == opcode && e.xclient.data.l[1] == 0) {
            ft->icon = (Window)e.xclient.data.l[2];
            XReparentWindow(d, ft->icon, m, 4, 4);
            XResizeWindow(d, ft->icon, 24, 24);
            XMapWindow(d, ft->icon);
            XSync(d, False);
            ft->docked = 1;
            usleep(200 * 1000);
            unsigned buttons[4] = { 1, 3, 1, 1 };
            for (int b = 0; b < 4; b++) {
                XEvent c;
                memset(&c, 0, sizeof(c));
                c.xbutton.type = ButtonRelease;
                c.xbutton.window = ft->icon;
                c.xbutton.button = buttons[b];
                c.xbutton.x = c.xbutton.y = 10;
                c.xbutton.same_screen = True;
                XSendEvent(d, ft->icon, True, ButtonReleaseMask, &c);
                XFlush(d);
                usleep(b == 2 ? 50 * 1000 : 600 * 1000); // the last two: a double click
            }
            break;
        }
        usleep(10 * 1000);
    }
    while (!ft->stop) usleep(20 * 1000);
    XDestroyWindow(d, m);
    XCloseDisplay(d);
    return NULL;
}

static void desktop_tests(GW_Window* main_w) {
    GW_Event ev;

    /* popup menu: keyboard and mouse */
    static const char* items[] = { "Open", "", "Pause", "Quit" };
    {
        Script k = { "gw-popup-menu", NULL, 0, NULL, 0, 0, 0, { XK_Down, XK_Down, XK_Return }, 0, 0 };
        pthread_t tk = start_user(&k);
        int r = GW_ShowPopupMenu(main_w, items, 4);
        pthread_join(tk, NULL);
        CHECK(k.ok && r == 2, "popup: Down, Down (skips the separator), Enter -> 2 (%d)", r);
    }
    {
        // Click on "Quit": pad 4 + item 28 + separator 9 + item 28 + half an item.
        Script c = { "gw-popup-menu", NULL, 0, NULL, 0, 0, 0, { 0 }, 30, 4 + 28 + 9 + 28 + 14 };
        pthread_t tc = start_user(&c);
        int r = GW_ShowPopupMenu(main_w, items, 4);
        pthread_join(tc, NULL);
        CHECK(c.ok && r == 3, "popup: click on the 4th item -> 3 (%d)", r);
    }
    pthread_t t;
    {
        Script esc = { "gw-popup-menu", NULL, XK_Escape, NULL, 0, 0, 0, { 0 }, 0, 0 };
        t = start_user(&esc);
        int r = GW_ShowPopupMenu(main_w, items, 4);
        pthread_join(t, NULL);
        CHECK(esc.ok && r == -1, "popup: Escape -> -1 (%d)", r);
    }

    /* clipboard: we own it, another client reads it */
    while (GW_PollEvent(&ev)) {}
    CHECK(GW_SetClipboardText(main_w, "h\xc3\xa9llo clipboard \xe2\x9c\x93"), "set clipboard");
    char* own = GW_GetClipboardText(main_w);
    CHECK(own && strcmp(own, "h\xc3\xa9llo clipboard \xe2\x9c\x93") == 0, "own clipboard text: %s", own ? own : "(null)");
    free(own);
    ClipReader cr;
    memset(&cr, 0, sizeof(cr));
    pthread_create(&t, NULL, clip_reader, &cr);
    // Serve its request from our event loop.
    uint64_t end = GW_GetTicks() + 3000;
    while (GW_GetTicks() < end && !cr.ok) GW_WaitEventTimeout(&ev, 20);
    pthread_join(t, NULL);
    CHECK(cr.ok && strcmp(cr.got, "h\xc3\xa9llo clipboard \xe2\x9c\x93") == 0, "another app reads our clipboard: %s", cr.got);

    /* clipboard: another client owns it (plain, then INCR) */
    ClipOwner co = { "from another app \xc3\xb1", strlen("from another app \xc3\xb1"), 0, 0, 0, (Window)main_w->win };
    pthread_create(&t, NULL, clip_owner, &co);
    while (!co.ready) usleep(5 * 1000);
    char* got = GW_GetClipboardText(main_w);
    pthread_join(t, NULL);
    CHECK(got && strcmp(got, co.text) == 0, "read another app's clipboard: %s", got ? got : "(null)");
    free(got);

    size_t big_n = 50000;
    char* big = (char*)malloc(big_n + 1);
    for (size_t i = 0; i < big_n; i++) big[i] = (char)('a' + i % 26);
    big[big_n] = 0;
    ClipOwner ci = { big, big_n, 1, 0, 0, (Window)main_w->win };
    pthread_create(&t, NULL, clip_owner, &ci);
    while (!ci.ready) usleep(5 * 1000);
    got = GW_GetClipboardText(main_w);
    pthread_join(t, NULL);
    CHECK(got && strlen(got) == big_n && memcmp(got, big, big_n) == 0, "INCR transfer of %zu bytes (got %zu)", big_n, got ? strlen(got) : 0);
    free(got);
    free(big);

    /* tray icon: none without a tray manager; docked and clicked with one */
    uint32_t px[16 * 16];
    for (int i = 0; i < 256; i++) px[i] = 0xFF3366CC;
    GW_Image* ico = GW_CreateImage(16, 16, px);
    FakeTray ft;
    memset(&ft, 0, sizeof(ft));
    pthread_create(&t, NULL, fake_tray, &ft);
    while (!ft.ready) usleep(5 * 1000);
    GW_TrayIcon* tray = GW_CreateTrayIcon(main_w, 5, ico, "gw tray \xe2\x9c\x93");
    CHECK(tray != NULL, "tray icon with a tray manager");
    int clicks = 0, rights = 0, doubles = 0, bad_window = 0;
    end = GW_GetTicks() + 6000;
    while (GW_GetTicks() < end && (clicks + rights + doubles) < 4) {
        if (!GW_WaitEventTimeout(&ev, 50) || ev.type != GW_EVENT_TRAY) continue;
        if (ev.window != main_w || ev.tray.id != 5) bad_window = 1;
        if (ev.tray.action == GW_TRAY_CLICK) clicks++;
        if (ev.tray.action == GW_TRAY_RIGHT_CLICK) rights++;
        if (ev.tray.action == GW_TRAY_DOUBLE_CLICK) doubles++;
    }
    CHECK(ft.docked, "the tray manager docked the icon");
    CHECK(clicks == 2 && rights == 1 && doubles == 1 && !bad_window,
          "tray events: click %d, right %d, double %d (window ok %d)", clicks, rights, doubles, !bad_window);
    GW_SetTrayTooltip(tray, "changed");
    GW_SetTrayIcon(tray, ico);
    GW_DestroyTrayIcon(tray);
    ft.stop = 1;
    pthread_join(t, NULL);
    usleep(100 * 1000);
    CHECK(GW_CreateTrayIcon(main_w, 6, ico, "x") == NULL, "no tray manager -> NULL");
    GW_FreeImage(ico);
}

static void touch(const char* path) { FILE* f = fopen(path, "w"); if (f) { fputs("x", f); fclose(f); } }

int main(void) {
    if (!getenv("GW_FONT") && getenv("GW_TEST_FONT")) setenv("GW_FONT", getenv("GW_TEST_FONT"), 1);
    GW_Window* main_w = GW_CreateWindow("gw_dialog_test main", 200, 120);
    CHECK(main_w != NULL, "main window");

    /* message box: Enter = first button, Escape = last */
    static const wchar_t* three[] = { L"Save", L"Don't save", L"Cancel" };
    Script s1 = { "Dlg A", NULL, XK_Return, NULL, 0, 0 };
    pthread_t t = start_user(&s1);
    int r = GW_ShowMessageBox(main_w, "Dlg A", L"Save the changes to \x201cnotes\x201d before closing?", three, 3);
    pthread_join(t, NULL);
    CHECK(s1.ok && r == 0, "message box: Enter -> first button (%d, found %d)", r, s1.ok);

    Script s2 = { "Dlg B", NULL, XK_Escape, NULL, 0, 0 };
    t = start_user(&s2);
    r = GW_ShowMessageBox(main_w, "Dlg B", L"Escape closes it", three, 3);
    pthread_join(t, NULL);
    CHECK(s2.ok && r == 2, "message box: Escape -> last button (%d)", r);

    /* events of other windows that arrive during a dialog are kept */
    GW_Event ev;
    while (GW_PollEvent(&ev)) {}
    GW_PostUserEvent(main_w, 77, NULL);
    Script s3 = { "Dlg C", NULL, XK_Return, NULL, 0, 0 };
    t = start_user(&s3);
    GW_ShowMessageBox(main_w, "Dlg C", L"one button", NULL, 0);
    pthread_join(t, NULL);
    int got = 0;
    while (GW_PollEvent(&ev)) if (ev.type == GW_EVENT_USER && ev.user.code == 77 && ev.window == main_w) got = 1;
    CHECK(got, "event for the main window posted during the dialog is delivered after it");

    /* file dialogs in a scratch folder */
    char tmpl[] = "/tmp/gw_dialog_test_XXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != NULL, "temp dir");
    if (!dir) return 1;
    char path[512], want[512];
    snprintf(path, sizeof(path), "%s/a.txt", dir); touch(path);
    snprintf(path, sizeof(path), "%s/b.png", dir); touch(path);
    snprintf(path, sizeof(path), "%s/sub", dir); mkdir(path, 0700);
    if (chdir(dir) != 0) return 1;
    char real[512];
    if (!realpath(dir, real)) snprintf(real, sizeof(real), "%s", dir);

    Script s4 = { "Open test", "b.png", XK_Return, NULL, 0, 0 };
    t = start_user(&s4);
    char* got_path = GW_ShowOpenFileDialog(main_w, "Open test", "Images|*.png|All files|*.*");
    pthread_join(t, NULL);
    snprintf(want, sizeof(want), "%s/b.png", real);
    CHECK(got_path && strcmp(got_path, want) == 0, "open: typed name + Enter -> %s (got %s)", want, got_path ? got_path : "(null)");
    free(got_path);

    // Enter on a missing file keeps the dialog open; then Escape cancels.
    Script s5 = { "Open missing", "zzz.txt", XK_Return, NULL, 0, 0, XK_Escape };
    t = start_user(&s5);
    got_path = GW_ShowOpenFileDialog(main_w, "Open missing", NULL);
    pthread_join(t, NULL);
    CHECK(got_path == NULL, "open: Escape cancels (got %s)", got_path ? got_path : "(null)");
    free(got_path);

    Script s6 = { "Save test", "new_file.txt", XK_Return, NULL, 0, 0 };
    t = start_user(&s6);
    got_path = GW_ShowSaveFileDialog(main_w, "Save test", NULL);
    pthread_join(t, NULL);
    snprintf(want, sizeof(want), "%s/new_file.txt", real);
    CHECK(got_path && strcmp(got_path, want) == 0, "save: new name -> %s (got %s)", want, got_path ? got_path : "(null)");
    free(got_path);

    Script s7 = { "Save over", "a.txt", XK_Return, "Confirm", XK_Return, 0 };
    t = start_user(&s7);
    got_path = GW_ShowSaveFileDialog(main_w, "Save over", NULL);
    pthread_join(t, NULL);
    snprintf(want, sizeof(want), "%s/a.txt", real);
    CHECK(s7.ok && got_path && strcmp(got_path, want) == 0, "save over an existing file asks, Enter = Replace (got %s, confirm %d)", got_path ? got_path : "(null)", s7.ok);
    free(got_path);

    desktop_tests(main_w);

    GW_DestroyWindow(main_w);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
#endif
