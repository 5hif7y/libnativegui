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
#else
    void* dpy;      // Display*
    unsigned long win; // Window
    void* gc;       // GC
    void* ximage;   // XImage*
#endif
};

#endif // GW_INTERNAL_H
