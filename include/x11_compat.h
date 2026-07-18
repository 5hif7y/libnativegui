#ifndef X11_COMPAT_H
#define X11_COMPAT_H

#include <windows.h>
#define EndMenu wagner_EndMenu
#define DestroyMenu wagner_DestroyMenu
#include "gw.h"
#include "gw_internal.h"

// Basic X11 Types mapped to LibGW
typedef void* Display;
typedef GW_Window* Window;
typedef GW_Window* Drawable;
typedef void* GC;
typedef void* Colormap;
typedef void* Visual;

typedef struct {
    GW_Font* gw_font;
    int ascent;
    int descent;
} XFontStruct;

typedef struct {
    short x, y;
} XPoint;

typedef struct {
    unsigned long pixel;
    unsigned short red, green, blue;
    char flags;
    char pad;
} XColor;

typedef struct {
    int width;
    int height;
} XWindowAttributes;

// X11 Event Abstraction
typedef struct {
    int type;
    union {
        struct {
            Window window;
        } xexpose;
        struct {
            Window window;
            int width;
            int height;
        } xconfigure;
        struct {
            Window window;
            int x;
            int y;
            int button;
        } xbutton;
        struct {
            Window window;
            int keycode;
            int state;
        } xkey;
        struct {
            Window window;
        } xmapping;
    };
} XEvent;

typedef unsigned long KeySym;

// Event Mask constants (Dummies for LibGW compatibility)
#define ExposureMask 0
#define StructureNotifyMask 0
#define KeyPressMask 0
#define KeyReleaseMask 0
#define ButtonPressMask 0
#define ButtonReleaseMask 0
#define Nonconvex 0
#define CoordModeOrigin 0
#define QueuedAfterReading 0
#define VisualNoMask 0
#define DoRed 1
#define DoGreen 2
#define DoBlue 4

// Event Types
#define Expose 1
#define ConfigureNotify 2
#define ButtonPress 3
#define KeyPress 4
#define KeyRelease 5
#define MappingNotify 6
#define ButtonRelease 7

// Keysyms
#define XK_Shift_L 0xFFE1
#define XK_Shift_R 0xFFE2
#define XK_Control_L 0xFFE3
#define XK_Control_R 0xFFE4
#define XK_BackSpace 0x08
#define XK_Delete 0x7F
#define XK_Left 0x25
#define XK_Right 0x27
#define XK_Escape 0x1B
#define XK_Cancel 0x03
#define XK_Linefeed 0x0A
#define XK_Return 0x0D

static inline int XQueryPointer(Display *myd, Window w, Window *myroot, Window *mychild, int *rx, int *ry, int *wx, int *wy, unsigned int *kb) {
    POINT pt;
    GetCursorPos(&pt);
    *rx = pt.x;
    *ry = pt.y;
    if (w && ((uintptr_t)w > 0x10000)) {
        ScreenToClient((HWND)w->hwnd, &pt);
        *wx = pt.x;
        *wy = pt.y;
    } else {
        *wx = pt.x;
        *wy = pt.y;
    }
    *kb = 0;
    return 1;
}

static inline int XTextWidth(XFontStruct* font, const char* str, int len) {
    if (!str || len <= 0) return 0;
    char temp[512] = {0};
    if (len >= 512) len = 511;
    strncpy(temp, str, len);
    wchar_t wtext[512] = {0};
    GW_UTF8ToWide(temp, wtext, 512);
    int w = 0, h = 0;
    GW_MeasureText(font ? font->gw_font : NULL, wtext, &w, &h);
    return w;
}

static inline void XFlush(Display* d) {}

#endif // X11_COMPAT_H
