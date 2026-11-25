// X11 dialogs drawn with LibGW + gw_ui (feat/portmaster-ui): message box and
// open/save file dialogs, without external libraries. Built unless
// GW_X11_EXTERNAL_DIALOGS (CMake NATIVEGUI_X11_DIALOGS=ON) selects the
// MessageBox-X11 + sofd versions in gw_x11.c.
//
// Each dialog is its own window with a modal loop: events of other windows
// that arrive meanwhile are kept and re-queued when the dialog closes
// (mouse motion is dropped). Same results as the Win32 versions:
//   GW_ShowMessageBox: index of the clicked button; Enter = the first one;
//     Esc or closing the window = the last one (Win32: Cancel / No / OK).
//   GW_ShowOpenFileDialog / GW_ShowSaveFileDialog: a malloc'd UTF-8 path
//     (free with GW_Free or free), or NULL if cancelled. filter uses the
//     Win32 form "Images|*.png;*.jpg|All files|*.*"; NULL = all files.
// Font: $GW_FONT (a .ttf path) or DejaVu / Noto / Liberation in the usual
// places; with none, the dialogs print to stderr and return "cancelled".
#if !defined(_WIN32) && !defined(GW_X11_EXTERNAL_DIALOGS)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // FNM_CASEFOLD, realpath, strdup
#endif
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#include "gw.h"
#include "gw_internal.h"
#include "gw_ui.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0 // non-GNU libc: case-sensitive filters
#endif

/* ---- shared ---------------------------------------------------------------- */

static const char* FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",   // Debian, Ubuntu
    "/usr/share/fonts/TTF/DejaVuSans.ttf",               // Arch
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf", // Fedora, RHEL
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",            // openSUSE, Alpine
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
};

static GW_Font* dialog_font(float size) {
    const char* env = getenv("GW_FONT");
    if (env && *env && access(env, R_OK) == 0) {
        GW_Font* f = GW_LoadFont(env, size);
        if (f) return f;
    }
    for (size_t i = 0; i < sizeof(FONT_CANDIDATES) / sizeof(FONT_CANDIDATES[0]); i++) {
        if (access(FONT_CANDIDATES[i], R_OK) != 0) continue;
        GW_Font* f = GW_LoadFont(FONT_CANDIDATES[i], size);
        if (f) return f;
    }
    return NULL;
}

static char* wide_to_utf8_alloc(const wchar_t* w) {
    if (!w) w = L"";
    size_t cap = wcslen(w) * 4 + 1;
    char* s = (char*)malloc(cap);
    if (s) GW_WideToUTF8(w, s, (int)cap);
    return s;
}

#define DEFERRED_MAX 256
typedef struct { GW_Event ev[DEFERRED_MAX]; int n; } Deferred;

static void defer_event(Deferred* d, const GW_Event* e) {
    if (e->type == GW_EVENT_MOUSE_MOTION) return;
    if (d->n < DEFERRED_MAX) d->ev[d->n++] = *e;
}

// Frame callback: returns DLG_RUNNING to keep going, anything else ends the
// dialog with that value.
#define DLG_RUNNING (-1000)
typedef int (*DialogFrame)(GWUI* ui, void* ctx, const GW_Event* key);

// Runs the modal loop of dialog window w. Escape / close -> cancel_value.
static int run_modal(GW_Window* w, GWUI* ui, DialogFrame frame, void* ctx, int cancel_value) {
    Deferred* d = (Deferred*)calloc(1, sizeof(Deferred));
    int result = DLG_RUNNING;
    int dirty = 1;
    GW_Event ev, key;
    memset(&key, 0, sizeof(key));
    GW_ShowWindow(w, 1);
    GW_FocusWindow(w);
    while (result == DLG_RUNNING) {
        if (dirty) {
            do {
                GWUI_Begin(ui);
                int r = frame(ui, ctx, key.type ? &key : NULL);
                if (r != DLG_RUNNING) result = r;
                key.type = GW_EVENT_NONE;
            } while (GWUI_End(ui) && result == DLG_RUNNING);
            GW_Present(w);
            dirty = 0;
            if (result != DLG_RUNNING) break;
        }
        if (!GW_WaitEventTimeout(&ev, 500)) {
            if (GWUI_TextFocused(ui)) dirty = 1; // caret blink
            continue;
        }
        if (ev.window != w) {
            if (d) defer_event(d, &ev);
            continue;
        }
        if (ev.type == GW_EVENT_QUIT) { result = cancel_value; break; }
        if (ev.type == GW_EVENT_KEY_DOWN && ev.key.keycode == GW_KEY_ESCAPE) { result = cancel_value; break; }
        if (ev.type == GW_EVENT_KEY_DOWN && ev.key.keycode == GW_KEY_ENTER && !GWUI_TextFocused(ui)) key = ev;
        GWUI_HandleEvent(ui, &ev);
        dirty = 1;
    }
    GW_ShowWindow(w, 0);
    if (d) {
        for (int i = 0; i < d->n; i++) gw_x11_push_event(d->ev[i]);
        free(d);
    }
    return result;
}

