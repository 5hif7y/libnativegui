// gw_ui: immediate-mode widgets on top of LibGW (feat/portmaster-ui).
// See include/gw_ui.h for the model. Everything is drawn with the LibGW
// software rasterizer; containers clip with GW_SetClipRect.
#include "gw_ui.h"
#include <stdlib.h>
#include <string.h>

#define MAX_CONTAINERS 16
#define MAX_IDSTACK    32
#define MAX_COLS       16
#define SCROLL_SLOTS   64
#define POPUP_MAX      64
#define POPUP_ITEM_LEN 96
#define VOID_ID        ((GWUI_Id)0xFFFFFFFFu)

typedef struct {
    GWUI_Rect rect;       // outer rect
    GWUI_Rect body;       // content area
    int cur_y;            // y of the next row
    int has_row;          // GWUI_Row set for this container
    int row_count, row_h, row_widths[MAX_COLS];
    int row_item;         // next item index in the current row (0 = start a row)
    int row_y, col_x[MAX_COLS], col_w[MAX_COLS];
    int max_y;            // lowest content edge
    int saved_clip[4];    // clip to restore on pop
    int scroll_slot;      // -1 if not a scroll area
} Container;

typedef struct {
    GWUI_Id id;
    int used;
    int offset;
    int content_h;
    int view_h;
} ScrollSlot;

struct GWUI {
    GW_Window* win;
    GW_Font* font;
    GW_Font* small;
    GWUI_Theme t;
    int line_h, small_line_h;

    // input accumulated since the last frame
    int mx, my, down, pressed, released, dbl, mods, wheel;
    int last_click_dbl; // the last left press was a double click (kept until the next press)
    char text_in[128];
    int text_len;
    int keys[32];
    int nkeys;
    unsigned pending_high; // UTF-16 high surrogate from WM_CHAR

    // identity
    GWUI_Id id_stack[MAX_IDSTACK];
    int id_depth;
    GWUI_Id hot, active, focus;
    int focus_kept;

    // text field state (for the focused field)
    int caret;
    int text_scroll;
    int select_all;
    uint64_t caret_t0;

    // popup (select)
    GWUI_Id popup;
    GWUI_Rect popup_rect, popup_rect_prev;
    int popup_valid_prev;
    char popup_items[POPUP_MAX][POPUP_ITEM_LEN];
    int popup_count, popup_sel;
    int popup_pressing;
    GWUI_Id popup_chosen_id;
    int popup_chosen;
    int popup_drawn;  // the Select owning the popup ran this frame

    // containers and scrolling
    Container cs[MAX_CONTAINERS];
    int nc;
    ScrollSlot scrolls[SCROLL_SLOTS];
    GWUI_Id drag_scroll;
    int drag_my0, drag_off0;

    int again;
    GW_Cursor cursor, cursor_set;
    int in_frame;
};

/* ---- small helpers --------------------------------------------------------- */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static GWUI_Rect rect_make(int x, int y, int w, int h) { GWUI_Rect r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }

static GWUI_Rect rect_inset(GWUI_Rect r, int d) {
    return rect_make(r.x + d, r.y + d, imax(0, r.w - 2 * d), imax(0, r.h - 2 * d));
}

static GWUI_Rect rect_intersect(GWUI_Rect a, GWUI_Rect b) {
    int x1 = imax(a.x, b.x), y1 = imax(a.y, b.y);
    int x2 = imin(a.x + a.w, b.x + b.w), y2 = imin(a.y + a.h, b.y + b.h);
    return rect_make(x1, y1, imax(0, x2 - x1), imax(0, y2 - y1));
}

static int rect_contains(GWUI_Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static uint32_t with_alpha(uint32_t c, unsigned a) { return (c & 0x00FFFFFFu) | ((uint32_t)a << 24); }

static GWUI_Id hash_str(GWUI_Id seed, const char* s) {
    GWUI_Id h = seed ? seed : 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h ? h : 1;
}

static GWUI_Id hash_int(GWUI_Id seed, int n) {
    GWUI_Id h = seed ? seed : 2166136261u;
    for (int i = 0; i < 4; i++) { h ^= (unsigned)((n >> (i * 8)) & 0xFF); h *= 16777619u; }
    return h ? h : 1;
}

// The visible part of a label: everything before "##".
static int label_len(const char* s) {
    const char* p = strstr(s, "##");
    return p ? (int)(p - s) : (int)strlen(s);
}

static void copy_label(char* dst, int cap, const char* s) {
    int n = imin(label_len(s), cap - 1);
    memcpy(dst, s, (size_t)n);
    dst[n] = 0;
}

static int text_w(GW_Font* f, const char* s) {
    int w = 0, h = 0;
    GW_MeasureTextUTF8(f, s, &w, &h);
    return w;
}

// Writes s into out, cut with "..." so it fits max_w (UTF-8 safe).
static void fit_text(GW_Font* f, const char* s, int max_w, char* out, int cap) {
    int n = (int)strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(out, s, (size_t)n);
    out[n] = 0;
    if (text_w(f, out) <= max_w) return;
    int ell = text_w(f, "...");
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80) n--; // to a char boundary
        out[n] = 0;
        if (text_w(f, out) + ell <= max_w) break;
    }
    if (n + 4 <= cap) memcpy(out + n, "...", 4);
}

/* ---- theme / lifecycle -------------------------------------------------------- */

GWUI_Theme GWUI_DefaultTheme(void) {
    GWUI_Theme t;
    t.bg = 0xFF121417;
    t.panel = 0xFF1B1E23;
    t.panel_alt = 0xFF23272E;
    t.border = 0xFF2F343C;
    t.text = 0xFFE6E8EB;
    t.text_dim = 0xFF8B929C;
    t.accent = 0xFF4E97FA;
    t.accent_text = 0xFFFFFFFF;
    t.hover = 0x14FFFFFF;
    t.pressed = 0x28FFFFFF;
    t.selection = 0xFF26395C;
    t.danger = 0xFFE5484D;
    t.success = 0xFF30A46C;
    t.warning = 0xFFF5A524;
    t.scrollbar = 0x50FFFFFF;
    t.radius = 6;
    t.padding = 10;
    t.spacing = 6;
    t.row_height = 30;
    t.scrollbar_width = 8;
    return t;
}

static void update_metrics(GWUI* ui) {
    int a, d;
    GW_GetFontMetrics(ui->font, &a, &d, &ui->line_h);
    GW_GetFontMetrics(ui->small ? ui->small : ui->font, &a, &d, &ui->small_line_h);
}

