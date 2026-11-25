#ifndef GW_INTERNAL_H
#define GW_INTERNAL_H

#include "gw.h"

// Define GW_Window internally to be shared by backends
struct GW_Window {
    uint32_t* backbuffer;
    int width;
    int height;

#ifdef _WIN32
    void* hwnd; // HWND
    void* hdc;  // HDC
    void* hbitmap; // HBITMAP for double buffering/blitting
    void* old_bitmap;
    void* mem_dc;
    void* icon_big;   // HICON set by GW_SetWindowIcon (owned)
    void* icon_small;
    void* cursor;     // HCURSOR shown over the client area (shared, not owned)
#else
    void* dpy;      // Display*
    unsigned long win; // Window
    void* gc;       // GC
    void* ximage;   // XImage*
    void* xic;      // XIC of the input method (NULL without one)
#endif

    // Extensions (feat/portmaster-ui)
    int clip_set;                  // 0 = whole window
    int clip_x, clip_y, clip_w, clip_h;
    int wheel_mode;                // GW_WHEEL_AS_KEYS / GW_WHEEL_EVENTS
    int min_w, min_h;              // GW_SetMinWindowSize (client size)
};

#ifndef _WIN32
// X11: queues an event (ev.window must be set), e.g. from a modal loop.
void gw_x11_push_event(GW_Event ev);
// X11: extension hooks get every X event first (an XEvent*); returning 1
// consumes it. Used by the clipboard and the tray icon (gw_x11_ext.c).
typedef int (*GW_X11_EventHook)(void* xevent);
void gw_x11_add_event_hook(GW_X11_EventHook hook);
#endif

#ifdef _WIN32
// Private window messages of the Win32 backend (WM_APP + n).
#define GW_WM_USER_EVENT (0x8000 + 1)
#define GW_WM_TRAY       (0x8000 + 2)
#endif

// Effective drawing bounds [x1, x2) x [y1, y2): the window, intersected
// with the clip rectangle when one is set. Returns 0 if empty.
static inline int gw_draw_bounds(const GW_Window* win, int* x1, int* y1, int* x2, int* y2) {
    int bx1 = 0, by1 = 0, bx2 = win->width, by2 = win->height;
    if (win->clip_set) {
        if (win->clip_x > bx1) bx1 = win->clip_x;
        if (win->clip_y > by1) by1 = win->clip_y;
        if (win->clip_x + win->clip_w < bx2) bx2 = win->clip_x + win->clip_w;
        if (win->clip_y + win->clip_h < by2) by2 = win->clip_y + win->clip_h;
    }
    *x1 = bx1; *y1 = by1; *x2 = bx2; *y2 = by2;
    return bx1 < bx2 && by1 < by2;
}

// Blends src (0xAARRGGBB, alpha scaled by cov/255) over *dst; the result
// is opaque (the backbuffer has no meaningful alpha).
static inline void gw_blend_pixel(uint32_t* dst, uint32_t src, unsigned cov) {
    unsigned a = ((src >> 24) & 0xFF) * cov / 255;
    if (a == 0) return;
    if (a >= 255) { *dst = 0xFF000000u | (src & 0x00FFFFFFu); return; }
    uint32_t d = *dst;
    unsigned ia = 255 - a;
    unsigned r = (((src >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia) / 255;
    unsigned g = (((src >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia) / 255;
    unsigned b = ((src & 0xFF) * a + (d & 0xFF) * ia) / 255;
    *dst = 0xFF000000u | (r << 16) | (g << 8) | b;
}

#endif // GW_INTERNAL_H
