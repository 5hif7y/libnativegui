// X11 side of the extensions (feat/portmaster-ui): cursor, DPI, clipboard
// (CLIPBOARD selection), tray icon (XEmbed system tray), window icon/min
// size/focus/visibility/redraw. The event-loop parts (user events, timers,
// wheel, focus, double clicks, input method) live in gw_x11.c; the popup
// menu and the dialogs in gw_x11_dialogs.c.
#ifndef _WIN32

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
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

/* ---- clipboard ---------------------------------------------------------------- *
 * CLIPBOARD selection. Set: we own the selection and serve SelectionRequest
 * from the event loop (TARGETS, UTF8_STRING, STRING, TEXT); text larger than
 * one X request is refused (no outgoing INCR). Get: our own text directly,
 * else XConvertSelection to UTF8_STRING (then STRING) and wait for the
 * owner (2 s for the first answer, 3 s between INCR chunks),
 * reading INCR transfers. Other events stay queued. */

static char* clip_text = NULL;       // what we serve while we own CLIPBOARD
static Window clip_window = 0;
static Atom a_clipboard, a_utf8, a_targets, a_text, a_incr, a_gw_clip;

static void clip_atoms(Display* d) {
    if (a_clipboard) return;
    a_clipboard = XInternAtom(d, "CLIPBOARD", False);
    a_utf8      = XInternAtom(d, "UTF8_STRING", False);
    a_targets   = XInternAtom(d, "TARGETS", False);
    a_text      = XInternAtom(d, "TEXT", False);
    a_incr      = XInternAtom(d, "INCR", False);
    a_gw_clip   = XInternAtom(d, "GW_CLIPBOARD", False);
}

static int clip_hook(XEvent* xev) {
    if (xev->type == SelectionClear) {
        if (xev->xselectionclear.window == clip_window && xev->xselectionclear.selection == a_clipboard) {
            free(clip_text);
            clip_text = NULL;
            clip_window = 0;
        }
        return xev->xselectionclear.window == clip_window || xev->xselectionclear.selection == a_clipboard;
    }
    if (xev->type != SelectionRequest) return 0;
    XSelectionRequestEvent* rq = &xev->xselectionrequest;
    XSelectionEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = SelectionNotify;
    reply.display = rq->display;
    reply.requestor = rq->requestor;
    reply.selection = rq->selection;
    reply.target = rq->target;
    reply.time = rq->time;
    reply.property = None;
    Atom prop = rq->property != None ? rq->property : rq->target; // obsolete clients
    if (rq->selection == a_clipboard && clip_text) {
        size_t len = strlen(clip_text);
        long max_bytes = XMaxRequestSize(rq->display) * 4 - 256;
        if (rq->target == a_targets) {
            Atom targets[4] = { a_targets, a_utf8, XA_STRING, a_text };
            XChangeProperty(rq->display, rq->requestor, prop, XA_ATOM, 32, PropModeReplace,
                            (unsigned char*)targets, 4);
            reply.property = prop;
        } else if ((rq->target == a_utf8 || rq->target == XA_STRING || rq->target == a_text) &&
                   (long)len <= max_bytes) {
            XChangeProperty(rq->display, rq->requestor, prop, rq->target == XA_STRING ? XA_STRING : a_utf8,
                            8, PropModeReplace, (unsigned char*)clip_text, (int)len);
            reply.property = prop;
        }
    }
    XSendEvent(rq->display, rq->requestor, False, NoEventMask, (XEvent*)&reply);
    XFlush(rq->display);
    return 1;
}

int GW_SetClipboardText(GW_Window* win, const char* utf8) {
    if (!win || !win->dpy || !utf8) return 0;
    Display* d = (Display*)win->dpy;
    clip_atoms(d);
    gw_x11_add_event_hook((GW_X11_EventHook)clip_hook);
    char* copy = strdup(utf8);
    if (!copy) return 0;
    free(clip_text);
    clip_text = copy;
    clip_window = (Window)win->win;
    XSetSelectionOwner(d, a_clipboard, clip_window, CurrentTime);
    XFlush(d);
    if (XGetSelectionOwner(d, a_clipboard) != clip_window) {
        free(clip_text);
        clip_text = NULL;
        clip_window = 0;
        return 0;
    }
    return 1;
}

// Waits (up to deadline) for an event of type on window w, leaving the
// rest queued. Returns 1 and fills out on success.
static int wait_typed(Display* d, Window w, int type, XEvent* out, uint64_t deadline) {
    for (;;) {
        if (XCheckTypedWindowEvent(d, w, type, out)) return 1;
        uint64_t now = GW_GetTicks();
        if (now >= deadline) return 0;
        int fd = ConnectionNumber(d);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        uint64_t left = deadline - now;
        struct timeval tv = { (time_t)(left / 1000), (suseconds_t)((left % 1000) * 1000) };
        XFlush(d);
        select(fd + 1, &fds, NULL, NULL, &tv);
    }
}