GWUI* GWUI_Create(GW_Window* win, GW_Font* regular, GW_Font* small) {
    if (!win || !regular) return NULL;
    GWUI* ui = (GWUI*)calloc(1, sizeof(GWUI));
    if (!ui) return NULL;
    ui->win = win;
    ui->font = regular;
    ui->small = small;
    ui->t = GWUI_DefaultTheme();
    ui->mx = ui->my = -10000;
    update_metrics(ui);
    GW_SetWheelMode(win, GW_WHEEL_EVENTS);
    return ui;
}

void GWUI_Destroy(GWUI* ui) { free(ui); }
void GWUI_SetTheme(GWUI* ui, const GWUI_Theme* theme) { if (ui && theme) ui->t = *theme; }
const GWUI_Theme* GWUI_GetTheme(GWUI* ui) { return ui ? &ui->t : NULL; }
GW_Window* GWUI_GetWindow(GWUI* ui) { return ui ? ui->win : NULL; }
void GWUI_RequestFrame(GWUI* ui) { if (ui) ui->again = 1; }
int GWUI_TextFocused(GWUI* ui) { return ui && ui->focus != 0; }
int GWUI_LastClickWasDouble(GWUI* ui) { return ui && ui->last_click_dbl; }

void GWUI_FocusText(GWUI* ui, const char* id_str) {
    if (!ui) return;
    GWUI_Id id = GWUI_GetID(ui, id_str);
    if (ui->focus == id) return;
    ui->focus = id;
    ui->text_scroll = 0;
    ui->caret = 1 << 30; // clamped to the end by GWUI_TextInput
    ui->select_all = 0;
    ui->caret_t0 = GW_GetTicks();
    ui->again = 1;
}

/* ---- input --------------------------------------------------------------------- */

static void put_utf8(GWUI* ui, unsigned cp) {
    char b[4];
    int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    if (ui->text_len + n >= (int)sizeof(ui->text_in)) return;
    memcpy(ui->text_in + ui->text_len, b, (size_t)n);
    ui->text_len += n;
    ui->text_in[ui->text_len] = 0;
}

void GWUI_HandleEvent(GWUI* ui, const GW_Event* ev) {
    if (!ui || !ev) return;
    switch (ev->type) {
        case GW_EVENT_MOUSE_MOTION:
            ui->mx = ev->mouse_motion.x;
            ui->my = ev->mouse_motion.y;
            break;
        case GW_EVENT_MOUSE_BUTTON_DOWN:
            ui->mx = ev->mouse_button.x;
            ui->my = ev->mouse_button.y;
            if (ev->mouse_button.button == 1) {
                ui->down = 1;
                ui->pressed = 1;
                ui->dbl = ev->mouse_button.clicks >= 2;
                ui->last_click_dbl = ui->dbl;
                ui->mods = ev->mouse_button.mod;
            }
            break;
        case GW_EVENT_MOUSE_BUTTON_UP:
            ui->mx = ev->mouse_button.x;
            ui->my = ev->mouse_button.y;
            if (ev->mouse_button.button == 1) {
                ui->down = 0;
                ui->released = 1;
            }
            break;
        case GW_EVENT_MOUSE_WHEEL:
            ui->mx = ev->wheel.x; // the wheel goes to what is under the pointer
            ui->my = ev->wheel.y;
            if (!ev->wheel.horizontal) ui->wheel += ev->wheel.delta;
            break;
        case GW_EVENT_KEY_DOWN: {
            int k = ev->key.keycode;
            if (k >= 'a' && k <= 'z') k -= 32; // X11 keysyms are lowercase
            if (ui->nkeys < 32) ui->keys[ui->nkeys++] = k | (ev->key.mod << 16);
            ui->mods = ev->key.mod;
            break;
        }
        case GW_EVENT_TEXT_INPUT: {
            unsigned c = (unsigned)ev->text.character;
            if (c >= 0xD800 && c <= 0xDBFF) { ui->pending_high = c; break; }
            if (c >= 0xDC00 && c <= 0xDFFF) {
                if (ui->pending_high) c = 0x10000 + ((ui->pending_high - 0xD800) << 10) + (c - 0xDC00);
                else c = 0xFFFD;
            }
            ui->pending_high = 0;
            if (c < 32 || c == 127) break; // control chars come as keys
            put_utf8(ui, c);
            break;
        }
        case GW_EVENT_FOCUS_LOST:
            ui->down = 0;
            ui->active = 0;
            break;
        default:
            break;
    }
}

static int key_pressed(GWUI* ui, int key, int need_mods) {
    for (int i = 0; i < ui->nkeys; i++) {
        int k = ui->keys[i] & 0xFFFF, m = ui->keys[i] >> 16;
        if (k == key && (m & need_mods) == need_mods) return 1;
    }
    return 0;
}

/* ---- identity ---------------------------------------------------------------------- */

static GWUI_Id id_seed(GWUI* ui) { return ui->id_depth ? ui->id_stack[ui->id_depth - 1] : 0; }

GWUI_Id GWUI_GetID(GWUI* ui, const char* str) { return hash_str(id_seed(ui), str ? str : ""); }

void GWUI_PushID(GWUI* ui, const char* str) {
    if (ui->id_depth < MAX_IDSTACK) { ui->id_stack[ui->id_depth] = GWUI_GetID(ui, str); ui->id_depth++; }
}

void GWUI_PushIDInt(GWUI* ui, int n) {
    if (ui->id_depth < MAX_IDSTACK) { ui->id_stack[ui->id_depth] = hash_int(id_seed(ui), n); ui->id_depth++; }
}

void GWUI_PopID(GWUI* ui) { if (ui->id_depth > 0) ui->id_depth--; }

/* ---- containers / layout ------------------------------------------------------------ */

static Container* top(GWUI* ui) { return &ui->cs[ui->nc - 1]; }

static void current_clip(GWUI* ui, int* c) { GW_GetClipRect(ui->win, &c[0], &c[1], &c[2], &c[3]); }

static GWUI_Rect clip_rect(GWUI* ui) {
    int c[4];
    current_clip(ui, c);
    return rect_make(c[0], c[1], c[2], c[3]);
}

static Container* push_container(GWUI* ui, GWUI_Rect rect, GWUI_Rect body, int scroll_slot) {
    if (ui->nc >= MAX_CONTAINERS) return NULL;
    Container* c = &ui->cs[ui->nc++];
    memset(c, 0, sizeof(*c));
    c->rect = rect;
    c->body = body;
    c->cur_y = body.y;
    c->max_y = body.y;
    c->scroll_slot = scroll_slot;
    current_clip(ui, c->saved_clip);
    GWUI_Rect clip = rect_intersect(clip_rect(ui), rect);
    GW_SetClipRect(ui->win, clip.x, clip.y, clip.w, clip.h);
    return c;
}