/* ---- message box --------------------------------------------------------- */

#define MB_W 440

typedef struct {
    const char* text;
    char** labels;
    int n;
} MsgBox;

static int msgbox_frame(GWUI* ui, void* ctx, const GW_Event* key) {
    MsgBox* m = (MsgBox*)ctx;
    const GWUI_Theme* t = GWUI_GetTheme(ui);
    if (key && key->key.keycode == GW_KEY_ENTER) return 0;
    GWUI_Paragraph(ui, m->text, NULL, t->text);
    GWUI_Space(ui, t->spacing);
    int widths[9];
    int n = m->n < 8 ? m->n : 8;
    widths[0] = 0; // flexible gap, buttons go right
    for (int i = 0; i < n; i++) {
        int tw = GWUI_TextWidth(ui, m->labels[i], NULL) + 32;
        widths[i + 1] = tw < 84 ? 84 : tw;
    }
    GWUI_Row(ui, n + 1, widths, 32);
    (void)GWUI_NextRect(ui);
    for (int i = 0; i < n; i++) {
        GWUI_PushIDInt(ui, i);
        if (GWUI_ButtonEx(ui, m->labels[i], i == 0 ? GWUI_BUTTON_PRIMARY : GWUI_BUTTON_NORMAL, NULL)) {
            GWUI_PopID(ui);
            return i;
        }
        GWUI_PopID(ui);
    }
    return DLG_RUNNING;
}

int GW_ShowMessageBox(GW_Window* parent, const char* title, const wchar_t* text, const wchar_t** buttons, int num_buttons) {
    (void)parent;
    char* body = wide_to_utf8_alloc(text);
    MsgBox m;
    memset(&m, 0, sizeof(m));
    m.text = body ? body : "";
    static const wchar_t* ok[] = { L"OK" };
    if (!buttons || num_buttons <= 0) { buttons = ok; num_buttons = 1; }
    m.n = num_buttons;
    m.labels = (char**)calloc((size_t)num_buttons, sizeof(char*));
    for (int i = 0; m.labels && i < num_buttons; i++) m.labels[i] = wide_to_utf8_alloc(buttons[i]);
    int cancel = num_buttons - 1;

    int result = cancel;
    GW_Font* font = dialog_font(15.0f);
    if (!font || !m.labels) {
        fprintf(stderr, "[%s] %s\n(no font for the X11 message box: set GW_FONT)\n", title ? title : "", m.text);
        result = -1;
        goto done;
    }
    // Measure with a hidden window, then make the real one with the height
    // the text needs.
    GW_Window* probe = GW_CreateWindow(title ? title : "", MB_W, 100);
    if (!probe) { result = -1; goto done; }
    GWUI* pui = GWUI_Create(probe, font, NULL);
    const GWUI_Theme* t = GWUI_GetTheme(pui);
    int text_h = GWUI_MeasureWrapped(pui, m.text, NULL, MB_W - 2 * t->padding);
    int h = t->padding * 2 + text_h + t->spacing * 2 + 32;
    GWUI_Destroy(pui);
    GW_DestroyWindow(probe);

    GW_Window* w = GW_CreateWindow(title ? title : "", MB_W, h < 120 ? 120 : h);
    if (!w) { result = -1; goto done; }
    GW_SetMinWindowSize(w, MB_W, h < 120 ? 120 : h);
    GWUI* ui = GWUI_Create(w, font, NULL);
    result = run_modal(w, ui, msgbox_frame, &m, cancel);
    GWUI_Destroy(ui);
    GW_DestroyWindow(w);
done:
    if (font) GW_FreeFont(font);
    for (int i = 0; m.labels && i < num_buttons; i++) free(m.labels[i]);
    free(m.labels);
    free(body);
    return result;
}