// Reads (and deletes) property prop of w: plain or INCR. malloc'd, NUL-terminated.
static char* read_selection_property(Display* d, Window w, Atom prop, uint64_t deadline) {
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char* data = NULL;
    if (XGetWindowProperty(d, w, prop, 0, 0x7fffffff, True, AnyPropertyType,
                           &type, &format, &n, &after, &data) != Success) return NULL;
    if (type == a_incr) {
        // INCR: the owner sends chunks; each PropertyNotify(NewValue) is one,
        // a zero-length chunk ends it. Deleting the property asks for more.
        deadline = GW_GetTicks() + 3000;
        if (data) XFree(data);
        size_t cap = 4096, len = 0;
        char* out = (char*)malloc(cap);
        if (!out) return NULL;
        XFlush(d);
        for (;;) {
            XEvent pe;
            do {
                if (!wait_typed(d, w, PropertyNotify, &pe, deadline)) { free(out); return NULL; }
            } while (pe.xproperty.atom != prop || pe.xproperty.state != PropertyNewValue);
            unsigned char* chunk = NULL;
            if (XGetWindowProperty(d, w, prop, 0, 0x7fffffff, True, AnyPropertyType,
                                   &type, &format, &n, &after, &chunk) != Success) { free(out); return NULL; }
            // A stale NewValue (e.g. the INCR announcement itself, already
            // deleted) reads as no property: not the end, keep waiting. The
            // end is an existing, empty chunk.
            if (type == None) { if (chunk) XFree(chunk); continue; }
            size_t bytes = (size_t)n * (size_t)(format / 8);
            if (bytes == 0) { if (chunk) XFree(chunk); break; }
            if (len + bytes + 1 > cap) {
                while (len + bytes + 1 > cap) cap *= 2;
                char* grown = (char*)realloc(out, cap);
                if (!grown) { free(out); XFree(chunk); return NULL; }
                out = grown;
            }
            memcpy(out + len, chunk, bytes);
            len += bytes;
            deadline = GW_GetTicks() + 3000; // progress: the next chunk gets its own 3 s
            XFree(chunk);
            XFlush(d);
        }
        out[len] = 0;
        return out;
    }
    if (!data) return NULL;
    size_t bytes = (size_t)n * (size_t)(format / 8);
    char* out = (char*)malloc(bytes + 1);
    if (out) { memcpy(out, data, bytes); out[bytes] = 0; }
    XFree(data);
    return out;
}

char* GW_GetClipboardText(GW_Window* win) {
    if (!win || !win->dpy) return NULL;
    Display* d = (Display*)win->dpy;
    clip_atoms(d);
    gw_x11_add_event_hook((GW_X11_EventHook)clip_hook);
    Window owner = XGetSelectionOwner(d, a_clipboard);
    if (owner == None) return NULL;
    if (clip_text && owner == clip_window) return strdup(clip_text);
    Window w = (Window)win->win;
    // ICCCM leaves timeouts to the client; owners can be slow programs.
    uint64_t deadline = GW_GetTicks() + 2000;
    Atom targets[2] = { a_utf8, XA_STRING };
    for (int i = 0; i < 2; i++) {
        XDeleteProperty(d, w, a_gw_clip);
        XConvertSelection(d, a_clipboard, targets[i], a_gw_clip, w, CurrentTime);
        XEvent ev;
        if (!wait_typed(d, w, SelectionNotify, &ev, deadline)) return NULL;
        if (ev.xselection.property == None) continue; // owner can't: try STRING
        char* text = read_selection_property(d, w, a_gw_clip, deadline);
        if (!text) return NULL;
        if (targets[i] == XA_STRING) {
            // STRING is Latin-1: convert to UTF-8.
            size_t n = strlen(text);
            char* u = (char*)malloc(n * 2 + 1);
            if (!u) { free(text); return NULL; }
            size_t j = 0;
            for (size_t k = 0; k < n; k++) {
                unsigned char ch = (unsigned char)text[k];
                if (ch < 0x80) u[j++] = (char)ch;
                else { u[j++] = (char)(0xC0 | (ch >> 6)); u[j++] = (char)(0x80 | (ch & 0x3F)); }
            }
            u[j] = 0;
            free(text);
            text = u;
        }
        return text;
    }
    return NULL;
}