static void pop_container(GWUI* ui) {
    if (ui->nc <= 1) return; // the root stays
    Container* c = top(ui);
    GW_SetClipRect(ui->win, c->saved_clip[0], c->saved_clip[1], c->saved_clip[2], c->saved_clip[3]);
    ui->nc--;
}

static void end_row(Container* c, int spacing) {
    if (c->row_item != 0) {
        c->cur_y = c->row_y + c->row_h + spacing;
        c->row_item = 0;
    }
}

void GWUI_Row(GWUI* ui, int count, const int* widths, int height) {
    Container* c = top(ui);
    end_row(c, ui->t.spacing);
    if (count < 1) count = 1;
    if (count > MAX_COLS) count = MAX_COLS;
    c->has_row = 1;
    c->row_count = count;
    c->row_h = height > 0 ? height : ui->t.row_height;
    for (int i = 0; i < count; i++) c->row_widths[i] = widths ? widths[i] : 0;
}

GWUI_Rect GWUI_NextRect(GWUI* ui) {
    Container* c = top(ui);
    if (!c->has_row) {
        c->has_row = 1;
        c->row_count = 1;
        c->row_h = ui->t.row_height;
        c->row_widths[0] = 0;
    }
    if (c->row_item == 0) {
        c->row_y = c->cur_y;
        int n = c->row_count, fixed = 0, zeros = 0;
        for (int i = 0; i < n; i++) {
            if (c->row_widths[i] > 0) fixed += c->row_widths[i];
            else zeros++;
        }
        int free_w = c->body.w - fixed - ui->t.spacing * (n - 1);
        int share = zeros ? imax(0, free_w / zeros) : 0;
        int x = c->body.x;
        for (int i = 0; i < n; i++) {
            int w = c->row_widths[i] > 0 ? c->row_widths[i] : share;
            // The last zero-width column takes the rounding remainder.
            if (c->row_widths[i] <= 0 && i == n - 1 && zeros) w = imax(0, c->body.x + c->body.w - x);
            c->col_x[i] = x;
            c->col_w[i] = w;
            x += w + ui->t.spacing;
        }
    }
    int i = c->row_item;
    GWUI_Rect r = rect_make(c->col_x[i], c->row_y, c->col_w[i], c->row_h);
    c->max_y = imax(c->max_y, c->row_y + c->row_h);
    c->row_item++;
    if (c->row_item >= c->row_count) end_row(c, ui->t.spacing);
    return r;
}

GWUI_Rect GWUI_ContentRect(GWUI* ui) { return top(ui)->body; }

void GWUI_Space(GWUI* ui, int height) {
    Container* c = top(ui);
    end_row(c, ui->t.spacing);
    c->cur_y += height;
    c->max_y = imax(c->max_y, c->cur_y);
}

void GWUI_Separator(GWUI* ui) {
    Container* c = top(ui);
    end_row(c, ui->t.spacing);
    GW_FillRectBlend(ui->win, c->body.x, c->cur_y, c->body.w, 1, ui->t.border);
    c->cur_y += 1 + ui->t.spacing;
    c->max_y = imax(c->max_y, c->cur_y);
}

void GWUI_BeginPanel(GWUI* ui, GWUI_Rect r, uint32_t color) {
    if ((color >> 24) != 0) GW_FillRoundRect(ui->win, r.x, r.y, r.w, r.h, ui->t.radius, color);
    push_container(ui, r, rect_inset(r, ui->t.padding), -1);
}

void GWUI_EndPanel(GWUI* ui) { pop_container(ui); }

static ScrollSlot* scroll_slot(GWUI* ui, GWUI_Id id, int create) {
    ScrollSlot* free_slot = NULL;
    for (int i = 0; i < SCROLL_SLOTS; i++) {
        if (ui->scrolls[i].used && ui->scrolls[i].id == id) return &ui->scrolls[i];
        if (!ui->scrolls[i].used && !free_slot) free_slot = &ui->scrolls[i];
    }
    if (!create) return NULL;
    if (!free_slot) free_slot = &ui->scrolls[id % SCROLL_SLOTS]; // evict
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = 1;
    free_slot->id = id;
    return free_slot;
}

static void clamp_scroll(ScrollSlot* s) {
    int max_off = imax(0, s->content_h - s->view_h);
    if (s->offset > max_off) s->offset = max_off;
    if (s->offset < 0) s->offset = 0;
}

void GWUI_BeginScroll(GWUI* ui, const char* id, GWUI_Rect r) {
    ScrollSlot* s = scroll_slot(ui, GWUI_GetID(ui, id), 1);
    int pad = ui->t.padding;
    s->view_h = imax(0, r.h - 2 * pad);
    clamp_scroll(s);
    int bar = s->content_h > s->view_h ? ui->t.scrollbar_width + 4 : 0;
    GWUI_Rect body = rect_make(r.x + pad, r.y + pad - s->offset, imax(0, r.w - 2 * pad - bar), s->view_h);
    push_container(ui, r, body, (int)(s - ui->scrolls));
}

void GWUI_EndScroll(GWUI* ui) {
    Container* c = top(ui);
    if (c->scroll_slot < 0) { pop_container(ui); return; }
    end_row(c, ui->t.spacing);
    ScrollSlot* s = &ui->scrolls[c->scroll_slot];
    GWUI_Rect r = c->rect;
    int content_h = imax(0, c->max_y - c->body.y);
    if (content_h != s->content_h) { s->content_h = content_h; ui->again = 1; }

    // Wheel: the innermost hovered scroll area takes it (inner ends first).
    if (ui->wheel && GWUI_MouseOver(ui, r)) {
        int step = ui->t.row_height * 3;
        s->offset -= ui->wheel * step / 120;
        ui->wheel = 0;
        clamp_scroll(s);
        ui->again = 1;
    }

    if (s->content_h > s->view_h && s->view_h > 0) {
        int track_x = r.x + r.w - ui->t.scrollbar_width - 3, track_y = r.y + 3, track_h = r.h - 6;
        int thumb_h = imax(24, track_h * s->view_h / s->content_h);
        int max_off = s->content_h - s->view_h;
        int thumb_y = track_y + (track_h - thumb_h) * s->offset / imax(1, max_off);
        GWUI_Rect thumb = rect_make(track_x, thumb_y, ui->t.scrollbar_width, thumb_h);
        GWUI_Id tid = s->id ^ 0x5C5C5C5Cu;
        int hovered = 0, held = 0;
        GWUI_Interact(ui, tid, thumb, &hovered, &held);
        if (ui->active == tid && ui->pressed) { ui->drag_my0 = ui->my; ui->drag_off0 = s->offset; }
        if (held) {
            int dy = ui->my - ui->drag_my0;
            s->offset = ui->drag_off0 + dy * max_off / imax(1, track_h - thumb_h);
            clamp_scroll(s);
        }
        uint32_t col = (held || hovered) ? with_alpha(ui->t.scrollbar, 0xA0) : ui->t.scrollbar;
        GW_FillRoundRect(ui->win, thumb.x, thumb.y, thumb.w, thumb.h, thumb.w / 2, col);
    }
    pop_container(ui);
}

