#ifndef _WIN32

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "gw_internal.h"
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>

// Dialogs. With GW_X11_EXTERNAL_DIALOGS (CMake NATIVEGUI_X11_DIALOGS=ON) they
// use MessageBox-X11 and sofd (x_fib), which must be linked in: neither is
// part of this repository. Without it, the fallbacks below print to stderr
// and report "cancelled" (-1 / NULL), so the library links on its own.
#ifdef GW_X11_EXTERNAL_DIALOGS
typedef struct {
    wchar_t *label;
    int result;
} X11_Button;

extern int Messagebox(const char* title, const wchar_t* text, const X11_Button* buttons, int numButtons);

extern int x_fib_show (Display *dpy, Window parent, int x, int y);
extern void x_fib_close (Display *dpy);
extern int x_fib_handle_events (Display *dpy, XEvent *event);
extern int x_fib_status ();
extern char *x_fib_filename ();
extern int x_fib_configure (int k, const char *v);
#endif

// Event queue for X11
#define EVENT_QUEUE_SIZE 256
static GW_Event event_queue[EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static Atom WM_DELETE_WINDOW;

extern GW_Window* g_active_window;

static void push_event(GW_Event ev) {
    ev.window = g_active_window;
    int next_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
    if (next_head != queue_tail) {
        event_queue[queue_head] = ev;
        queue_head = next_head;
    }
}

static int pop_event(GW_Event* ev) {
    if (queue_head == queue_tail) return 0;
    *ev = event_queue[queue_tail];
    queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

static Display* global_dpy = NULL;
static Window global_win = 0;

static void process_xevent(XEvent* xev) {
    switch (xev->type) {
        case ClientMessage: {
            if ((Atom)xev->xclient.data.l[0] == WM_DELETE_WINDOW) {
                GW_Event ev;
                ev.type = GW_EVENT_QUIT;
                push_event(ev);
            }
            break;
        }
        
        case Expose: {
            GW_Event ev;
            ev.type = GW_EVENT_WINDOW_EXPOSE;
            push_event(ev);
            break;
        }
        
        case ConfigureNotify: {
            // Find window matching (since we have single window usually, we can map to global_win)
            // In our implementation, we will query via a global or by checking user data if X11 supported context.
            // For a single window library, mapping to a global window is standard and works perfectly.
            // Let's assume win is the active window.
            // We'll read the global window structure.
            // (X11 doesn't have an easy GWLP_USERDATA, so we can store a global active window pointer)
            extern GW_Window* g_active_window;
            if (g_active_window && xev->xconfigure.window == (Window)g_active_window->win) {
                int new_w = xev->xconfigure.width;
                int new_h = xev->xconfigure.height;
                if (new_w != g_active_window->width || new_h != g_active_window->height) {
                    Display* dpy = (Display*)g_active_window->dpy;
                    int ds = DefaultScreen(dpy);
                    
                    if (g_active_window->ximage) {
                        // Destroy old XImage and its buffer
                        XDestroyImage((XImage*)g_active_window->ximage);
                    }
                    
                    g_active_window->width = new_w;
                    g_active_window->height = new_h;
                    g_active_window->backbuffer = (uint32_t*)malloc(new_w * new_h * sizeof(uint32_t));
                    
                    g_active_window->ximage = XCreateImage(
                        dpy,
                        DefaultVisual(dpy, ds),
                        DefaultDepth(dpy, ds),
                        ZPixmap,
                        0,
                        (char*)g_active_window->backbuffer,
                        new_w,
                        new_h,
                        32,
                        0
                    );
                    
                    GW_Event ev;
                    ev.type = GW_EVENT_WINDOW_RESIZE;
                    ev.resize.width = new_w;
                    ev.resize.height = new_h;
                    push_event(ev);
                }
            }
            break;
        }
        
        case MotionNotify: {
            GW_Event ev;
            ev.type = GW_EVENT_MOUSE_MOTION;
            ev.mouse_motion.x = xev->xmotion.x;
            ev.mouse_motion.y = xev->xmotion.y;
            push_event(ev);
            break;
        }
        
        case ButtonPress:
        case ButtonRelease: {
            GW_Event ev;
            memset(&ev, 0, sizeof(ev));
            unsigned b = xev->xbutton.button;
            int mods = 0;
            if (xev->xbutton.state & ShiftMask)   mods |= GW_MOD_SHIFT;
            if (xev->xbutton.state & ControlMask) mods |= GW_MOD_CTRL;
            if (xev->xbutton.state & Mod1Mask)    mods |= GW_MOD_ALT;
            // Buttons 4-7 are the wheel (4/5 vertical, 6/7 horizontal).
            if (b >= 4 && b <= 7 && g_active_window && g_active_window->wheel_mode == GW_WHEEL_EVENTS) {
                if (xev->type == ButtonPress) {
                    ev.type = GW_EVENT_MOUSE_WHEEL;
                    ev.wheel.x = xev->xbutton.x;
                    ev.wheel.y = xev->xbutton.y;
                    ev.wheel.delta = (b == 4 || b == 7) ? 120 : -120;
                    ev.wheel.mod = mods;
                    ev.wheel.horizontal = (b >= 6);
                    push_event(ev);
                }
                break;
            }
            ev.type = (xev->type == ButtonPress) ? GW_EVENT_MOUSE_BUTTON_DOWN : GW_EVENT_MOUSE_BUTTON_UP;
            ev.mouse_button.x = xev->xbutton.x;
            ev.mouse_button.y = xev->xbutton.y;
            ev.mouse_button.button = (int)b; // 1=left, 2=middle, 3=right
            ev.mouse_button.mod = mods;
            ev.mouse_button.clicks = 1;
            if (xev->type == ButtonPress) {
                // Double click: same button again within 400 ms and 4 px.
                static Time last_time = 0;
                static unsigned last_button = 0;
                static int last_x = -1000, last_y = -1000;
                if (b == last_button && xev->xbutton.time - last_time <= 400 &&
                    abs(xev->xbutton.x - last_x) <= 4 && abs(xev->xbutton.y - last_y) <= 4) {
                    ev.mouse_button.clicks = 2;
                    last_button = 0; // a third click starts over
                } else {
                    last_button = b;
                }
                last_time = xev->xbutton.time;
                last_x = xev->xbutton.x;
                last_y = xev->xbutton.y;
            }
            push_event(ev);
            break;
        }

        case FocusIn:
        case FocusOut: {
            GW_Event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = (xev->type == FocusIn) ? GW_EVENT_FOCUS_GAINED : GW_EVENT_FOCUS_LOST;
            push_event(ev);
            break;
        }
        
        case KeyPress:
        case KeyRelease: {
            KeySym keysym;
            char text_buf[8] = {0};
            int len = XLookupString(&xev->xkey, text_buf, sizeof(text_buf), &keysym, NULL);
            
            GW_Event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = (xev->type == KeyPress) ? GW_EVENT_KEY_DOWN : GW_EVENT_KEY_UP;
            
            int keycode = 0;
            switch (keysym) {
                case XK_BackSpace: keycode = GW_KEY_BACKSPACE; break;
                case XK_Tab:       keycode = GW_KEY_TAB; break;
                case XK_Return:    keycode = GW_KEY_ENTER; break;
                case XK_Escape:    keycode = GW_KEY_ESCAPE; break;
                case XK_Left:      keycode = GW_KEY_LEFT; break;
                case XK_Up:        keycode = GW_KEY_UP; break;
                case XK_Right:     keycode = GW_KEY_RIGHT; break;
                case XK_Down:      keycode = GW_KEY_DOWN; break;
                case XK_Delete:    keycode = GW_KEY_DELETE; break;
                case XK_Home:      keycode = GW_KEY_HOME; break;
                case XK_End:       keycode = GW_KEY_END; break;
                case XK_Page_Up:   keycode = GW_KEY_PAGEUP; break;
                case XK_Page_Down: keycode = GW_KEY_PAGEDOWN; break;
                default:
                    if (keysym >= 32 && keysym <= 126) {
                        keycode = (int)keysym;
                    } else {
                        keycode = (int)keysym; // Fallback to raw keysym
                    }
                    break;
            }
            
            ev.key.keycode = keycode;
            
            int mods = 0;
            if (xev->xkey.state & ShiftMask)   mods |= GW_MOD_SHIFT;
            if (xev->xkey.state & ControlMask) mods |= GW_MOD_CTRL;
            if (xev->xkey.state & Mod1Mask)    mods |= GW_MOD_ALT;
            ev.key.mod = mods;
            
            push_event(ev);
            
            // Handle Character Input
            // The character comes from the keysym, not from mbtowc (which
            // depends on the C locale): Latin-1 keysyms are their codepoint,
            // 0x01xxxxxx keysyms carry a Unicode codepoint; anything else
            // falls back to the ASCII XLookupString gave. Control characters
            // (Ctrl+letter, Enter, Tab, Backspace) are keys, not text.
            // No input method (XIM): dead keys and IME composition are not
            // handled yet.
            if (xev->type == KeyPress && !(xev->xkey.state & ControlMask)) {
                unsigned cp = 0;
                if (keysym >= 0x20 && keysym <= 0xFF) cp = (unsigned)keysym;
                else if ((keysym & 0xFF000000) == 0x01000000) cp = (unsigned)(keysym & 0x00FFFFFF);
                else if (len == 1 && (unsigned char)text_buf[0] >= 0x20 && (unsigned char)text_buf[0] < 0x7F) cp = (unsigned char)text_buf[0];
                if (cp >= 0x20 && cp != 0x7F && cp <= 0x10FFFF) {
                    GW_Event text_ev;
                    memset(&text_ev, 0, sizeof(text_ev));
                    text_ev.type = GW_EVENT_TEXT_INPUT;
                    text_ev.text.character = (wchar_t)cp;
                    push_event(text_ev);
                }
            }
            break;
        }
    }
}

// Global active window pointer for resize events
GW_Window* g_active_window = NULL;

static int ensure_user_pipe(void); // extensions, below

GW_Window* GW_CreateWindow(const char* title, int width, int height) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "LibGW Error: Failed to open X11 Display.\n");
        return NULL;
    }
    
    int ds = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(
        dpy,
        RootWindow(dpy, ds),
        0, 0, width, height, 1,
        BlackPixel(dpy, ds),
        WhitePixel(dpy, ds)
    );
    
    XStoreName(dpy, win, title);
    
    XSelectInput(
        dpy, win,
        ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | StructureNotifyMask | FocusChangeMask
    );
    
    WM_DELETE_WINDOW = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &WM_DELETE_WINDOW, 1);
    
    GW_Window* gwin = (GW_Window*)malloc(sizeof(GW_Window));
    if (!gwin) {
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return NULL;
    }
    memset(gwin, 0, sizeof(GW_Window));
    gwin->dpy = (void*)dpy;
    gwin->win = (unsigned long)win;
    gwin->width = width;
    gwin->height = height;
    gwin->gc = (void*)XCreateGC(dpy, win, 0, NULL);
    
    gwin->backbuffer = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    gwin->ximage = XCreateImage(
        dpy,
        DefaultVisual(dpy, ds),
        DefaultDepth(dpy, ds),
        ZPixmap,
        0,
        (char*)gwin->backbuffer,
        width,
        height,
        32,
        0
    );
    
    global_dpy = dpy;
    global_win = win;
    g_active_window = gwin;
    // Extensions: GW_PostUserEvent may be called from another thread right
    // after the window exists, before the first wait.
    ensure_user_pipe();
    
    return gwin;
}