/* ---- tray icon (XEmbed system tray) --------------------------------------------- *
 * freedesktop "System Tray Protocol": the tray manager owns the selection
 * _NET_SYSTEM_TRAY_S<screen>; the icon is a small window we ask it to dock
 * (SYSTEM_TRAY_REQUEST_DOCK) and that it reparents. We draw the icon scaled
 * to the size the tray gives us, pixels with alpha >= 50% over the tray's
 * background (ParentRelative). Without a tray manager (GNOME without the
 * AppIndicator extension, WSLg): NULL, as documented. StatusNotifierItem
 * (D-Bus) is not implemented. Clicks become GW_EVENT_TRAY on the owner:
 * left release = CLICK (DOUBLE_CLICK when it is the second within 400 ms),
 * right release = RIGHT_CLICK. The tooltip goes to the window's WM_NAME. */

struct GW_TrayIcon {
    GW_Window* owner;
    Display* dpy;
    Window w;
    int id;
    int size_w, size_h;
    uint32_t* pixels; int img_w, img_h; // copy of the icon (ARGB)
    uint64_t last_click;
    struct GW_TrayIcon* next;
};

static struct GW_TrayIcon* trays = NULL;

static void tray_draw(struct GW_TrayIcon* t) {
    if (!t->pixels || t->size_w <= 0 || t->size_h <= 0) return;
    Display* d = t->dpy;
    int scr = DefaultScreen(d);
    int W = t->size_w, H = t->size_h;
    // Square icon, centred.
    int side = W < H ? W : H;
    int ox = (W - side) / 2, oy = (H - side) / 2;
    uint32_t* buf = (uint32_t*)calloc((size_t)W * H, sizeof(uint32_t));
    unsigned char* mask_bits = (unsigned char*)calloc((size_t)((W + 7) / 8) * H, 1);
    if (!buf || !mask_bits) { free(buf); free(mask_bits); return; }
    for (int y = 0; y < side; y++) {
        int sy = y * t->img_h / side;
        for (int x = 0; x < side; x++) {
            int sx = x * t->img_w / side;
            uint32_t p = t->pixels[sy * t->img_w + sx];
            if ((p >> 24) >= 0x80) {
                buf[(oy + y) * W + ox + x] = p & 0x00FFFFFF;
                mask_bits[(oy + y) * ((W + 7) / 8) + (ox + x) / 8] |= (unsigned char)(1 << ((ox + x) % 8));
            }
        }
    }
    XClearWindow(d, t->w); // ParentRelative: the tray's own background
    Pixmap mask = XCreateBitmapFromData(d, t->w, (char*)mask_bits, (unsigned)W, (unsigned)H);
    GC gc = XCreateGC(d, t->w, 0, NULL);
    XSetClipMask(d, gc, mask);
    XImage* img = XCreateImage(d, DefaultVisual(d, scr), (unsigned)DefaultDepth(d, scr), ZPixmap, 0,
                               (char*)buf, (unsigned)W, (unsigned)H, 32, 0);
    if (img) {
        XPutImage(d, t->w, gc, img, 0, 0, 0, 0, (unsigned)W, (unsigned)H);
        img->data = NULL; // buf is freed below
        XDestroyImage(img);
    }
    XFreeGC(d, gc);
    XFreePixmap(d, mask);
    free(buf);
    free(mask_bits);
    XFlush(d);
}

static int tray_hook(XEvent* xev) {
    for (struct GW_TrayIcon* t = trays; t; t = t->next) {
        if (xev->xany.window != t->w) continue;
        switch (xev->type) {
            case Expose:
                if (xev->xexpose.count == 0) tray_draw(t);
                break;
            case ConfigureNotify:
                if (xev->xconfigure.width != t->size_w || xev->xconfigure.height != t->size_h) {
                    t->size_w = xev->xconfigure.width;
                    t->size_h = xev->xconfigure.height;
                    tray_draw(t);
                }
                break;
            case ButtonRelease: {
                int action = 0;
                if (xev->xbutton.button == 1) {
                    uint64_t now = GW_GetTicks();
                    action = (t->last_click && now - t->last_click <= 400) ? GW_TRAY_DOUBLE_CLICK : GW_TRAY_CLICK;
                    t->last_click = action == GW_TRAY_DOUBLE_CLICK ? 0 : now;
                } else if (xev->xbutton.button == 3) {
                    action = GW_TRAY_RIGHT_CLICK;
                }
                if (action) {
                    GW_Event ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = GW_EVENT_TRAY;
                    ev.window = t->owner;
                    ev.tray.id = t->id;
                    ev.tray.action = action;
                    gw_x11_push_event(ev);
                }
                break;
            }
            default:
                break;
        }
        return 1;
    }
    return 0;
}

static void tray_set_pixels(struct GW_TrayIcon* t, GW_Image* icon) {
    free(t->pixels);
    t->pixels = NULL;
    if (!icon || !icon->pixels || icon->w <= 0 || icon->h <= 0) return;
    size_t n = (size_t)icon->w * icon->h;
    t->pixels = (uint32_t*)malloc(n * sizeof(uint32_t));
    if (!t->pixels) return;
    memcpy(t->pixels, icon->pixels, n * sizeof(uint32_t));
    t->img_w = icon->w;
    t->img_h = icon->h;
}

