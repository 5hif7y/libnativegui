#ifndef GW_UI_H
#define GW_UI_H

/* gw_ui: immediate-mode widgets on top of LibGW (feat/portmaster-ui).
 *
 * The program keeps its own state; every frame it calls the widget
 * functions, which draw and return whether they were used:
 *
 *     GW_Event ev;
 *     while (GW_WaitEvent(&ev)) {
 *         GWUI_HandleEvent(ui, &ev);           // feed every event
 *         if (ev.type == GW_EVENT_QUIT) break;
 *         do {
 *             GWUI_Begin(ui);
 *             if (GWUI_Button(ui, "Save")) save();
 *             GWUI_Toggle(ui, "Enabled", &enabled);
 *         } while (GWUI_End(ui));               // 1 = draw once more (state changed)
 *         GW_Present(win);
 *     }
 *
 * Text is UTF-8. A label "Text##key" shows "Text" and uses the whole string
 * as identity, so equal labels can coexist; GWUI_PushID / GWUI_PopID scope
 * identities (e.g. per list row). Colors are 0xAARRGGBB.
 *
 * Layout: each container (the window, a panel, a scroll area) places items
 * in rows. GWUI_Row(ui, n, widths, height) sets the columns of the next
 * rows; a width > 0 is pixels, 0 shares the remaining width equally. Without
 * a GWUI_Row, items are one per line, full width, theme row height.
 */

#include "gw.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int x, y, w, h; } GWUI_Rect;
typedef uint32_t GWUI_Id;
typedef struct GWUI GWUI;

typedef struct {
    uint32_t bg;          // window background
    uint32_t panel;       // panels / cards
    uint32_t panel_alt;   // alternate rows, inputs
    uint32_t border;
    uint32_t text;
    uint32_t text_dim;
    uint32_t accent;      // primary buttons, toggles on, focus
    uint32_t accent_text;
    uint32_t hover;       // overlay on hovered items
    uint32_t pressed;     // overlay on pressed items
    uint32_t selection;   // selected rows / text selection
    uint32_t danger;
    uint32_t success;
    uint32_t warning;
    uint32_t scrollbar;
    int radius;
    int padding;          // inside containers and widgets
    int spacing;          // between items
    int row_height;
    int scrollbar_width;
} GWUI_Theme;

GWUI_Theme GWUI_DefaultTheme(void);   // dark

// small may be NULL (then regular is used for small text).
GWUI* GWUI_Create(GW_Window* win, GW_Font* regular, GW_Font* small);
void  GWUI_Destroy(GWUI* ui);
void  GWUI_SetTheme(GWUI* ui, const GWUI_Theme* theme);
const GWUI_Theme* GWUI_GetTheme(GWUI* ui);
GW_Window* GWUI_GetWindow(GWUI* ui);

// Feeds one event (input, resize, focus). Call it for every event.
void GWUI_HandleEvent(GWUI* ui, const GW_Event* ev);

// Frame. GWUI_End returns 1 if the frame changed state that only shows on
// the next frame (a click opened a popup, a text field got focus...): run
// the frame again before presenting. It also keeps the cursor shape.
void GWUI_Begin(GWUI* ui);
int  GWUI_End(GWUI* ui);
// Ask for one more frame (e.g. the program changed its own data).
void GWUI_RequestFrame(GWUI* ui);
// True while a text field is focused (caret blinking: the program may run a
// GW timer and redraw).
int  GWUI_TextFocused(GWUI* ui);

// Identity scopes.
void GWUI_PushID(GWUI* ui, const char* str);
void GWUI_PushIDInt(GWUI* ui, int n);
void GWUI_PopID(GWUI* ui);
GWUI_Id GWUI_GetID(GWUI* ui, const char* str);

// Layout.
void GWUI_Row(GWUI* ui, int count, const int* widths, int height);
GWUI_Rect GWUI_NextRect(GWUI* ui);          // the next cell, for custom drawing
GWUI_Rect GWUI_ContentRect(GWUI* ui);       // the current container's content area
void GWUI_Space(GWUI* ui, int height);      // vertical gap, ends the current row
void GWUI_Separator(GWUI* ui);

// Containers. Panels draw a background card (color 0 = none) and clip their
// content; scroll areas also scroll it (wheel, scrollbar drag).
void GWUI_BeginPanel(GWUI* ui, GWUI_Rect r, uint32_t color);
void GWUI_EndPanel(GWUI* ui);
void GWUI_BeginScroll(GWUI* ui, const char* id, GWUI_Rect r);
void GWUI_EndScroll(GWUI* ui);
// Scroll position of a scroll area, in pixels (set it to jump).
int  GWUI_GetScroll(GWUI* ui, const char* id);
void GWUI_SetScroll(GWUI* ui, const char* id, int y);

// Text.
#define GWUI_ALIGN_LEFT   0
#define GWUI_ALIGN_CENTER 1
#define GWUI_ALIGN_RIGHT  2
void GWUI_Label(GWUI* ui, const char* text);
void GWUI_LabelDim(GWUI* ui, const char* text);
void GWUI_LabelEx(GWUI* ui, const char* text, GW_Font* font, uint32_t color, int align);
// Draws text in r (vertically centered, clipped to r). font NULL = regular.
void GWUI_DrawText(GWUI* ui, GWUI_Rect r, const char* text, GW_Font* font, uint32_t color, int align);
int  GWUI_TextWidth(GWUI* ui, const char* text, GW_Font* font);

// Widgets. They return 1 when used this frame.
#define GWUI_BUTTON_NORMAL  0
#define GWUI_BUTTON_PRIMARY 1
#define GWUI_BUTTON_DANGER  2
#define GWUI_BUTTON_FLAT    3
int GWUI_Button(GWUI* ui, const char* label);
int GWUI_ButtonEx(GWUI* ui, const char* label, int style, GW_Image* icon);
int GWUI_Toggle(GWUI* ui, const char* label, int* value);     // switch, label on the left
int GWUI_Checkbox(GWUI* ui, const char* label, int* value);
// Single-line text field editing buf (NUL-terminated, capacity cap bytes).
#define GWUI_TEXT_CHANGED   1
#define GWUI_TEXT_SUBMITTED 2   // Enter pressed
int GWUI_TextInput(GWUI* ui, const char* id, char* buf, int cap, const char* placeholder);
// Drop-down list; returns 1 when *selected changed.
int GWUI_Select(GWUI* ui, const char* id, const char** items, int count, int* selected);
// Row of tabs; returns 1 when *active changed.
int GWUI_Tabs(GWUI* ui, const char** labels, int count, int* active);
// Full-width clickable row (lists); returns 1 when clicked.
int GWUI_Selectable(GWUI* ui, const char* label, int selected);
// Icon in the next cell, tinted (tint 0 = draw the image as is).
void GWUI_Icon(GWUI* ui, GW_Image* img, int size, uint32_t tint);
// Small colored badge ("Allowed", "Blocked"...).
void GWUI_Badge(GWUI* ui, const char* text, uint32_t color);
// Horizontal progress bar (0..1).
void GWUI_Progress(GWUI* ui, float value, uint32_t color);

// Interaction on an arbitrary rect (custom widgets): 1 if clicked; hovered
// and held (may be NULL) report the state.
int GWUI_Interact(GWUI* ui, GWUI_Id id, GWUI_Rect r, int* hovered, int* held);
// Mouse position and whether r contains it (and is not covered by a popup).
void GWUI_MousePos(GWUI* ui, int* x, int* y);
int  GWUI_MouseOver(GWUI* ui, GWUI_Rect r);

#ifdef __cplusplus
}
#endif

#endif // GW_UI_H
