// X11 side of the extensions (feat/portmaster-ui): cursor, DPI, window
// icon/min size/focus/visibility/redraw. The event-loop parts (user events,
// timers, wheel, focus, double clicks) live in gw_x11.c.
//
// Not implemented on X11 yet (documented stubs):
//   * clipboard: needs selection ownership served from the event loop
//     (SelectionRequest / INCR); GW_SetClipboardText returns 0 and
//     GW_GetClipboardText NULL;
//   * tray icon (XEmbed / StatusNotifierItem over D-Bus): returns NULL;
//   * popup menu: returns -1.
#ifndef _WIN32

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/cursorfont.h>
#include <stdlib.h>
#include <string.h>
#include "gw_internal.h"

/* ---- cursor ------------------------------------------------------------------- */

void GW_SetCursor(GW_Window* win, GW_Cursor cursor) {
    if (!win || !win->dpy) return;
    Display* dpy = (Display*)win->dpy;
    unsigned shape = XC_left_ptr;
    switch (cursor) {
        case GW_CURSOR_HAND:      shape = XC_hand2; break;
        case GW_CURSOR_IBEAM:     shape = XC_xterm; break;
        case GW_CURSOR_SIZE_WE:   shape = XC_sb_h_double_arrow; break;
        case GW_CURSOR_SIZE_NS:   shape = XC_sb_v_double_arrow; break;
        case GW_CURSOR_WAIT:      shape = XC_watch; break;
        case GW_CURSOR_CROSSHAIR: shape = XC_crosshair; break;
        default:                  shape = XC_left_ptr; break;
    }
    // Font cursors are cached per shape for the life of the display.
    static Cursor cache[XC_num_glyphs];
    static Display* cache_dpy = NULL;
    if (cache_dpy != dpy) { memset(cache, 0, sizeof(cache)); cache_dpy = dpy; }
    if (!cache[shape]) cache[shape] = XCreateFontCursor(dpy, shape);
    XDefineCursor(dpy, (Window)win->win, cache[shape]);
    XFlush(dpy);
}

/* ---- DPI ------------------------------------------------------------------------ */

void GW_EnableHighDpi(void) {
    // X11 has no per-process scaling: the backbuffer is always in real pixels.
}

float GW_GetDpiScale(GW_Window* win) {
    if (!win || !win->dpy) return 1.0f;
    Display* dpy = (Display*)win->dpy;
    float scale = 1.0f;
    char* rms = XResourceManagerString(dpy); // owned by Xlib
    if (rms) {
        XrmInitialize();
        XrmDatabase db = XrmGetStringDatabase(rms);
        if (db) {
            char* type = NULL;
            XrmValue value;
            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) {
                float dpi = (float)atof(value.addr);
                if (dpi > 0) scale = dpi / 96.0f;
            }
            XrmDestroyDatabase(db);
        }
    }
    return scale;
}

/* ---- clipboard / tray / popup: stubs ------------------------------------------------ */

int GW_SetClipboardText(GW_Window* win, const char* utf8) {
    (void)win; (void)utf8;
    return 0;
}

char* GW_GetClipboardText(GW_Window* win) {
    (void)win;
    return NULL;
}

GW_TrayIcon* GW_CreateTrayIcon(GW_Window* owner, int id, GW_Image* icon, const char* tooltip_utf8) {
    (void)owner; (void)id; (void)icon; (void)tooltip_utf8;
    return NULL;
}

void GW_SetTrayIcon(GW_TrayIcon* tray, GW_Image* icon) { (void)tray; (void)icon; }
void GW_SetTrayTooltip(GW_TrayIcon* tray, const char* tooltip_utf8) { (void)tray; (void)tooltip_utf8; }
void GW_DestroyTrayIcon(GW_TrayIcon* tray) { (void)tray; }

int GW_ShowPopupMenu(GW_Window* owner, const char** items, int count) {
    (void)owner; (void)items; (void)count;
    return -1;
}

/* ---- window extras -------------------------------------------------------------------- */

void GW_SetWindowIcon(GW_Window* win, GW_Image* icon) {
    if (!win || !win->dpy || !icon || !icon->pixels || icon->w <= 0 || icon->h <= 0) return;
    Display* dpy = (Display*)win->dpy;
    // _NET_WM_ICON: CARDINAL[] = width, height, then ARGB pixels, each as a
    // long (32 significant bits even where long is 64-bit).
    size_t n = 2 + (size_t)icon->w * icon->h;
    unsigned long* data = (unsigned long*)malloc(n * sizeof(unsigned long));
    if (!data) return;
    data[0] = (unsigned long)icon->w;
    data[1] = (unsigned long)icon->h;
    for (size_t i = 0; i < (size_t)icon->w * icon->h; i++) data[2 + i] = icon->pixels[i];
    Atom net_wm_icon = XInternAtom(dpy, "_NET_WM_ICON", False);
    XChangeProperty(dpy, (Window)win->win, net_wm_icon, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char*)data, (int)n);
    XFlush(dpy);
    free(data);
}

void GW_SetMinWindowSize(GW_Window* win, int min_w, int min_h) {
    if (!win) return;
    win->min_w = min_w;
    win->min_h = min_h;
    if (!win->dpy) return;
    XSizeHints* hints = XAllocSizeHints();
    if (!hints) return;
    hints->flags = PMinSize;
    hints->min_width = min_w;
    hints->min_height = min_h;
    XSetWMNormalHints((Display*)win->dpy, (Window)win->win, hints);
    XFree(hints);
    XFlush((Display*)win->dpy);
}

void GW_FocusWindow(GW_Window* win) {
    if (!win || !win->dpy) return;
    Display* dpy = (Display*)win->dpy;
    XMapRaised(dpy, (Window)win->win);
    XSetInputFocus(dpy, (Window)win->win, RevertToParent, CurrentTime);
    XFlush(dpy);
}

int GW_IsWindowVisible(GW_Window* win) {
    if (!win || !win->dpy) return 0;
    XWindowAttributes attrs;
    if (!XGetWindowAttributes((Display*)win->dpy, (Window)win->win, &attrs)) return 0;
    return attrs.map_state == IsViewable;
}

void GW_RequestRedraw(GW_Window* win) {
    if (!win || !win->dpy) return;
    // An empty-area clear with exposures = True generates an Expose event.
    XClearArea((Display*)win->dpy, (Window)win->win, 0, 0, 1, 1, True);
    XFlush((Display*)win->dpy);
}

#endif // !_WIN32