void GW_DestroyWindow(GW_Window* win) {
    if (!win) return;
    Display* dpy = (Display*)win->dpy;
    Window w = (Window)win->win;
    GC gc = (GC)win->gc;
    
    if (win->ximage) {
        XDestroyImage((XImage*)win->ximage);
    }
    
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    
    if (g_active_window == win) {
        g_active_window = NULL;
    }
    free(win);
}

void GW_ShowWindow(GW_Window* win, int show) {
    if (!win) return;
    Display* dpy = (Display*)win->dpy;
    Window w = (Window)win->win;
    if (show) {
        XMapWindow(dpy, w);
    } else {
        XUnmapWindow(dpy, w);
    }
    XFlush(dpy);
}

void GW_GetWindowSize(GW_Window* win, int* width, int* height) {
    if (!win) return;
    if (width) *width = win->width;
    if (height) *height = win->height;
}

void GW_SetWindowTitle(GW_Window* win, const char* title) {
    if (!win) return;
    Display* dpy = (Display*)win->dpy;
    Window w = (Window)win->win;
    XStoreName(dpy, w, title);
    XFlush(dpy);
}

/* ---- Extensions (feat/portmaster-ui): user events, timers, timeouts -------
 * GW_PostUserEvent writes a small record to a self-pipe; writes up to
 * PIPE_BUF bytes are atomic, so any thread may post. The event loop waits
 * with select() on the X connection and the pipe, with a timeout from the
 * next timer and the caller's deadline. */