int GWUI_GetScroll(GWUI* ui, const char* id) {
    ScrollSlot* s = scroll_slot(ui, GWUI_GetID(ui, id), 0);
    return s ? s->offset : 0;
}

void GWUI_SetScroll(GWUI* ui, const char* id, int y) {
    ScrollSlot* s = scroll_slot(ui, GWUI_GetID(ui, id), 1);
    s->offset = y;
    clamp_scroll(s);
    ui->again = 1;
}

/* ---- frame --------------------------------------------------------------------------- */

void GWUI_Begin(GWUI* ui) {
    int w, h;
    GW_GetWindowSize(ui->win, &w, &h);
    GW_ResetClipRect(ui->win);
    GW_Clear(ui->win, ui->t.bg);
    ui->nc = 0;
    ui->id_depth = 0;
    ui->hot = 0;
    ui->cursor = GW_CURSOR_ARROW;
    ui->focus_kept = 0;
    ui->popup_drawn = 0;
    ui->in_frame = 1;
    GWUI_Rect all = rect_make(0, 0, w, h);
    push_container(ui, all, rect_inset(all, ui->t.padding), -1);
}

static void draw_popup_and_input(GWUI* ui);

int GWUI_End(GWUI* ui) {
    while (ui->nc > 1) pop_container(ui);
    GW_ResetClipRect(ui->win);
    if (ui->popup && !ui->popup_drawn) ui->popup = 0; // its Select is gone
    draw_popup_and_input(ui);

    if (ui->pressed && ui->active == 0) ui->active = VOID_ID;
    if (!ui->down) {
        if (ui->active) ui->again = 1; // hover state changes after a release
        ui->active = 0;
    }
    if (ui->pressed && !ui->focus_kept && ui->focus) { ui->focus = 0; ui->again = 1; }

    // Transient input is consumed by the first frame after the events.
    ui->pressed = ui->released = ui->dbl = 0;
    ui->wheel = 0;
    ui->text_len = 0;
    ui->text_in[0] = 0;
    ui->nkeys = 0;

    if (ui->cursor != ui->cursor_set) {
        GW_SetCursor(ui->win, ui->cursor);
        ui->cursor_set = ui->cursor;
    }
    ui->in_frame = 0;
    int again = ui->again;
    ui->again = 0;
    return again;
}

/* ---- interaction ----------------------------------------------------------------------- */

void GWUI_MousePos(GWUI* ui, int* x, int* y) {
    if (x) *x = ui->mx;
    if (y) *y = ui->my;
}

int GWUI_MouseOver(GWUI* ui, GWUI_Rect r) {
    if (!rect_contains(r, ui->mx, ui->my)) return 0;
    if (!rect_contains(clip_rect(ui), ui->mx, ui->my)) return 0;
    // Covered by an open popup (its rect from the previous frame)?
    if (ui->popup && ui->popup_valid_prev && rect_contains(ui->popup_rect_prev, ui->mx, ui->my)) return 0;
    return 1;
}

int GWUI_Interact(GWUI* ui, GWUI_Id id, GWUI_Rect r, int* hovered, int* held) {
    int over = GWUI_MouseOver(ui, r);
    int hov = over && (ui->active == 0 || ui->active == id);
    if (hov) ui->hot = id;
    if (over && ui->pressed && ui->active == 0) ui->active = id;
    int clicked = ui->released && ui->active == id && over;
    if (hovered) *hovered = hov;
    if (held) *held = (ui->active == id && ui->down);
    return clicked;
}

/* ---- text ------------------------------------------------------------------------------------ */

int GWUI_TextWidth(GWUI* ui, const char* text, GW_Font* font) {
    return text_w(font ? font : ui->font, text);
}

void GWUI_DrawText(GWUI* ui, GWUI_Rect r, const char* text, GW_Font* font, uint32_t color, int align) {
    if (!font) font = ui->font;
    char buf[512];
    char vis[512];
    copy_label(vis, sizeof(vis), text);
    fit_text(font, vis, r.w, buf, sizeof(buf));
    int a, d, lh;
    GW_GetFontMetrics(font, &a, &d, &lh);
    int tw = text_w(font, buf);
    int x = r.x;
    if (align == GWUI_ALIGN_CENTER) x = r.x + (r.w - tw) / 2;
    else if (align == GWUI_ALIGN_RIGHT) x = r.x + r.w - tw;
    int y = r.y + (r.h - lh) / 2;
    int saved[4];
    current_clip(ui, saved);
    GWUI_Rect clip = rect_intersect(rect_make(saved[0], saved[1], saved[2], saved[3]), r);
    GW_SetClipRect(ui->win, clip.x, clip.y, clip.w, clip.h);
    GW_DrawTextUTF8(ui->win, font, x, y, buf, color);
    GW_SetClipRect(ui->win, saved[0], saved[1], saved[2], saved[3]);
}

void GWUI_LabelEx(GWUI* ui, const char* text, GW_Font* font, uint32_t color, int align) {
    GWUI_DrawText(ui, GWUI_NextRect(ui), text, font, color, align);
}

/* Word wrapping: walks text one line at a time. A line is the longest run
 * that fits width, broken after the last space in it (the space is not
 * drawn), at "\n", or by characters when a single word is wider than width. */
typedef void (*wrap_line_fn)(void* user, const char* s, int len, int line);

