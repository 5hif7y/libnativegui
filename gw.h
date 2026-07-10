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
    GW_EVENT_MOUSE_MOTION
} GW_EventType;

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
        } mouse_button;
        struct {
            int x;
            int y;
        } mouse_motion;
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

#ifdef __cplusplus
}
#endif

#endif // GW_H
