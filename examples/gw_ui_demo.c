// Interactive demo of gw_ui (feat/portmaster-ui): a Portmaster-like layout
// with fake data. Close the window to quit; the tray icon has a menu.
//
// Build (Developer prompt, repo root, after building the library):
//   cl /nologo /MD /Iinclude examples\gw_ui_demo.c build\nativegui.lib user32.lib gdi32.lib comdlg32.lib shell32.lib /Fe:build\gw_ui_demo.exe
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gw.h"
#include "gw_ui.h"

#define TIMER_CARET 1

static const char* SHIELD_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#ffffff' d='M12 2 4 5v6c0 5 3.4 9.7 8 11 4.6-1.3 8-6 8-11V5z'/></svg>";
static const char* GLOBE_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<circle cx='12' cy='12' r='9' fill='none' stroke='#ffffff' stroke-width='2'/>"
    "<path d='M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18' fill='none' stroke='#ffffff' stroke-width='2'/></svg>";
static const char* GEAR_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<circle cx='12' cy='12' r='3.5' fill='none' stroke='#ffffff' stroke-width='2'/>"
    "<path d='M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1 7 17M17 7l2.1-2.1' stroke='#ffffff' stroke-width='2'/></svg>";

typedef struct { const char* app; const char* domain; int blocked; int bytes; } Conn;
static const Conn CONNS[] = {
    { "firefox.exe", "www.wikipedia.org", 0, 48210 },
    { "firefox.exe", "tracker.example-ads.com", 1, 0 },
    { "svchost.exe", "settings-win.data.microsoft.com", 1, 0 },
    { "spotify.exe", "apresolve.spotify.com", 0, 1220 },
    { "code.exe", "update.code.visualstudio.com", 0, 9400 },
    { "steam.exe", "api.steampowered.com", 0, 30311 },
    { "discord.exe", "gateway.discord.gg", 0, 77120 },
    { "chrome.exe", "doubleclick.net", 1, 0 },
    { "git.exe", "github.com", 0, 512000 },
    { "odin.exe", "odin-lang.org", 0, 2048 },
    { "teams.exe", "teams.microsoft.com", 0, 4500 },
    { "unknown.exe", "evil-c2.example", 1, 0 },
};
#define NCONN ((int)(sizeof(CONNS) / sizeof(CONNS[0])))

typedef struct {
    int page;           // 0 monitor, 1 spn, 2 settings
    int tab;
    char filter[64];
    int selected;
    int spn_on, block_ads, block_trackers, prompt_new;
    int default_action;
    int exit_country;
    int interception;
} State;