static int wrap_text(GW_Font* f, const char* text, int width, wrap_line_fn fn, void* user) {
    char ch_buf[8];
    int lines = 0;
    const char* p = text;
    if (width < 1) width = 1;
    while (*p) {
        // Width is summed per character (advance widths, no kerning): linear
        // per line, so long texts stay cheap to wrap every frame.
        int n = 0, last_space = -1, fit = 0, w = 0;
        for (;;) {
            unsigned char ch = (unsigned char)p[n];
            if (ch == 0 || ch == '\n') { fit = n; break; }
            int next = n + 1;
            while (p[next] && ((unsigned char)p[next] & 0xC0) == 0x80 && next - n < 4) next++; // whole UTF-8 char
            memcpy(ch_buf, p + n, (size_t)(next - n));
            ch_buf[next - n] = 0;
            w += text_w(f, ch_buf);
            if (n >= 1000) { fit = n; break; } // draw buffer limit
            if (w > width && n > 0) {
                fit = last_space > 0 ? last_space : n;
                break;
            }
            if (ch == ' ') last_space = n;
            n = next;
        }
        if (fn) fn(user, p, fit, lines);
        lines++;
        p += fit;
        if (*p == '\n') p++;
        else while (*p == ' ') p++; // the break swallows the spaces
    }
    return lines;
}

static int line_height(GW_Font* f) {
    int a, d, lh;
    GW_GetFontMetrics(f, &a, &d, &lh);
    return lh;
}

int GWUI_MeasureWrapped(GWUI* ui, const char* text, GW_Font* font, int width) {
    if (!font) font = ui->font;
    if (!text || !*text) return 0;
    return wrap_text(font, text, width, NULL, NULL) * line_height(font);
}

typedef struct {
    GWUI* ui;
    GW_Font* font;
    uint32_t color;
    int x, y, lh;
    int clip_y0, clip_y1;
} WrapDraw;

static void wrap_draw_line(void* user, const char* s, int len, int line) {
    WrapDraw* w = (WrapDraw*)user;
    int y = w->y + line * w->lh;
    if (y + w->lh < w->clip_y0 || y > w->clip_y1) return; // off screen
    char buf[1024];
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    GW_DrawTextUTF8(w->ui->win, w->font, w->x, y, buf, w->color);
}

void GWUI_DrawTextWrapped(GWUI* ui, GWUI_Rect r, const char* text, GW_Font* font, uint32_t color) {
    if (!font) font = ui->font;
    if (!text || !*text) return;
    int saved[4];
    current_clip(ui, saved);
    GWUI_Rect clip = rect_intersect(rect_make(saved[0], saved[1], saved[2], saved[3]), r);
    if (clip.w <= 0 || clip.h <= 0) return;
    GW_SetClipRect(ui->win, clip.x, clip.y, clip.w, clip.h);
    WrapDraw w = {ui, font, color, r.x, r.y, line_height(font), clip.y, clip.y + clip.h};
    wrap_text(font, text, r.w, wrap_draw_line, &w);
    GW_SetClipRect(ui->win, saved[0], saved[1], saved[2], saved[3]);
}

int GWUI_Paragraph(GWUI* ui, const char* text, GW_Font* font, uint32_t color) {
    Container* c = top(ui);
    end_row(c, ui->t.spacing);
    int h = GWUI_MeasureWrapped(ui, text, font, c->body.w);
    if (h <= 0) return 0;
    GWUI_Rect r = rect_make(c->body.x, c->cur_y, c->body.w, h);
    GWUI_DrawTextWrapped(ui, r, text, font, color);
    c->cur_y += h + ui->t.spacing;
    c->max_y = imax(c->max_y, c->cur_y);
    return h;
}

void GWUI_Label(GWUI* ui, const char* text) { GWUI_LabelEx(ui, text, NULL, ui->t.text, GWUI_ALIGN_LEFT); }
void GWUI_LabelDim(GWUI* ui, const char* text) { GWUI_LabelEx(ui, text, NULL, ui->t.text_dim, GWUI_ALIGN_LEFT); }

/* ---- widgets ---------------------------------------------------------------------------------- */

static void hover_overlay(GWUI* ui, GWUI_Rect r, int hovered, int held, int radius) {
    if (held) GW_FillRoundRect(ui->win, r.x, r.y, r.w, r.h, radius, ui->t.pressed);
    else if (hovered) GW_FillRoundRect(ui->win, r.x, r.y, r.w, r.h, radius, ui->t.hover);
}

int GWUI_ButtonEx(GWUI* ui, const char* label, int style, GW_Image* icon) {
    GWUI_Rect r = GWUI_NextRect(ui);
    GWUI_Id id = GWUI_GetID(ui, label);
    int hovered, held;
    int clicked = GWUI_Interact(ui, id, r, &hovered, &held);
    if (hovered) ui->cursor = GW_CURSOR_HAND;
    uint32_t bg, fg;
    switch (style) {
        case GWUI_BUTTON_PRIMARY: bg = ui->t.accent; fg = ui->t.accent_text; break;
        case GWUI_BUTTON_DANGER:  bg = ui->t.danger; fg = 0xFFFFFFFF; break;
        case GWUI_BUTTON_FLAT:    bg = 0; fg = ui->t.text; break;
        default:                  bg = ui->t.panel_alt; fg = ui->t.text; break;
    }
    if (bg) GW_FillRoundRect(ui->win, r.x, r.y, r.w, r.h, ui->t.radius, bg);
    if (style == GWUI_BUTTON_NORMAL) GW_DrawRoundRect(ui->win, r.x, r.y, r.w, r.h, ui->t.radius, 1, ui->t.border);
    hover_overlay(ui, r, hovered, held, ui->t.radius);
    char vis[256];
    copy_label(vis, sizeof(vis), label);
    if (icon) {
        int isz = imin(r.h - 10, 18);
        int tw = vis[0] ? text_w(ui->font, vis) : 0;
        int total = isz + (tw ? 6 + tw : 0);
        int x = r.x + (r.w - total) / 2;
        GW_DrawImageTinted(ui->win, icon, x, r.y + (r.h - isz) / 2, isz, isz, fg);
        if (tw) GWUI_DrawText(ui, rect_make(x + isz + 6, r.y, r.w - (x + isz + 6 - r.x), r.h), vis, NULL, fg, GWUI_ALIGN_LEFT);
    } else {
        GWUI_DrawText(ui, rect_inset(r, 2), vis, NULL, fg, GWUI_ALIGN_CENTER);
    }
    return clicked;
}

int GWUI_Button(GWUI* ui, const char* label) { return GWUI_ButtonEx(ui, label, GWUI_BUTTON_NORMAL, NULL); }

