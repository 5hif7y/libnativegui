// Automated checks for the built-in X11 dialogs (gw_x11_dialogs.c). A
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
                 const char* confirm_title; KeySym confirm_key; int ok; KeySym then_key; } Script;

static void* user(void* p) {
    Script* sc = (Script*)p;
    Display* d = XOpenDisplay(NULL);
    if (!d) return NULL;
    Window w = wait_window(d, sc->title);
    if (w) {
        if (sc->text) type_text(d, w, sc->text);
        send_key(d, w, sc->final_key, 0);
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

    GW_DestroyWindow(main_w);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
#endif