GW_TrayIcon* GW_CreateTrayIcon(GW_Window* owner, int id, GW_Image* icon, const char* tooltip_utf8) {
    if (!owner || !owner->dpy) return NULL;
    Display* d = (Display*)owner->dpy;
    int scr = DefaultScreen(d);
    char sel_name[64];
    snprintf(sel_name, sizeof(sel_name), "_NET_SYSTEM_TRAY_S%d", scr);
    Window manager = XGetSelectionOwner(d, XInternAtom(d, sel_name, False));
    if (manager == None) return NULL; // no tray on this desktop

    struct GW_TrayIcon* t = (struct GW_TrayIcon*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->owner = owner;
    t->dpy = d;
    t->id = id;
    t->size_w = t->size_h = 22;
    tray_set_pixels(t, icon);

    XSetWindowAttributes wa;
    memset(&wa, 0, sizeof(wa));
    wa.background_pixmap = ParentRelative;
    wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask;
    t->w = XCreateWindow(d, RootWindow(d, scr), 0, 0, (unsigned)t->size_w, (unsigned)t->size_h, 0,
                         CopyFromParent, InputOutput, CopyFromParent, CWBackPixmap | CWEventMask, &wa);
    // _XEMBED_INFO: protocol version 0, flags XEMBED_MAPPED.
    long info[2] = { 0, 1 };
    Atom xembed_info = XInternAtom(d, "_XEMBED_INFO", False);
    XChangeProperty(d, t->w, xembed_info, xembed_info, 32, PropModeReplace, (unsigned char*)info, 2);
    if (tooltip_utf8) {
        Xutf8SetWMProperties(d, t->w, tooltip_utf8, tooltip_utf8, NULL, 0, NULL, NULL, NULL);
    }
    t->next = trays;
    trays = t;
    gw_x11_add_event_hook((GW_X11_EventHook)tray_hook);

    XEvent e;
    memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.window = manager;
    e.xclient.message_type = XInternAtom(d, "_NET_SYSTEM_TRAY_OPCODE", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = 0; // SYSTEM_TRAY_REQUEST_DOCK
    e.xclient.data.l[2] = (long)t->w;
    XSendEvent(d, manager, False, NoEventMask, &e);
    XFlush(d);
    return t;
}

void GW_SetTrayIcon(GW_TrayIcon* tray, GW_Image* icon) {
    if (!tray) return;
    tray_set_pixels(tray, icon);
    tray_draw(tray);
}

void GW_SetTrayTooltip(GW_TrayIcon* tray, const char* tooltip_utf8) {
    if (!tray || !tooltip_utf8) return;
    Xutf8SetWMProperties(tray->dpy, tray->w, tooltip_utf8, tooltip_utf8, NULL, 0, NULL, NULL, NULL);
    XFlush(tray->dpy);
}

void GW_DestroyTrayIcon(GW_TrayIcon* tray) {
    if (!tray) return;
    for (struct GW_TrayIcon** p = &trays; *p; p = &(*p)->next) {
        if (*p == tray) { *p = tray->next; break; }
    }
    XDestroyWindow(tray->dpy, tray->w);
    XFlush(tray->dpy);
    free(tray->pixels);
    free(tray);
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
    Window w = (Window)win->win;
    XMapRaised(dpy, w);
    // Ask the window manager (EWMH _NET_ACTIVE_WINDOW, source 1 = application);
    // it also restores a minimised window.
    XEvent e;
    memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = 1;
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    // Without a window manager, set the focus directly; only legal once the
    // window is viewable (otherwise BadMatch), so not right after mapping.
    XWindowAttributes a;
    if (XGetWindowAttributes(dpy, w, &a) && a.map_state == IsViewable)
        XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
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

void GW_GetMousePosition(GW_Window* win, int* sx, int* sy, int* wx, int* wy) {
    int rx = 0, ry = 0, cx = 0, cy = 0;
    if (win && win->dpy) {
        Display* d = (Display*)win->dpy;
        Window root, child;
        unsigned int mask;
        if (XQueryPointer(d, (Window)win->win, &root, &child, &rx, &ry, &cx, &cy, &mask) == False) {
            cx = rx; cy = ry;
        }
    } else {
        // No window: open a short-lived connection for the screen position.
        Display* d = XOpenDisplay(NULL);
        if (d) {
            Window root, child;
            unsigned int mask;
            XQueryPointer(d, DefaultRootWindow(d), &root, &child, &rx, &ry, &cx, &cy, &mask);
            XCloseDisplay(d);
        }
        cx = rx; cy = ry;
    }
    if (sx) *sx = rx;
    if (sy) *sy = ry;
    if (wx) *wx = cx;
    if (wy) *wy = cy;
}

#endif // !_WIN32