int GWUI_Toggle(GWUI* ui, const char* label, int* value) {
    GWUI_Rect r = GWUI_NextRect(ui);
    GWUI_Id id = GWUI_GetID(ui, label);
    int hovered, held;
    int clicked = GWUI_Interact(ui, id, r, &hovered, &held);
    if (hovered) ui->cursor = GW_CURSOR_HAND;
    if (clicked && value) { *value = !*value; ui->again = 1; }
    int on = value && *value;
    int sw = 36, sh = 20;
    GWUI_Rect s = rect_make(r.x + r.w - sw, r.y + (r.h - sh) / 2, sw, sh);
    GW_FillRoundRect(ui->win, s.x, s.y, s.w, s.h, sh / 2, on ? ui->t.accent : ui->t.panel_alt);
    if (!on) GW_DrawRoundRect(ui->win, s.x, s.y, s.w, s.h, sh / 2, 1, ui->t.border);
    int knob = sh - 6;
    int kx = on ? s.x + s.w - knob - 3 : s.x + 3;
    GW_FillCircle(ui->win, kx + knob / 2, s.y + 3 + knob / 2, knob / 2, on ? 0xFFFFFFFF : ui->t.text_dim);
    if (hovered) GW_FillRoundRect(ui->win, s.x, s.y, s.w, s.h, sh / 2, ui->t.hover);
    GWUI_DrawText(ui, rect_make(r.x, r.y, imax(0, r.w - sw - 8), r.h), label, NULL, ui->t.text, GWUI_ALIGN_LEFT);
    return clicked;
}

int GWUI_Checkbox(GWUI* ui, const char* label, int* value) {
    GWUI_Rect r = GWUI_NextRect(ui);
    GWUI_Id id = GWUI_GetID(ui, label);
    int hovered, held;
    int clicked = GWUI_Interact(ui, id, r, &hovered, &held);
    if (hovered) ui->cursor = GW_CURSOR_HAND;
    if (clicked && value) { *value = !*value; ui->again = 1; }
    int on = value && *value;
    int bs = 18;
    GWUI_Rect b = rect_make(r.x, r.y + (r.h - bs) / 2, bs, bs);
    if (on) {
        GW_FillRoundRect(ui->win, b.x, b.y, bs, bs, 4, ui->t.accent);
        GW_DrawLineAA(ui->win, b.x + 4.0f, b.y + 9.5f, b.x + 7.5f, b.y + 13.0f, 2.0f, 0xFFFFFFFF);
        GW_DrawLineAA(ui->win, b.x + 7.5f, b.y + 13.0f, b.x + 14.0f, b.y + 5.5f, 2.0f, 0xFFFFFFFF);
    } else {
        GW_FillRoundRect(ui->win, b.x, b.y, bs, bs, 4, ui->t.panel_alt);
        GW_DrawRoundRect(ui->win, b.x, b.y, bs, bs, 4, 1, ui->t.border);
    }
    if (hovered) GW_FillRoundRect(ui->win, b.x, b.y, bs, bs, 4, ui->t.hover);
    GWUI_DrawText(ui, rect_make(r.x + bs + 8, r.y, imax(0, r.w - bs - 8), r.h), label, NULL, ui->t.text, GWUI_ALIGN_LEFT);
    return clicked;
}

