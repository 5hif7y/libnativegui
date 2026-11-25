// Automated checks for gw_ui (feat/portmaster-ui): feeds synthetic events
// to GWUI_HandleEvent and checks what the widgets return. Never shows the
// window. Windows for now (font path).
//
// Build (Developer prompt, repo root, after building the library):
//   cl /nologo /MD /Iinclude examples\gw_ui_test.c build\nativegui.lib user32.lib gdi32.lib comdlg32.lib shell32.lib /Fe:build\gw_ui_test.exe
#include <stdio.h>
#include <string.h>
#include "gw.h"
#include "gw_ui.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* The program's state and one frame of its UI. */
typedef struct {
    int clicks;
    int toggle;
    int check;
    char text[32];
    int text_result;
    int sel;
    int sel_changed;
    int tab;
    int row_clicked;
    GWUI_Rect button_r, toggle_r, check_r, text_r, select_r, tabs_r, scroll_r, row0_r;
} App;

static const char* ITEMS[] = { "Allow", "Block", "Prompt" };
static const char* TABS[] = { "Monitor", "Apps", "Settings" };

static void frame(GWUI* ui, App* a) {
    do {
        GWUI_Begin(ui);
        // Row 1: a fixed 120 px label and a cell that takes the rest.
        int w2[2] = { 120, 0 };
        GWUI_Row(ui, 2, w2, 30);
        GWUI_Label(ui, "Widgets:");
        GWUI_Rect rest = GWUI_NextRect(ui);
        a->button_r = rest;
        // Then one widget per row, full width.
        GWUI_Row(ui, 1, NULL, 30);
        if (GWUI_Button(ui, "Click me")) a->clicks++;
        GWUI_Toggle(ui, "Enable thing", &a->toggle);
        GWUI_Checkbox(ui, "Check thing", &a->check);
        int r = GWUI_TextInput(ui, "name", a->text, sizeof(a->text), "type here");
        if (r) a->text_result |= r;
        if (GWUI_Select(ui, "action", ITEMS, 3, &a->sel)) a->sel_changed++;
        GWUI_Tabs(ui, TABS, 3, &a->tab);
        GWUI_BeginScroll(ui, "list", (GWUI_Rect){ 10, 330, 300, 120 });
        for (int i = 0; i < 20; i++) {
            char label[32];
            sprintf(label, "Row %d", i);
            GWUI_PushIDInt(ui, i);
            if (GWUI_Selectable(ui, label, 0)) a->row_clicked = i;
            GWUI_PopID(ui);
        }
        GWUI_EndScroll(ui);
    } while (GWUI_End(ui));
}

static void ev_move(GWUI* ui, int x, int y) {
    GW_Event e; memset(&e, 0, sizeof(e));
    e.type = GW_EVENT_MOUSE_MOTION; e.mouse_motion.x = x; e.mouse_motion.y = y;
    GWUI_HandleEvent(ui, &e);
}
static void ev_button(GWUI* ui, int down, int x, int y, int clicks) {
    GW_Event e; memset(&e, 0, sizeof(e));
    e.type = down ? GW_EVENT_MOUSE_BUTTON_DOWN : GW_EVENT_MOUSE_BUTTON_UP;
    e.mouse_button.x = x; e.mouse_button.y = y; e.mouse_button.button = 1; e.mouse_button.clicks = clicks;
    GWUI_HandleEvent(ui, &e);
}
static void click(GWUI* ui, App* a, int x, int y) {
    ev_move(ui, x, y); frame(ui, a);
    ev_button(ui, 1, x, y, 1); frame(ui, a);
    ev_button(ui, 0, x, y, 1); frame(ui, a);
}
static void type_text(GWUI* ui, App* a, const wchar_t* s) {
    for (; *s; s++) {
        GW_Event e; memset(&e, 0, sizeof(e));
        e.type = GW_EVENT_TEXT_INPUT; e.text.character = *s;
        GWUI_HandleEvent(ui, &e);
    }
    frame(ui, a);
}
static void key(GWUI* ui, App* a, int code, int mod) {
    GW_Event e; memset(&e, 0, sizeof(e));
    e.type = GW_EVENT_KEY_DOWN; e.key.keycode = code; e.key.mod = mod;
    GWUI_HandleEvent(ui, &e);
    frame(ui, a);
}
static void wheel(GWUI* ui, App* a, int x, int y, int delta) {
    GW_Event e; memset(&e, 0, sizeof(e));
    e.type = GW_EVENT_MOUSE_WHEEL; e.wheel.x = x; e.wheel.y = y; e.wheel.delta = delta;
    GWUI_HandleEvent(ui, &e);
    frame(ui, a);
}