static void draw(GWUI* ui, State* s, GW_Image** icons, GW_Font* big) {
    const GWUI_Theme* t = GWUI_GetTheme(ui);
    int w, h;
    GW_GetWindowSize(GWUI_GetWindow(ui), &w, &h);
    do {
        GWUI_Begin(ui);
        // Sidebar
        GWUI_BeginPanel(ui, (GWUI_Rect){ 0, 0, 64, h }, t->panel);
        static const char* names[] = { "##nav-monitor", "##nav-spn", "##nav-settings" };
        int one[1] = { 0 };
        GWUI_Row(ui, 1, one, 44);
        for (int i = 0; i < 3; i++) {
            if (GWUI_ButtonEx(ui, names[i], s->page == i ? GWUI_BUTTON_PRIMARY : GWUI_BUTTON_FLAT, icons[i])) s->page = i;
        }
        GWUI_EndPanel(ui);

        GWUI_BeginPanel(ui, (GWUI_Rect){ 64, 0, w - 64, h }, 0);
        if (s->page == 0) {
            int head[3] = { 0, 160, 110 };
            GWUI_Row(ui, 3, head, 36);
            GWUI_LabelEx(ui, "Network Activity", big, t->text, GWUI_ALIGN_LEFT);
            GWUI_TextInput(ui, "filter", s->filter, sizeof(s->filter), "Filter...");
            int on = s->interception;
            if (GWUI_ButtonEx(ui, on ? "Pause##ic" : "Resume##ic", on ? GWUI_BUTTON_NORMAL : GWUI_BUTTON_PRIMARY, NULL)) s->interception = !s->interception;
            static const char* tabs[] = { "All", "Allowed", "Blocked" };
            GWUI_Row(ui, 1, NULL, 34);
            GWUI_Tabs(ui, tabs, 3, &s->tab);
            GWUI_Rect area = GWUI_NextRect(ui);
            GWUI_Rect cr = GWUI_ContentRect(ui);
            area.h = cr.y + cr.h - area.y;
            GWUI_BeginScroll(ui, "conns", area);
            int cols[4] = { 140, 0, 90, 80 };
            for (int i = 0; i < NCONN; i++) {
                const Conn* c = &CONNS[i];
                if (s->tab == 1 && c->blocked) continue;
                if (s->tab == 2 && !c->blocked) continue;
                if (s->filter[0] && !strstr(c->domain, s->filter) && !strstr(c->app, s->filter)) continue;
                GWUI_PushIDInt(ui, i);
                GWUI_Row(ui, 4, cols, 32);
                GWUI_Rect rowr = GWUI_NextRect(ui);
                GWUI_Rect full = { rowr.x, rowr.y, GWUI_ContentRect(ui).w, rowr.h };
                int hov, held;
                if (GWUI_Interact(ui, GWUI_GetID(ui, "row"), full, &hov, &held)) s->selected = i;
                if (s->selected == i) GW_FillRoundRect(GWUI_GetWindow(ui), full.x, full.y, full.w, full.h, 4, t->selection);
                else if (hov) GW_FillRoundRect(GWUI_GetWindow(ui), full.x, full.y, full.w, full.h, 4, t->hover);
                GWUI_DrawText(ui, (GWUI_Rect){ rowr.x + 8, rowr.y, rowr.w - 8, rowr.h }, c->app, NULL, t->text, GWUI_ALIGN_LEFT);
                GWUI_LabelDim(ui, c->domain);
                GWUI_Badge(ui, c->blocked ? "Blocked" : "Allowed", c->blocked ? t->danger : t->success);
                char kb[32];
                if (c->bytes) snprintf(kb, sizeof(kb), "%.1f KB", c->bytes / 1024.0); else snprintf(kb, sizeof(kb), "-");
                GWUI_LabelEx(ui, kb, NULL, t->text_dim, GWUI_ALIGN_RIGHT);
                GWUI_PopID(ui);
            }
            GWUI_EndScroll(ui);
        } else if (s->page == 1) {
            GWUI_Row(ui, 1, NULL, 36);
            GWUI_LabelEx(ui, "Safing Privacy Network", big, t->text, GWUI_ALIGN_LEFT);
            GWUI_LabelDim(ui, "Your own hubs: route app traffic through a multi-hop path.");
            int half[2] = { 0, 0 };
            GWUI_Row(ui, 2, half, 30);
            GWUI_Toggle(ui, "Use SPN", &s->spn_on);
            static const char* countries[] = { "Automatic", "Germany (vps)", "Netherlands", "Argentina" };
            GWUI_Select(ui, "exit", countries, 4, &s->exit_country);
            GWUI_Row(ui, 1, NULL, 30);
            GWUI_Label(ui, s->spn_on ? "Connected: 2 hops" : "Disconnected");
            GWUI_Progress(ui, s->spn_on ? 0.72f : 0.0f, t->success);
        } else {
            GWUI_Row(ui, 1, NULL, 36);
            GWUI_LabelEx(ui, "Settings", big, t->text, GWUI_ALIGN_LEFT);
            GWUI_Row(ui, 1, NULL, 30);
            GWUI_Toggle(ui, "Block ads", &s->block_ads);
            GWUI_Toggle(ui, "Block trackers", &s->block_trackers);
            GWUI_Checkbox(ui, "Prompt for new apps", &s->prompt_new);
            GWUI_Separator(ui);
            int two[2] = { 200, 220 };
            GWUI_Row(ui, 2, two, 30);
            GWUI_Label(ui, "Default network action");
            static const char* acts[] = { "Allow", "Block", "Prompt" };
            GWUI_Select(ui, "defact", acts, 3, &s->default_action);
        }
        GWUI_EndPanel(ui);
    } while (GWUI_End(ui));
    GW_Present(GWUI_GetWindow(ui));
}

