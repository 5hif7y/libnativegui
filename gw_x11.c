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

// X11 Dialog wrappers (MessageBox-X11 & sofd headers)
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

// Event queue for X11
#define EVENT_QUEUE_SIZE 256
static GW_Event event_queue[EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static Atom WM_DELETE_WINDOW;

static void push_event(GW_Event ev) {
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
            GW_Window* win = NULL;
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
            ev.type = (xev->type == ButtonPress) ? GW_EVENT_MOUSE_BUTTON_DOWN : GW_EVENT_MOUSE_BUTTON_UP;
            ev.mouse_button.x = xev->xbutton.x;
            ev.mouse_button.y = xev->xbutton.y;
            ev.mouse_button.button = xev->xbutton.button; // 1=left, 2=middle, 3=right
            push_event(ev);
            break;
        }
        
        case KeyPress:
        case KeyRelease: {
            KeySym keysym;
            char text_buf[8] = {0};
            int len = XLookupString(&xev->xkey, text_buf, sizeof(text_buf), &keysym, NULL);
            
            GW_Event ev;
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
            if (xev->type == KeyPress && len > 0) {
                GW_Event text_ev;
                text_ev.type = GW_EVENT_TEXT_INPUT;
                wchar_t wch = 0;
                int res = mbtowc(&wch, text_buf, len);
                if (res > 0) {
                    text_ev.text.character = wch;
                    push_event(text_ev);
                }
            }
            break;
        }
    }
}

// Global active window pointer for resize events
GW_Window* g_active_window = NULL;

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
        PointerMotionMask | StructureNotifyMask
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

int GW_WaitEvent(GW_Event* event) {
    if (!event) return 0;
    
    if (pop_event(event)) {
        return 1;
    }
    
    if (!global_dpy) return 0;
    
    XEvent xev;
    while (1) {
        XNextEvent(global_dpy, &xev);
        process_xevent(&xev);
        if (pop_event(event)) {
            return 1;
        }
    }
    return 0;
}

int GW_PollEvent(GW_Event* event) {
    if (!event) return 0;
    
    if (pop_event(event)) {
        return 1;
    }
    
    if (!global_dpy) return 0;
    
    XEvent xev;
    while (XPending(global_dpy) > 0) {
        XNextEvent(global_dpy, &xev);
        process_xevent(&xev);
        if (pop_event(event)) {
            return 1;
        }
    }
    
    return 0;
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

int GW_UTF8ToWide(const char* utf8, wchar_t* wide, int max_wide_chars) {
    return (int)mbstowcs(wide, utf8, max_wide_chars);
}

int GW_WideToUTF8(const wchar_t* wide, char* utf8, int max_utf8_bytes) {
    return (int)wcstombs(utf8, wide, max_utf8_bytes);
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