int main(void) {
    GW_Window* win = GW_CreateWindow("gw_ui_test", 400, 480);
    GW_Font* font = GW_LoadFont("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
    GWUI* ui = GWUI_Create(win, font, NULL);
    CHECK(win && font && ui, "setup");
    App a;
    memset(&a, 0, sizeof(a));
    frame(ui, &a);

    // Layout: padding 10, row 30, spacing 6. Row 1 = label + cell (y 10..40),
    // then one item per row: button y 46, toggle 82, check 118, text 154,
    // select 190, tabs 226.
    int x = 200;
    click(ui, &a, x, 46 + 15);
    CHECK(a.clicks == 1, "button clicked once: %d", a.clicks);
    // Press on the button, release outside: no click.
    ev_move(ui, x, 61); frame(ui, &a);
    ev_button(ui, 1, x, 61, 1); frame(ui, &a);
    ev_move(ui, x, 300); ev_button(ui, 0, x, 300, 1); frame(ui, &a);
    CHECK(a.clicks == 1, "release outside does not click: %d", a.clicks);

    click(ui, &a, x, 82 + 15);
    CHECK(a.toggle == 1, "toggle on");
    click(ui, &a, x, 82 + 15);
    CHECK(a.toggle == 0, "toggle off");
    click(ui, &a, 20, 118 + 15);
    CHECK(a.check == 1, "checkbox");

    // Text field: focus, type (with a non-BMP char as a surrogate pair), edit.
    click(ui, &a, x, 154 + 15);
    CHECK(GWUI_TextFocused(ui), "text focused");
    type_text(ui, &a, L"hola \xD83D\xDE00");
    CHECK(strcmp(a.text, "hola \xf0\x9f\x98\x80") == 0, "typed utf-8: '%s'", a.text);
    CHECK(a.text_result & GWUI_TEXT_CHANGED, "changed flag");
    key(ui, &a, GW_KEY_BACKSPACE, 0);
    CHECK(strcmp(a.text, "hola ") == 0, "backspace removes the whole emoji: '%s'", a.text);
    key(ui, &a, GW_KEY_HOME, 0);
    type_text(ui, &a, L">");
    CHECK(strcmp(a.text, ">hola ") == 0, "home + insert: '%s'", a.text);
    key(ui, &a, 'A', GW_MOD_CTRL);
    type_text(ui, &a, L"x");
    CHECK(strcmp(a.text, "x") == 0, "ctrl+a then type replaces: '%s'", a.text);
    a.text_result = 0;
    key(ui, &a, GW_KEY_ENTER, 0);
    CHECK(a.text_result & GWUI_TEXT_SUBMITTED, "enter submits");
    type_text(ui, &a, L"0123456789012345678901234567890123456789");
    CHECK((int)strlen(a.text) == 31, "capacity respected: %d", (int)strlen(a.text));
    click(ui, &a, x, 46 + 15); // click elsewhere
    CHECK(!GWUI_TextFocused(ui), "click elsewhere unfocuses");
    CHECK(a.clicks == 2, "that click still hit the button: %d", a.clicks);

    // Select: open, pick the third item. Items open below: y = 190+30+4 = 224,
    // first item at 228, item height 26.
    click(ui, &a, x, 190 + 15);
    click(ui, &a, x, 228 + 2 * 26 + 13);
    CHECK(a.sel == 2 && a.sel_changed == 1, "select picked item 2: sel %d changed %d", a.sel, a.sel_changed);
    // The popup covered the tabs: they must not have changed.
    CHECK(a.tab == 0, "popup blocks widgets under it: tab %d", a.tab);
    // Open and click outside: closes without change.
    click(ui, &a, x, 190 + 15);
    click(ui, &a, 380, 470);
    CHECK(a.sel == 2 && a.sel_changed == 1, "outside click keeps the value");

    // Tabs: "Monitor" starts at x 10; the second tab follows it.
    int tw = GWUI_TextWidth(ui, "Monitor", NULL) + 28;
    click(ui, &a, 10 + tw + 10, 226 + 15);
    CHECK(a.tab == 1, "second tab: %d", a.tab);

    // Scroll list: rows at y = 340 + i*36; row 1 = 376..406.
    a.row_clicked = -1;
    click(ui, &a, 100, 376 + 15);
    CHECK(a.row_clicked == 1, "row 1 clicked: %d", a.row_clicked);
    wheel(ui, &a, 100, 380, -120); // 3 rows of 30 = 90 px down
    CHECK(GWUI_GetScroll(ui, "list") == 90, "scrolled by wheel: %d", GWUI_GetScroll(ui, "list"));
    // Content y = 350 + 90 = 440 -> row 2 (412..442).
    a.row_clicked = -1;
    click(ui, &a, 100, 350);
    CHECK(a.row_clicked == 2, "row after scroll: %d", a.row_clicked);
    wheel(ui, &a, 100, 380, -120 * 50);
    int max_scroll = GWUI_GetScroll(ui, "list");
    CHECK(max_scroll == 20 * 36 - 6 - 100, "scroll clamps at the end: %d", max_scroll);
    wheel(ui, &a, 100, 380, 120 * 50);
    CHECK(GWUI_GetScroll(ui, "list") == 0, "scroll clamps at 0");
    wheel(ui, &a, 380, 100, -120); // wheel outside the list
    CHECK(GWUI_GetScroll(ui, "list") == 0, "wheel outside does not scroll");

    GWUI_Destroy(ui);
    GW_FreeFont(font);
    GW_DestroyWindow(win);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