// Writes the backbuffer as a 32-bit BMP (for --shot).
static int save_bmp(GW_Window* win, const char* path) {
    int w, h;
    uint32_t* px = GW_GetBackbuffer(win, &w, &h);
    FILE* f = fopen(path, "wb");
    if (!f || !px) return 0;
    unsigned int data = (unsigned int)(w * h * 4);
    unsigned char hdr[54] = { 'B', 'M' };
    unsigned int file = 54 + data;
    memcpy(hdr + 2, &file, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    int nh = -h; // top-down
    memcpy(hdr + 22, &nh, 4);
    hdr[26] = 1;
    hdr[28] = 32;
    memcpy(hdr + 34, &data, 4);
    fwrite(hdr, 1, 54, f);
    for (int i = 0; i < w * h; i++) { uint32_t p = px[i] | 0xFF000000u; fwrite(&p, 4, 1, f); }
    fclose(f);
    return 1;
}

int main(int argc, char** argv) {
    const char* shot = (argc > 2 && strcmp(argv[1], "--shot") == 0) ? argv[2] : NULL;
    int shot_page = argc > 3 ? atoi(argv[3]) : 0;
    if (!shot) GW_EnableHighDpi();
    GW_Window* win = GW_CreateWindow("gw_ui demo", 980, 640);
    if (!win) return 1;
    float scale = GW_GetDpiScale(win);
    GW_Font* font = GW_LoadFont("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f * scale);
    GW_Font* small = GW_LoadFont("C:\\Windows\\Fonts\\segoeui.ttf", 12.0f * scale);
    GW_Font* big = GW_LoadFont("C:\\Windows\\Fonts\\segoeuib.ttf", 20.0f * scale);
    GWUI* ui = GWUI_Create(win, font, small);
    GW_Image* icons[3] = {
        GW_LoadSVGFromMemory(SHIELD_SVG, (int)strlen(SHIELD_SVG), 64, 64),
        GW_LoadSVGFromMemory(GLOBE_SVG, (int)strlen(GLOBE_SVG), 64, 64),
        GW_LoadSVGFromMemory(GEAR_SVG, (int)strlen(GEAR_SVG), 64, 64),
    };
    GW_SetWindowIcon(win, icons[0]);
    GW_SetMinWindowSize(win, 640, 420);
    State s;
    memset(&s, 0, sizeof(s));
    s.interception = 1;
    s.block_ads = 1;
    s.selected = -1;
    if (shot) {
        // Render one frame of a page without showing anything, then exit.
        s.page = shot_page;
        s.selected = 1;
        s.spn_on = 1;
        draw(ui, &s, icons, big);
        int ok = save_bmp(win, shot);
        GWUI_Destroy(ui);
        GW_DestroyWindow(win);
        return ok ? 0 : 1;
    }
    GW_TrayIcon* tray = GW_CreateTrayIcon(win, 1, icons[0], "gw_ui demo");
    GW_ShowWindow(win, 1);
    GW_SetTimer(win, TIMER_CARET, 530);

    GW_Event ev;
    int running = 1;
    draw(ui, &s, icons, big);
    while (running && GW_WaitEvent(&ev)) {
        GWUI_HandleEvent(ui, &ev);
        switch (ev.type) {
            case GW_EVENT_QUIT: running = 0; continue;
            case GW_EVENT_TIMER: if (!GWUI_TextFocused(ui)) continue; break; // caret blink only
            case GW_EVENT_TRAY:
                if (ev.tray.action == GW_TRAY_RIGHT_CLICK) {
                    const char* items[] = { "Open", "Pause interception", "", "Quit" };
                    int r = GW_ShowPopupMenu(win, items, 4);
                    if (r == 0) GW_FocusWindow(win);
                    if (r == 1) s.interception = 0;
                    if (r == 3) running = 0;
                } else {
                    GW_FocusWindow(win);
                }
                break;
            default: break;
        }
        draw(ui, &s, icons, big);
    }
    GW_DestroyTrayIcon(tray);
    for (int i = 0; i < 3; i++) GW_FreeImage(icons[i]);
    GWUI_Destroy(ui);
    GW_FreeFont(font); GW_FreeFont(small); GW_FreeFont(big);
    GW_DestroyWindow(win);
    return 0;
}