/* text field helpers (byte offsets on UTF-8 boundaries) */
static int prev_char(const char* s, int i) {
    if (i <= 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

static int next_char(const char* s, int i) {
    int n = (int)strlen(s);
    if (i >= n) return n;
    i++;
    while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

static int prefix_width(GW_Font* f, const char* s, int bytes) {
    char tmp[1024];
    int n = imin(bytes, (int)sizeof(tmp) - 1);
    memcpy(tmp, s, (size_t)n);
    tmp[n] = 0;
    return text_w(f, tmp);
}

static int insert_text(char* buf, int cap, int at, const char* ins) {
    int len = (int)strlen(buf), n = (int)strlen(ins);
    if (len + n >= cap) n = cap - 1 - len;
    if (n <= 0) return 0;
    while (n > 0 && ((unsigned char)ins[n] & 0xC0) == 0x80) n--; // keep whole chars
    memmove(buf + at + n, buf + at, (size_t)(len - at + 1));
    memcpy(buf + at, ins, (size_t)n);
    return n;
}

int GWUI_TextInput(GWUI* ui, const char* id_str, char* buf, int cap, const char* placeholder) {
    GWUI_Rect r = GWUI_NextRect(ui);
    GWUI_Id id = GWUI_GetID(ui, id_str);
    int hovered, held;
    GWUI_Interact(ui, id, r, &hovered, &held);
    if (hovered) ui->cursor = GW_CURSOR_IBEAM;
    int result = 0;
    int pad = 8;
    GWUI_Rect inner = rect_make(r.x + pad, r.y, imax(0, r.w - 2 * pad), r.h);

    if (ui->pressed && GWUI_MouseOver(ui, r)) {
        if (ui->focus != id) { ui->focus = id; ui->text_scroll = 0; ui->again = 1; }
        ui->focus_kept = 1;
        ui->select_all = ui->dbl;
        // Caret at the click position.
        int target = ui->mx - inner.x + ui->text_scroll, best = 0, i = 0, len = (int)strlen(buf);
        while (i <= len) {
            if (prefix_width(ui->font, buf, i) > target) break;
            best = i;
            if (i == len) break;
            i = next_char(buf, i);
        }
        ui->caret = best;
        ui->caret_t0 = GW_GetTicks();
    }

    int focused = ui->focus == id;
    if (focused) {
        ui->focus_kept |= !ui->pressed || GWUI_MouseOver(ui, r);
        int len = (int)strlen(buf);
        if (ui->caret > len) ui->caret = len;
        int changed = 0;
        if (key_pressed(ui, 'A', GW_MOD_CTRL)) ui->select_all = 1;
        if (key_pressed(ui, 'C', GW_MOD_CTRL)) GW_SetClipboardText(ui->win, buf);
        if (key_pressed(ui, 'X', GW_MOD_CTRL) && ui->select_all) {
            GW_SetClipboardText(ui->win, buf);
            buf[0] = 0; ui->caret = 0; ui->select_all = 0; changed = 1;
        }
        if (key_pressed(ui, 'V', GW_MOD_CTRL)) {
            char* clip = GW_GetClipboardText(ui->win);
            if (clip) {
                for (char* p = clip; *p; p++) if (*p == '\r' || *p == '\n' || *p == '\t') *p = ' ';
                if (ui->select_all) { buf[0] = 0; ui->caret = 0; ui->select_all = 0; }
                ui->caret += insert_text(buf, cap, ui->caret, clip);
                changed = 1;
                GW_Free(clip);
            }
        }
        if (ui->text_len > 0) {
            if (ui->select_all) { buf[0] = 0; ui->caret = 0; ui->select_all = 0; }
            ui->caret += insert_text(buf, cap, ui->caret, ui->text_in);
            changed = 1;
        }
        for (int k = 0; k < ui->nkeys; k++) {
            int key = ui->keys[k] & 0xFFFF;
            int mods = ui->keys[k] >> 16;
            if (mods & GW_MOD_CTRL) continue;
            len = (int)strlen(buf);
            switch (key) {
                case GW_KEY_BACKSPACE:
                    if (ui->select_all) { buf[0] = 0; ui->caret = 0; ui->select_all = 0; changed = 1; }
                    else if (ui->caret > 0) {
                        int p = prev_char(buf, ui->caret);
                        memmove(buf + p, buf + ui->caret, (size_t)(len - ui->caret + 1));
                        ui->caret = p; changed = 1;
                    }
                    break;
                case GW_KEY_DELETE:
                    if (ui->select_all) { buf[0] = 0; ui->caret = 0; ui->select_all = 0; changed = 1; }
                    else if (ui->caret < len) {
                        int n = next_char(buf, ui->caret);
                        memmove(buf + ui->caret, buf + n, (size_t)(len - n + 1));
                        changed = 1;
                    }
                    break;
                case GW_KEY_LEFT:  ui->caret = ui->select_all ? 0 : prev_char(buf, ui->caret); ui->select_all = 0; break;
                case GW_KEY_RIGHT: ui->caret = ui->select_all ? len : next_char(buf, ui->caret); ui->select_all = 0; break;
                case GW_KEY_HOME:  ui->caret = 0; ui->select_all = 0; break;
                case GW_KEY_END:   ui->caret = len; ui->select_all = 0; break;
                case GW_KEY_ENTER: result |= GWUI_TEXT_SUBMITTED; break;
                case GW_KEY_ESCAPE: ui->focus = 0; ui->again = 1; break;
            }
        }
        if (changed) result |= GWUI_TEXT_CHANGED;
        if (ui->nkeys || ui->text_len) ui->caret_t0 = GW_GetTicks();
    }
    focused = ui->focus == id;

    GW_FillRoundRect(ui->win, r.x, r.y, r.w, r.h, ui->t.radius, ui->t.panel_alt);
    GW_DrawRoundRect(ui->win, r.x, r.y, r.w, r.h, ui->t.radius, focused ? 2 : 1, focused ? ui->t.accent : ui->t.border);

    int saved[4];
    current_clip(ui, saved);
    GWUI_Rect clip = rect_intersect(rect_make(saved[0], saved[1], saved[2], saved[3]), inner);
    GW_SetClipRect(ui->win, clip.x, clip.y, clip.w, clip.h);
    int a, d, lh;
    GW_GetFontMetrics(ui->font, &a, &d, &lh);
    int ty = r.y + (r.h - lh) / 2;
    if (!buf[0] && !focused && placeholder) {
        GW_DrawTextUTF8(ui->win, ui->font, inner.x, ty, placeholder, ui->t.text_dim);
    } else {
        int cx = focused ? prefix_width(ui->font, buf, ui->caret) : 0;
        if (focused) {
            // Keep the caret visible.
            if (cx - ui->text_scroll > inner.w - 2) ui->text_scroll = cx - inner.w + 2;
            if (cx - ui->text_scroll < 0) ui->text_scroll = cx;
        }
        int scroll = focused ? ui->text_scroll : 0;
        if (focused && ui->select_all && buf[0])
            GW_FillRectBlend(ui->win, inner.x - scroll, ty, text_w(ui->font, buf), lh, ui->t.selection | 0xFF000000u);
        GW_DrawTextUTF8(ui->win, ui->font, inner.x - scroll, ty, buf, ui->t.text);
        if (focused && ((GW_GetTicks() - ui->caret_t0) / 530) % 2 == 0)
            GW_FillRect(ui->win, inner.x - scroll + cx, ty, 1, lh, ui->t.text & 0x00FFFFFFu);
    }
    GW_SetClipRect(ui->win, saved[0], saved[1], saved[2], saved[3]);
    return result;
}

static void draw_chevron(GWUI* ui, int cx, int cy, uint32_t color) {
    GW_DrawLineAA(ui->win, cx - 4.0f, cy - 2.0f, (float)cx, cy + 2.0f, 1.6f, color);
    GW_DrawLineAA(ui->win, (float)cx, cy + 2.0f, cx + 4.0f, cy - 2.0f, 1.6f, color);
}

int GWUI_Select(GWUI* ui, const char* id_str, const char** items, int count, int* selected) {
    GWUI_Rect r = GWUI_NextRect(ui);
    GWUI_Id id = GWUI_GetID(ui, id_str);
    int changed = 0;
    if (ui->popup_chosen_id == id) {
        if (selected && *selected != ui->popup_chosen) { *selected = ui->popup_chosen; changed = 1; }
        ui->popup_chosen_id = 0;
    }
    int hovered, held;
    int clicked = GWUI_Interact(ui, id, r, &hovered, &held);
    if (hovered) ui->cursor = GW_CURSOR_HAND;
    if (clicked) { ui->popup = (ui->popup == id) ? 0 : id; ui->popup_pressing = 0; ui->again = 1; }

    int sel = selected ? *selected : -1;
    GW_FillRoundRect(ui->win, r.x, r.y, r.w, r.h, ui->t.radius, ui->t.panel_alt);
    GW_DrawRoundRect(ui->win, r.x, r.y, r.w, r.h, ui->t.radius, 1, ui->popup == id ? ui->t.accent : ui->t.border);
    hover_overlay(ui, r, hovered, held, ui->t.radius);
    const char* cur = (sel >= 0 && sel < count && items) ? items[sel] : "";
    GWUI_DrawText(ui, rect_make(r.x + 8, r.y, imax(0, r.w - 30), r.h), cur, NULL, ui->t.text, GWUI_ALIGN_LEFT);
    draw_chevron(ui, r.x + r.w - 14, r.y + r.h / 2, ui->t.text_dim);

    if (ui->popup == id) {
        ui->popup_drawn = 1;
        ui->popup_count = imin(count, POPUP_MAX);
        for (int i = 0; i < ui->popup_count; i++) copy_label(ui->popup_items[i], POPUP_ITEM_LEN, items[i] ? items[i] : "");
        ui->popup_sel = sel;
        int ih = ui->t.row_height - 4;
        int h = ui->popup_count * ih + 8;
        int ww, wh;
        GW_GetWindowSize(ui->win, &ww, &wh);
        int y = r.y + r.h + 4;
        if (y + h > wh && r.y - h - 4 >= 0) y = r.y - h - 4; // open upwards
        ui->popup_rect = rect_make(r.x, y, r.w, h);
    }
    return changed;
}

static void draw_popup_and_input(GWUI* ui) {
    if (!ui->popup) { ui->popup_valid_prev = 0; ui->popup_pressing = 0; return; }
    GWUI_Rect pr = ui->popup_rect;
    int ih = ui->t.row_height - 4;
    // shadow + body
    GW_FillRoundRect(ui->win, pr.x + 2, pr.y + 4, pr.w, pr.h, ui->t.radius, 0x60000000);
    GW_FillRoundRect(ui->win, pr.x, pr.y, pr.w, pr.h, ui->t.radius, ui->t.panel);
    GW_DrawRoundRect(ui->win, pr.x, pr.y, pr.w, pr.h, ui->t.radius, 1, ui->t.border);
    int inside = rect_contains(pr, ui->mx, ui->my);
    if (ui->pressed && inside) ui->popup_pressing = 1;
    for (int i = 0; i < ui->popup_count; i++) {
        GWUI_Rect ir = rect_make(pr.x + 4, pr.y + 4 + i * ih, pr.w - 8, ih);
        int over = rect_contains(ir, ui->mx, ui->my);
        if (i == ui->popup_sel) GW_FillRoundRect(ui->win, ir.x, ir.y, ir.w, ir.h, 4, ui->t.selection);
        if (over) { GW_FillRoundRect(ui->win, ir.x, ir.y, ir.w, ir.h, 4, ui->t.hover); ui->cursor = GW_CURSOR_HAND; }
        GWUI_DrawText(ui, rect_make(ir.x + 8, ir.y, ir.w - 16, ir.h), ui->popup_items[i], NULL, ui->t.text, GWUI_ALIGN_LEFT);
        if (over && ui->released && ui->popup_pressing) {
            ui->popup_chosen_id = ui->popup;
            ui->popup_chosen = i;
            ui->popup = 0;
            ui->again = 1;
        }
    }
    if (ui->released) ui->popup_pressing = 0;
    // A press outside the popup closes it (the owner's own click toggles it
    // in GWUI_Select, which ran before this).
    if (ui->popup && ui->pressed && !inside && ui->hot != ui->popup) { ui->popup = 0; ui->again = 1; }
    if (ui->popup && inside && ui->active == 0 && ui->pressed) ui->active = VOID_ID; // the press belongs to the popup
    ui->popup_rect_prev = pr;
    ui->popup_valid_prev = ui->popup != 0;
}

int GWUI_Tabs(GWUI* ui, const char** labels, int count, int* active) {
    GWUI_Rect r = GWUI_NextRect(ui);
    int changed = 0;
    int x = r.x;
    for (int i = 0; i < count; i++) {
        char vis[128];
        copy_label(vis, sizeof(vis), labels[i]);
        int w = text_w(ui->font, vis) + 2 * 14;
        GWUI_Rect tr = rect_make(x, r.y, w, r.h);
        GWUI_PushIDInt(ui, i);
        GWUI_Id id = GWUI_GetID(ui, labels[i]);
        GWUI_PopID(ui);
        int hovered, held;
        if (GWUI_Interact(ui, id, tr, &hovered, &held) && active && *active != i) { *active = i; changed = 1; ui->again = 1; }
        if (hovered) ui->cursor = GW_CURSOR_HAND;
        int on = active && *active == i;
        hover_overlay(ui, tr, hovered && !on, held, 4);
        GWUI_DrawText(ui, tr, vis, NULL, on ? ui->t.text : ui->t.text_dim, GWUI_ALIGN_CENTER);
        if (on) GW_FillRoundRect(ui->win, tr.x + 6, tr.y + tr.h - 3, tr.w - 12, 3, 1, ui->t.accent);
        x += w + 2;
    }
    GW_FillRectBlend(ui->win, r.x, r.y + r.h - 1, r.w, 1, ui->t.border);
    return changed;
}

int GWUI_Selectable(GWUI* ui, const char* label, int selected) {
    GWUI_Rect r = GWUI_NextRect(ui);
    GWUI_Id id = GWUI_GetID(ui, label);
    int hovered, held;
    int clicked = GWUI_Interact(ui, id, r, &hovered, &held);
    if (hovered) ui->cursor = GW_CURSOR_HAND;
    if (selected) GW_FillRoundRect(ui->win, r.x, r.y, r.w, r.h, 4, ui->t.selection);
    hover_overlay(ui, r, hovered, held, 4);
    GWUI_DrawText(ui, rect_make(r.x + 8, r.y, imax(0, r.w - 16), r.h), label, NULL, ui->t.text, GWUI_ALIGN_LEFT);
    return clicked;
}

void GWUI_Icon(GWUI* ui, GW_Image* img, int size, uint32_t tint) {
    GWUI_Rect r = GWUI_NextRect(ui);
    if (!img) return;
    if (size <= 0) size = imin(r.w, r.h);
    int x = r.x + (r.w - size) / 2, y = r.y + (r.h - size) / 2;
    if (tint) GW_DrawImageTinted(ui->win, img, x, y, size, size, tint);
    else GW_DrawImage(ui->win, img, x, y, size, size, 0, 0, 0, 0, 0, 0);
}

void GWUI_Badge(GWUI* ui, const char* text, uint32_t color) {
    GWUI_Rect r = GWUI_NextRect(ui);
    GW_Font* f = ui->small ? ui->small : ui->font;
    char vis[128];
    copy_label(vis, sizeof(vis), text);
    int tw = text_w(f, vis);
    int h = ui->small_line_h + 6;
    int w = imin(r.w, tw + 16);
    GWUI_Rect b = rect_make(r.x, r.y + (r.h - h) / 2, w, h);
    GW_FillRoundRect(ui->win, b.x, b.y, b.w, b.h, h / 2, with_alpha(color, 0x33));
    GWUI_DrawText(ui, b, vis, f, color | 0xFF000000u, GWUI_ALIGN_CENTER);
}

void GWUI_Progress(GWUI* ui, float value, uint32_t color) {
    GWUI_Rect r = GWUI_NextRect(ui);
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    int h = 6;
    int y = r.y + (r.h - h) / 2;
    GW_FillRoundRect(ui->win, r.x, y, r.w, h, h / 2, ui->t.panel_alt);
    int fw = (int)(r.w * value + 0.5f);
    if (fw > 0) GW_FillRoundRect(ui->win, r.x, y, imax(fw, h), h, h / 2, color ? color : ui->t.accent);
}
