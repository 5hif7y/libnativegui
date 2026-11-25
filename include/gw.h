#ifndef GW_H
#define GW_H

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

// Keycodes for special keys
#define GW_KEY_BACKSPACE 0x08
#define GW_KEY_TAB       0x09
#define GW_KEY_ENTER     0x0D
#define GW_KEY_ESCAPE    0x1B
#define GW_KEY_LEFT      0x25
#define GW_KEY_UP        0x26
#define GW_KEY_RIGHT     0x27
#define GW_KEY_DOWN      0x28
#define GW_KEY_DELETE    0x2E
#define GW_KEY_HOME      0x24
#define GW_KEY_END       0x23
#define GW_KEY_PAGEUP    0x21
#define GW_KEY_PAGEDOWN  0x22

// Modifiers
#define GW_MOD_SHIFT     (1 << 0)
#define GW_MOD_CTRL      (1 << 1)
#define GW_MOD_ALT       (1 << 2)

typedef struct GW_Window GW_Window;

// Event Types
typedef enum {
    GW_EVENT_NONE = 0,
    GW_EVENT_QUIT,
    GW_EVENT_WINDOW_RESIZE,
    GW_EVENT_WINDOW_EXPOSE,
    GW_EVENT_KEY_DOWN,
    GW_EVENT_KEY_UP,
    GW_EVENT_TEXT_INPUT,
    GW_EVENT_MOUSE_BUTTON_DOWN,
    GW_EVENT_MOUSE_BUTTON_UP,
    GW_EVENT_MOUSE_MOTION,
    // Extensions (appended so the existing values keep their numbers)
    GW_EVENT_MOUSE_WHEEL,   // only with GW_SetWheelMode(win, GW_WHEEL_EVENTS)
    GW_EVENT_USER,          // GW_PostUserEvent (from any thread)
    GW_EVENT_TIMER,         // GW_SetTimer
    GW_EVENT_FOCUS_GAINED,
    GW_EVENT_FOCUS_LOST,
    GW_EVENT_DPI_CHANGED,   // after GW_EnableHighDpi; window already resized
    GW_EVENT_TRAY           // GW_CreateTrayIcon
} GW_EventType;

// Tray icon actions (GW_Event.tray.action)
#define GW_TRAY_CLICK        1
#define GW_TRAY_RIGHT_CLICK  2
#define GW_TRAY_DOUBLE_CLICK 3

// Event Structure
typedef struct {
    GW_EventType type;
    GW_Window* window;
    union {
        struct {
            int width;
            int height;
        } resize;
        struct {
            int keycode;
            int mod;
        } key;
        struct {
            wchar_t character;
        } text;
        struct {
            int x;
            int y;
            int button; // 1 = Left, 2 = Middle, 3 = Right
            int mod;    // GW_MOD_* held during the click (extension)
            int clicks; // 1, or 2 for a double click (extension)
        } mouse_button;
        struct {
            int x;
            int y;
        } mouse_motion;
        // Extensions
        struct {
            int x;      // pointer position, client coordinates
            int y;
            int delta;  // 120 per notch; > 0 = away from the user (scroll up)
            int mod;
            int horizontal; // 1 for a horizontal wheel / tilt
        } wheel;
        struct {
            int code;
            void* data;
        } user;
        struct {
            int id;
        } timer;
        struct {
            float scale; // new scale, 1.0 = 96 DPI
        } dpi;
        struct {
            int action; // GW_TRAY_*
            int id;     // the id given to GW_CreateTrayIcon
        } tray;
    };
} GW_Event;

// Window Lifecycle
GW_Window* GW_CreateWindow(const char* title, int width, int height);
void GW_DestroyWindow(GW_Window* win);
void GW_ShowWindow(GW_Window* win, int show);
void GW_GetWindowSize(GW_Window* win, int* width, int* height);
void GW_SetWindowTitle(GW_Window* win, const char* title);

// Event Handling
int GW_WaitEvent(GW_Event* event);
int GW_PollEvent(GW_Event* event);

// Drawing Interface
uint32_t* GW_GetBackbuffer(GW_Window* win, int* width, int* height);
void GW_Present(GW_Window* win);

// Drawing utilities
void GW_Clear(GW_Window* win, uint32_t color);
void GW_DrawPixel(GW_Window* win, int x, int y, uint32_t color);
void GW_DrawLine(GW_Window* win, int x1, int y1, int x2, int y2, uint32_t color);
void GW_DrawRect(GW_Window* win, int x, int y, int w, int h, uint32_t color);
void GW_FillRect(GW_Window* win, int x, int y, int w, int h, uint32_t color);