/* ---- file dialogs ---------------------------------------------------------- */

typedef struct { char* name; int is_dir; } Entry;

#define MAX_FILTERS 16
typedef struct {
    int save;
    char dir[PATH_MAX];
    char name[512];
    char error[256];
    Entry* entries;
    int n_entries, cap_entries;
    int selected;
    // filters: descriptions for the Select, patterns ("*.png;*.jpg")
    const char* filter_desc[MAX_FILTERS];
    char* filter_pat[MAX_FILTERS];
    int n_filters, filter;
    char* filter_buf;
    char* result;
    GW_Window* win;
    int focused_once;
} FileDlg;

static int entry_cmp(const void* a, const void* b) {
    const Entry* x = (const Entry*)a;
    const Entry* y = (const Entry*)b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir; // folders first
    if (strcmp(x->name, "..") == 0) return -1;
    if (strcmp(y->name, "..") == 0) return 1;
    return strcasecmp(x->name, y->name);
}

static int matches_filter(FileDlg* f, const char* name) {
    if (f->n_filters == 0) return 1;
    char pats[512];
    snprintf(pats, sizeof(pats), "%s", f->filter_pat[f->filter]);
    for (char* p = strtok(pats, ";"); p; p = strtok(NULL, ";")) {
        while (*p == ' ') p++;
        if (strcmp(p, "*") == 0 || strcmp(p, "*.*") == 0) return 1;
        if (fnmatch(p, name, FNM_CASEFOLD) == 0) return 1;
    }
    return 0;
}

static void free_entries(FileDlg* f) {
    for (int i = 0; i < f->n_entries; i++) free(f->entries[i].name);
    f->n_entries = 0;
}

static void load_dir(FileDlg* f) {
    free_entries(f);
    f->selected = -1;
    f->error[0] = 0;
    DIR* d = opendir(f->dir);
    if (!d) {
        snprintf(f->error, sizeof(f->error), "Cannot open this folder");
        return;
    }
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        const char* n = de->d_name;
        if (strcmp(n, ".") == 0) continue;
        if (n[0] == '.' && strcmp(n, "..") != 0) continue; // hidden
        if (strcmp(n, "..") == 0 && strcmp(f->dir, "/") == 0) continue;
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", strcmp(f->dir, "/") == 0 ? "" : f->dir, n);
        struct stat st;
        int is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        if (!is_dir && !matches_filter(f, n)) continue;
        if (f->n_entries == f->cap_entries) {
            int cap = f->cap_entries ? f->cap_entries * 2 : 64;
            Entry* e = (Entry*)realloc(f->entries, (size_t)cap * sizeof(Entry));
            if (!e) break;
            f->entries = e;
            f->cap_entries = cap;
        }
        f->entries[f->n_entries].name = strdup(n);
        f->entries[f->n_entries].is_dir = is_dir;
        f->n_entries++;
    }
    closedir(d);
    qsort(f->entries, (size_t)f->n_entries, sizeof(Entry), entry_cmp);
}

// Enters subdirectory name ("..": up). Keeps dir absolute and normalised.
static void enter_dir(FileDlg* f, const char* name) {
    char next[PATH_MAX];
    if (strcmp(name, "..") == 0) {
        snprintf(next, sizeof(next), "%s", f->dir);
        char* slash = strrchr(next, '/');
        if (slash && slash != next) *slash = 0;
        else snprintf(next, sizeof(next), "/");
    } else if (name[0] == '/') {
        snprintf(next, sizeof(next), "%s", name);
    } else {
        snprintf(next, sizeof(next), "%s/%s", strcmp(f->dir, "/") == 0 ? "" : f->dir, name);
    }
    char real[PATH_MAX];
    if (realpath(next, real)) snprintf(f->dir, sizeof(f->dir), "%s", real);
    else snprintf(f->dir, sizeof(f->dir), "%s", next);
    load_dir(f);
}