typedef struct {
    GW_Window* win;
    int code;
    void* data;
} UserMsg;

static int user_pipe[2] = { -1, -1 };

static int ensure_user_pipe(void) {
    if (user_pipe[0] >= 0) return 1;
    if (pipe(user_pipe) != 0) return 0;
    fcntl(user_pipe[0], F_SETFL, fcntl(user_pipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(user_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(user_pipe[1], F_SETFD, FD_CLOEXEC);
    return 1;
}

int GW_PostUserEvent(GW_Window* win, int code, void* data) {
    if (!win || user_pipe[1] < 0) return 0;
    UserMsg m;
    m.win = win;
    m.code = code;
    m.data = data;
    ssize_t n;
    do { n = write(user_pipe[1], &m, sizeof(m)); } while (n < 0 && errno == EINTR);
    return n == (ssize_t)sizeof(m);
}

static void drain_user_pipe(void) {
    if (user_pipe[0] < 0) return;
    UserMsg m;
    while (read(user_pipe[0], &m, sizeof(m)) == (ssize_t)sizeof(m)) {
        GW_Event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = GW_EVENT_USER;
        ev.user.code = m.code;
        ev.user.data = m.data;
        push_event(ev);
    }
}

uint64_t GW_GetTicks(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

#define GW_MAX_TIMERS 32
static struct { int used; int id; int interval; uint64_t due; } timers[GW_MAX_TIMERS];

int GW_SetTimer(GW_Window* win, int id, int interval_ms) {
    if (!win || interval_ms <= 0) return 0;
    int free_slot = -1;
    for (int i = 0; i < GW_MAX_TIMERS; i++) {
        if (timers[i].used && timers[i].id == id) { free_slot = i; break; }
        if (!timers[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return 0;
    timers[free_slot].used = 1;
    timers[free_slot].id = id;
    timers[free_slot].interval = interval_ms;
    timers[free_slot].due = GW_GetTicks() + (uint64_t)interval_ms;
    return 1;
}

void GW_KillTimer(GW_Window* win, int id) {
    (void)win;
    for (int i = 0; i < GW_MAX_TIMERS; i++) {
        if (timers[i].used && timers[i].id == id) timers[i].used = 0;
    }
}

// Pushes due timers; returns ms until the next one (-1 = none).
static int fire_timers(void) {
    uint64_t now = GW_GetTicks();
    int64_t next = -1;
    for (int i = 0; i < GW_MAX_TIMERS; i++) {
        if (!timers[i].used) continue;
        if (timers[i].due <= now) {
            GW_Event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = GW_EVENT_TIMER;
            ev.timer.id = timers[i].id;
            push_event(ev);
            // Like WM_TIMER: missed ticks coalesce into one.
            timers[i].due = now + (uint64_t)timers[i].interval;
        }
        int64_t left = (int64_t)(timers[i].due - now);
        if (next < 0 || left < next) next = left;
    }
    return (int)next;
}

static void pump_x(void) {
    XEvent xev;
    while (XPending(global_dpy) > 0) {
        XNextEvent(global_dpy, &xev);
        process_xevent(&xev);
    }
}

int GW_WaitEventTimeout(GW_Event* event, int timeout_ms) {
    if (!event) return 0;
    if (pop_event(event)) return 1;
    if (!global_dpy) return 0;
    ensure_user_pipe();
    uint64_t deadline = timeout_ms >= 0 ? GW_GetTicks() + (uint64_t)timeout_ms : 0;
    for (;;) {
        pump_x();
        drain_user_pipe();
        int next_timer = fire_timers();
        if (pop_event(event)) return 1;

        int wait = -1;
        if (timeout_ms >= 0) {
            uint64_t now = GW_GetTicks();
            if (now >= deadline) break;
            wait = (int)(deadline - now);
        }
        if (next_timer >= 0 && (wait < 0 || next_timer < wait)) wait = next_timer;

        XFlush(global_dpy);
        int xfd = ConnectionNumber(global_dpy);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        FD_SET(user_pipe[0], &fds);
        int maxfd = xfd > user_pipe[0] ? xfd : user_pipe[0];
        struct timeval tv, *tvp = NULL;
        if (wait >= 0) {
            tv.tv_sec = wait / 1000;
            tv.tv_usec = (wait % 1000) * 1000;
            tvp = &tv;
        }
        if (select(maxfd + 1, &fds, NULL, NULL, tvp) < 0 && errno != EINTR) break;
    }
    memset(event, 0, sizeof(*event));
    event->type = GW_EVENT_NONE;
    return 0;
}

int GW_WaitEvent(GW_Event* event) {
    if (!event) return 0;
    if (pop_event(event)) return 1;
    if (!global_dpy) return 0;
    // Same loop as WaitEventTimeout, without a deadline.
    return GW_WaitEventTimeout(event, -1);
}

int GW_PollEvent(GW_Event* event) {
    if (!event) return 0;

    if (pop_event(event)) {
        return 1;
    }

    if (!global_dpy) return 0;

    pump_x();
    drain_user_pipe();
    fire_timers();
    return pop_event(event);
}

uint32_t* GW_GetBackbuffer(GW_Window* win, int* width, int* height) {
    if (!win) return NULL;
    if (width) *width = win->width;
    if (height) *height = win->height;
    return win->backbuffer;
}

void GW_Present(GW_Window* win) {
    if (!win || !win->dpy || !win->win || !win->gc || !win->ximage) return;
    Display* dpy = (Display*)win->dpy;
    Window w = (Window)win->win;
    GC gc = (GC)win->gc;
    XImage* ximage = (XImage*)win->ximage;
    
    XPutImage(dpy, w, gc, ximage, 0, 0, 0, 0, win->width, win->height);
    XFlush(dpy);
}

#ifdef GW_X11_EXTERNAL_DIALOGS

int GW_ShowMessageBox(GW_Window* parent, const char* title, const wchar_t* text, const wchar_t** buttons, int num_buttons) {
    // Map to MessageBox-X11 buttons
    X11_Button* x11_buttons = (X11_Button*)malloc(num_buttons * sizeof(X11_Button));
    if (!x11_buttons) return -1;
    
    for (int i = 0; i < num_buttons; i++) {
        x11_buttons[i].label = (wchar_t*)buttons[i];
        x11_buttons[i].result = i;
    }
    
    int res = Messagebox(title, text, x11_buttons, num_buttons);
    free(x11_buttons);
    return res;
}

char* GW_ShowOpenFileDialog(GW_Window* parent, const char* title, const char* filter) {
    if (!parent) return NULL;
    Display* dpy = (Display*)parent->dpy;
    Window parent_win = (Window)parent->win;
    
    x_fib_configure(1, title ? title : "Abrir archivo");
    x_fib_show(dpy, parent_win, 0, 0);
    
    XEvent xev;
    while (x_fib_status() == 0) {
        XNextEvent(dpy, &xev);
        x_fib_handle_events(dpy, &xev);
    }
    
    if (x_fib_status() > 0) {
        return x_fib_filename(); // Caller must free this
    }
    return NULL;
}

char* GW_ShowSaveFileDialog(GW_Window* parent, const char* title, const char* filter) {
    // simple open dialog can be used for saving too on basic X11, or configuring sofd for save
    if (!parent) return NULL;
    Display* dpy = (Display*)parent->dpy;
    Window parent_win = (Window)parent->win;
    
    x_fib_configure(1, title ? title : "Guardar archivo");
    x_fib_show(dpy, parent_win, 0, 0);
    
    XEvent xev;
    while (x_fib_status() == 0) {
        XNextEvent(dpy, &xev);
        x_fib_handle_events(dpy, &xev);
    }
    
    if (x_fib_status() > 0) {
        return x_fib_filename(); // Caller must free
    }
    return NULL;
}

#else // !GW_X11_EXTERNAL_DIALOGS

int GW_ShowMessageBox(GW_Window* parent, const char* title, const wchar_t* text, const wchar_t** buttons, int num_buttons) {
    (void)parent; (void)buttons; (void)num_buttons;
    fprintf(stderr, "[%s] %ls\n(no X11 message box built in: NATIVEGUI_X11_DIALOGS is off)\n", title ? title : "", text ? text : L"");
    return -1;
}

char* GW_ShowOpenFileDialog(GW_Window* parent, const char* title, const char* filter) {
    (void)parent; (void)title; (void)filter;
    fprintf(stderr, "GW_ShowOpenFileDialog: no X11 file dialog built in (NATIVEGUI_X11_DIALOGS is off)\n");
    return NULL;
}

char* GW_ShowSaveFileDialog(GW_Window* parent, const char* title, const char* filter) {
    (void)parent; (void)title; (void)filter;
    fprintf(stderr, "GW_ShowSaveFileDialog: no X11 file dialog built in (NATIVEGUI_X11_DIALOGS is off)\n");
    return NULL;
}

#endif // GW_X11_EXTERNAL_DIALOGS

// UTF-8 <-> wchar_t (UTF-32 here), independent of the C locale (mbstowcs
// needs setlocale and fails on non-ASCII in the default "C" locale). Like
// the Win32 versions: the result is NUL-terminated and the return value
// counts the terminator; invalid UTF-8 bytes become U+FFFD.
int GW_UTF8ToWide(const char* utf8, wchar_t* wide, int max_wide_chars) {
    if (!utf8 || !wide || max_wide_chars <= 0) return 0;
    const unsigned char* s = (const unsigned char*)utf8;
    int n = 0;
    while (*s && n < max_wide_chars - 1) {
        unsigned c = *s, cp, need;
        if (c < 0x80)      { cp = c; need = 0; }
        else if (c >= 0xC2 && c < 0xE0) { cp = c & 0x1F; need = 1; }
        else if (c >= 0xE0 && c < 0xF0) { cp = c & 0x0F; need = 2; }
        else if (c >= 0xF0 && c < 0xF5) { cp = c & 0x07; need = 3; }
        else { wide[n++] = 0xFFFD; s++; continue; }
        s++;
        unsigned i = 0;
        for (; i < need && (*s & 0xC0) == 0x80; i++, s++) cp = (cp << 6) | (*s & 0x3F);
        if (i < need || (need == 2 && cp < 0x800) || (need == 3 && (cp < 0x10000 || cp > 0x10FFFF)) ||
            (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        wide[n++] = (wchar_t)cp;
    }
    wide[n] = 0;
    return n + 1;
}

int GW_WideToUTF8(const wchar_t* wide, char* utf8, int max_utf8_bytes) {
    if (!wide || !utf8 || max_utf8_bytes <= 0) return 0;
    int n = 0;
    for (; *wide; wide++) {
        unsigned cp = (unsigned)*wide;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        int len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (n + len > max_utf8_bytes - 1) break;
        if (len == 1) utf8[n++] = (char)cp;
        else if (len == 2) { utf8[n++] = (char)(0xC0 | (cp >> 6)); utf8[n++] = (char)(0x80 | (cp & 0x3F)); }
        else if (len == 3) { utf8[n++] = (char)(0xE0 | (cp >> 12)); utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (cp & 0x3F)); }
        else { utf8[n++] = (char)(0xF0 | (cp >> 18)); utf8[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (cp & 0x3F)); }
    }
    utf8[n] = 0;
    return n + 1;
}

// X11/Linux implementation of GW_Process
struct GW_Process {
    int pid;
    int pty_fd;
};

char* GW_SearchPath(const char* filename) {
    if (strchr(filename, '/')) {
        if (access(filename, X_OK) == 0) {
            return strdup(filename);
        }
        return NULL;
    }
    
    char* path_env = getenv("PATH");
    if (!path_env) return NULL;
    
    char* path_copy = strdup(path_env);
    char* dir = strtok(path_copy, ":");
    char full_path[512];
    
    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return strdup(full_path);
        }
        dir = strtok(NULL, ":");
    }
    
    free(path_copy);
    return NULL;
}

GW_Process* GW_SpawnProcess(const char* shell_command) {
    char* full_path = GW_SearchPath(shell_command);
    if (!full_path) return NULL;
    
    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
    
    if (pid < 0) {
        free(full_path);
        return NULL;
    }
    
    if (pid == 0) {
        char* args[] = { full_path, NULL };
        execvp(full_path, args);
        exit(127);
    }
    
    free(full_path);
    
    // Set non-blocking read
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    
    GW_Process* proc = (GW_Process*)malloc(sizeof(GW_Process));
    if (!proc) {
        close(master_fd);
        kill(pid, SIGKILL);
        return NULL;
    }
    proc->pid = pid;
    proc->pty_fd = master_fd;
    
    return proc;
}

int GW_ReadProcess(GW_Process* proc, char* buffer, int max_len) {
    if (!proc || proc->pty_fd < 0) return -1;
    
    ssize_t bytes_read = read(proc->pty_fd, buffer, max_len);
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return (int)bytes_read;
}

int GW_WriteProcess(GW_Process* proc, const char* buffer, int len) {
    if (!proc || proc->pty_fd < 0) return -1;
    
    ssize_t bytes_written = write(proc->pty_fd, buffer, len);
    if (bytes_written < 0) return -1;
    
    return (int)bytes_written;
}

void GW_CloseProcess(GW_Process* proc) {
    if (!proc) return;
    if (proc->pty_fd >= 0) close(proc->pty_fd);
    if (proc->pid > 0) {
        kill(proc->pid, SIGKILL);
        int status;
        waitpid(proc->pid, &status, WNOHANG);
    }
    free(proc);
}

#endif // _WIN32