// Font rendering
typedef struct GW_Font GW_Font;
GW_Font* GW_LoadFont(const char* font_path, float font_size);
void GW_FreeFont(GW_Font* font);
void GW_DrawText(GW_Window* win, GW_Font* font, int x, int y, const wchar_t* text, uint32_t color);
void GW_MeasureText(GW_Font* font, const wchar_t* text, int* width, int* height);

// Dialogs
int GW_ShowMessageBox(GW_Window* parent, const char* title, const wchar_t* text, const wchar_t** buttons, int num_buttons);
char* GW_ShowOpenFileDialog(GW_Window* parent, const char* title, const char* filter);
char* GW_ShowSaveFileDialog(GW_Window* parent, const char* title, const char* filter);

// Character Conversion Helpers
int GW_UTF8ToWide(const char* utf8, wchar_t* wide, int max_wide_chars);
int GW_WideToUTF8(const wchar_t* wide, char* utf8, int max_utf8_bytes);

// Process Spawning & I/O
typedef struct GW_Process GW_Process;
GW_Process* GW_SpawnProcess(const char* shell_command);
int GW_ReadProcess(GW_Process* proc, char* buffer, int max_len);
int GW_WriteProcess(GW_Process* proc, const char* buffer, int len);
void GW_CloseProcess(GW_Process* proc);

// File search helper
char* GW_SearchPath(const char* filename);

// Image Support
typedef struct GW_Image GW_Image;

struct GW_Image {
    uint32_t* pixels;      // RGBA8888 pixels (or frame array)
    int w;                 // Width
    int h;                 // Height
    int frame_count;       // GIF frame count (1 for static)
    int* frame_delays;     // GIF frame delays (ms)
    int current_frame;     // GIF current frame
    uint32_t** frames;     // Array of frame pixel buffers for animated GIFs
};

GW_Image* GW_LoadImage(const char* filepath);
void GW_FreeImage(GW_Image* img);
void GW_DrawImage(GW_Window* win, GW_Image* img, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int rotation, int flip);

/* ------------------------------------------------------------------------
 * Extensions (branch feat/portmaster-ui). Everything below is additive: the
 * existing API and event stream are unchanged unless a program opts in.
 * Colors are 0xAARRGGBB; the plain drawing calls above ignore the alpha
 * byte, the *Blend / rounded calls below honour it (0xFF = opaque).
 * ---------------------------------------------------------------------- */

// Memory returned by the library (clipboard text): free it with GW_Free so
// the allocator always matches (callers from other languages cannot rely
// on sharing the C runtime).
void GW_Free(void* ptr);

// Images from memory (PNG/JPG/BMP/GIF via stb_image, or SVG text) and from
// raw pixels (0xAARRGGBB, copied). Free with GW_FreeImage.
GW_Image* GW_LoadImageFromMemory(const unsigned char* data, int len);
GW_Image* GW_LoadSVGFromMemory(const char* svg_text, int len, int width, int height);
GW_Image* GW_CreateImage(int w, int h, const uint32_t* pixels);

// Fonts from memory (the buffer is copied).
GW_Font* GW_LoadFontFromMemory(const unsigned char* data, int len, float font_size);
// Vertical metrics in pixels: ascent (> 0), descent (< 0), line height.
void GW_GetFontMetrics(GW_Font* font, int* ascent, int* descent, int* line_height);
// UTF-8 variants of DrawText / MeasureText.
void GW_DrawTextUTF8(GW_Window* win, GW_Font* font, int x, int y, const char* text, uint32_t color);
void GW_MeasureTextUTF8(GW_Font* font, const char* text, int* width, int* height);

// Mouse wheel. GW_WHEEL_AS_KEYS (default, legacy) turns each notch into a
// '+' / '-' KEY_DOWN, as Wagner's plots expect; GW_WHEEL_EVENTS delivers
// GW_EVENT_MOUSE_WHEEL instead.
#define GW_WHEEL_AS_KEYS 0
#define GW_WHEEL_EVENTS  1
void GW_SetWheelMode(GW_Window* win, int mode);

// Thread-safe: queues a GW_EVENT_USER for win and wakes GW_WaitEvent.
// Returns 1 if queued.
int GW_PostUserEvent(GW_Window* win, int code, void* data);