// Tries to finish with the typed / selected name. Returns 1 when done.
static int accept(FileDlg* f) {
    if (!f->name[0]) return 0;
    char full[PATH_MAX];
    if (f->name[0] == '/') snprintf(full, sizeof(full), "%s", f->name);
    else snprintf(full, sizeof(full), "%s/%s", strcmp(f->dir, "/") == 0 ? "" : f->dir, f->name);
    struct stat st;
    int exists = stat(full, &st) == 0;
    if (exists && S_ISDIR(st.st_mode)) { // a folder name: go there
        enter_dir(f, full);
        f->name[0] = 0;
        return 0;
    }
    if (!f->save) {
        if (!exists) { snprintf(f->error, sizeof(f->error), "File not found"); return 0; }
    } else {
        // The folder must exist; an existing file asks first.
        char parent[PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", full);
        char* slash = strrchr(parent, '/');
        if (slash) { if (slash == parent) slash[1] = 0; else *slash = 0; }
        struct stat ps;
        if (stat(parent, &ps) != 0 || !S_ISDIR(ps.st_mode)) {
            snprintf(f->error, sizeof(f->error), "Folder not found");
            return 0;
        }
        if (exists) {
            static const wchar_t* yn[] = { L"Replace", L"Cancel" };
            char msg8[sizeof(f->name) + 64];
            wchar_t msg[sizeof(f->name) + 64];
            snprintf(msg8, sizeof(msg8), "%s already exists. Replace it?", f->name);
            GW_UTF8ToWide(msg8, msg, (int)(sizeof(msg) / sizeof(msg[0])));
            if (GW_ShowMessageBox(f->win, "Confirm", msg, yn, 2) != 0) return 0;
        }
    }
    f->result = strdup(full);
    return 1;
}

static int filedlg_frame(GWUI* ui, void* ctx, const GW_Event* key) {
    FileDlg* f = (FileDlg*)ctx;
    const GWUI_Theme* t = GWUI_GetTheme(ui);
    int w, h;
    GW_GetWindowSize(GWUI_GetWindow(ui), &w, &h);
    int pad = t->padding, sp = t->spacing, rh = 32;

    if (key && key->key.keycode == GW_KEY_ENTER) {
        if (f->selected >= 0 && f->selected < f->n_entries && f->entries[f->selected].is_dir) {
            char name[PATH_MAX];
            snprintf(name, sizeof(name), "%s", f->entries[f->selected].name);
            enter_dir(f, name);
            return DLG_RUNNING;
        }
        if (accept(f)) return 1;
    }

    // Top: folder path and "Up".
    GWUI_BeginPanel(ui, (GWUI_Rect){ 0, 0, w, pad * 2 + rh }, 0);
    int top[2] = { 0, 70 };
    GWUI_Row(ui, 2, top, rh);
    GWUI_LabelDim(ui, f->dir);
    if (GWUI_Button(ui, "Up##up")) enter_dir(f, "..");
    GWUI_EndPanel(ui);

    // Bottom: name + filter, error line, buttons.
    int bottom_h = pad * 2 + rh * 2 + sp + 18 + sp;
    GWUI_Rect list = { pad, pad * 2 + rh, w - 2 * pad, h - (pad * 2 + rh) - bottom_h };
    if (list.h < 40) list.h = 40;

    GWUI_BeginScroll(ui, "files", list);
    int clicked_dir = -1;
    for (int i = 0; i < f->n_entries; i++) {
        char label[600];
        snprintf(label, sizeof(label), "%s%s##e", f->entries[i].name, f->entries[i].is_dir ? "/" : "");
        GWUI_PushIDInt(ui, i);
        if (GWUI_Selectable(ui, label, i == f->selected)) {
            f->selected = i;
            f->error[0] = 0;
            if (f->entries[i].is_dir) {
                if (GWUI_LastClickWasDouble(ui)) clicked_dir = i;
            } else {
                snprintf(f->name, sizeof(f->name), "%s", f->entries[i].name);
                if (GWUI_LastClickWasDouble(ui) && accept(f)) { GWUI_PopID(ui); GWUI_EndScroll(ui); return 1; }
            }
        }
        GWUI_PopID(ui);
    }
    GWUI_EndScroll(ui);
    if (clicked_dir >= 0) {
        char name[PATH_MAX];
        snprintf(name, sizeof(name), "%s", f->entries[clicked_dir].name);
        enter_dir(f, name);
        GWUI_RequestFrame(ui);
    }

    GWUI_BeginPanel(ui, (GWUI_Rect){ 0, h - bottom_h, w, bottom_h }, 0);
    if (!f->focused_once) { GWUI_FocusText(ui, "name"); f->focused_once = 1; }
    if (f->n_filters > 0) {
        int row[2] = { 0, 200 };
        GWUI_Row(ui, 2, row, rh);
    } else {
        GWUI_Row(ui, 1, NULL, rh);
    }
    if (GWUI_TextInput(ui, "name", f->name, sizeof(f->name), f->save ? "File name" : "File name or path") & GWUI_TEXT_SUBMITTED) {
        if (accept(f)) { GWUI_EndPanel(ui); return 1; }
    }
    if (f->n_filters > 0) {
        int before = f->filter;
        GWUI_Select(ui, "filter", f->filter_desc, f->n_filters, &f->filter);
        if (f->filter != before) load_dir(f);
    }
    GWUI_Row(ui, 1, NULL, 18);
    GWUI_LabelEx(ui, f->error, NULL, t->danger, GWUI_ALIGN_LEFT);
    int btns[3] = { 0, 96, 96 };
    GWUI_Row(ui, 3, btns, rh);
    (void)GWUI_NextRect(ui);
    if (GWUI_Button(ui, "Cancel##cancel")) { GWUI_EndPanel(ui); return 0; }
    if (GWUI_ButtonEx(ui, f->save ? "Save##ok" : "Open##ok", GWUI_BUTTON_PRIMARY, NULL)) {
        if (f->selected >= 0 && f->selected < f->n_entries && f->entries[f->selected].is_dir && !f->name[0]) {
            char name[PATH_MAX];
            snprintf(name, sizeof(name), "%s", f->entries[f->selected].name);
            enter_dir(f, name);
        } else if (accept(f)) {
            GWUI_EndPanel(ui);
            return 1;
        }
    }
    GWUI_EndPanel(ui);
    return DLG_RUNNING;
}

static void parse_filter(FileDlg* f, const char* filter) {
    if (!filter || !*filter) return;
    f->filter_buf = strdup(filter);
    if (!f->filter_buf) return;
    char* save = NULL;
    char* desc = strtok_r(f->filter_buf, "|", &save);
    while (desc && f->n_filters < MAX_FILTERS) {
        char* pat = strtok_r(NULL, "|", &save);
        if (!pat) break;
        f->filter_desc[f->n_filters] = desc;
        f->filter_pat[f->n_filters] = pat;
        f->n_filters++;
        desc = strtok_r(NULL, "|", &save);
    }
}

static char* file_dialog(GW_Window* parent, const char* title, const char* filter, int save) {
    (void)parent;
    FileDlg* f = (FileDlg*)calloc(1, sizeof(FileDlg));
    if (!f) return NULL;
    f->save = save;
    f->selected = -1;
    parse_filter(f, filter);
    if (!getcwd(f->dir, sizeof(f->dir))) snprintf(f->dir, sizeof(f->dir), "/");
    char* result = NULL;
    GW_Font* font = dialog_font(15.0f);
    if (!font) {
        fprintf(stderr, "GW_Show%sFileDialog: no font for the X11 dialog (set GW_FONT)\n", save ? "Save" : "Open");
        goto done;
    }
    f->win = GW_CreateWindow(title ? title : (save ? "Save" : "Open"), 640, 460);
    if (!f->win) goto done;
    GW_SetMinWindowSize(f->win, 420, 300);
    GWUI* ui = GWUI_Create(f->win, font, NULL);
    load_dir(f);
    if (run_modal(f->win, ui, filedlg_frame, f, 0) == 1) result = f->result;
    else free(f->result);
    GWUI_Destroy(ui);
    GW_DestroyWindow(f->win);
done:
    if (font) GW_FreeFont(font);
    free_entries(f);
    free(f->entries);
    free(f->filter_buf);
    free(f);
    return result;
}

char* GW_ShowOpenFileDialog(GW_Window* parent, const char* title, const char* filter) {
    return file_dialog(parent, title, filter, 0);
}

char* GW_ShowSaveFileDialog(GW_Window* parent, const char* title, const char* filter) {
    return file_dialog(parent, title, filter, 1);
}

#endif // !_WIN32 && !GW_X11_EXTERNAL_DIALOGS