// Like GW_WaitEvent, but gives up after timeout_ms (< 0 = forever). Returns
// 1 with an event, 0 on timeout (event->type = GW_EVENT_NONE) and on quit.
int GW_WaitEventTimeout(GW_Event* event, int timeout_ms);

// Repeating timer: a GW_EVENT_TIMER with this id every interval_ms (an id
// already in use is restarted). Returns 1 on success.
int GW_SetTimer(GW_Window* win, int id, int interval_ms);
void GW_KillTimer(GW_Window* win, int id);

// Clip rectangle for every drawing call except GW_Clear (which always
// fills the whole backbuffer). It is intersected with the window at draw
// time, so it survives resizes.
void GW_SetClipRect(GW_Window* win, int x, int y, int w, int h);
void GW_ResetClipRect(GW_Window* win);
void GW_GetClipRect(GW_Window* win, int* x, int* y, int* w, int* h);

// Alpha-aware shapes (color = 0xAARRGGBB).
void GW_FillRectBlend(GW_Window* win, int x, int y, int w, int h, uint32_t color);
void GW_FillRoundRect(GW_Window* win, int x, int y, int w, int h, int radius, uint32_t color);
void GW_DrawRoundRect(GW_Window* win, int x, int y, int w, int h, int radius, int thickness, uint32_t color);
void GW_FillCircle(GW_Window* win, int cx, int cy, int radius, uint32_t color);
void GW_DrawLineAA(GW_Window* win, float x1, float y1, float x2, float y2, float thickness, uint32_t color);
// Draws an image tinted: every pixel's RGB is replaced by tint's RGB and
// its alpha multiplied by tint's alpha (monochrome icons in any color).
void GW_DrawImageTinted(GW_Window* win, GW_Image* img, int dx, int dy, int dw, int dh, uint32_t tint);

// Clipboard (UTF-8). GW_GetClipboardText returns NULL if empty; free with GW_Free.
int GW_SetClipboardText(GW_Window* win, const char* utf8);
char* GW_GetClipboardText(GW_Window* win);

// Mouse cursor shown over the client area.
typedef enum {
    GW_CURSOR_ARROW = 0,
    GW_CURSOR_HAND,
    GW_CURSOR_IBEAM,
    GW_CURSOR_SIZE_WE,
    GW_CURSOR_SIZE_NS,
    GW_CURSOR_WAIT,
    GW_CURSOR_CROSSHAIR
} GW_Cursor;
void GW_SetCursor(GW_Window* win, GW_Cursor cursor);

// High DPI. Call GW_EnableHighDpi once, before creating windows, to get a
// real-pixel backbuffer on scaled displays (otherwise the OS bitmap-scales
// the window). GW_GetDpiScale is 1.0 at 96 DPI.
void GW_EnableHighDpi(void);
float GW_GetDpiScale(GW_Window* win);

// Window extras.
void GW_SetWindowIcon(GW_Window* win, GW_Image* icon);
void GW_SetMinWindowSize(GW_Window* win, int min_w, int min_h);
void GW_FocusWindow(GW_Window* win);   // restore if minimised, bring to front
int  GW_IsWindowVisible(GW_Window* win);
// Requests a GW_EVENT_WINDOW_EXPOSE soon (e.g. after state changed).
void GW_RequestRedraw(GW_Window* win);
// Milliseconds from an arbitrary monotonic origin.
uint64_t GW_GetTicks(void);

// System tray / notification-area icon owned by win. Clicks arrive as
// GW_EVENT_TRAY with this id. Returns NULL where unsupported (X11 for now).
typedef struct GW_TrayIcon GW_TrayIcon;
GW_TrayIcon* GW_CreateTrayIcon(GW_Window* owner, int id, GW_Image* icon, const char* tooltip_utf8);
void GW_SetTrayIcon(GW_TrayIcon* tray, GW_Image* icon);
void GW_SetTrayTooltip(GW_TrayIcon* tray, const char* tooltip_utf8);
void GW_DestroyTrayIcon(GW_TrayIcon* tray);

// Native popup menu at the pointer (e.g. for the tray icon). items are
// UTF-8 labels; an empty string "" is a separator. Blocks until closed and
// returns the chosen index, or -1 (always -1 where unsupported).
int GW_ShowPopupMenu(GW_Window* owner, const char** items, int count);

#ifdef __cplusplus
}
#endif

#endif // GW_H
