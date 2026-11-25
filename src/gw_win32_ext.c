// Win32 side of the extensions (feat/portmaster-ui): user events, timers,
// clipboard, cursor, high DPI, window icon/size/focus, tray icon and popup
// menu. The window procedure part (new messages) lives in gw_win32.c.
#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include "gw_internal.h"

/* ---- user events and timers --------------------------------------------- */

int GW_PostUserEvent(GW_Window* win, int code, void* data) {
    if (!win || !win->hwnd) return 0;
    return PostMessageW((HWND)win->hwnd, GW_WM_USER_EVENT, (WPARAM)code, (LPARAM)data) ? 1 : 0;
}

int GW_SetTimer(GW_Window* win, int id, int interval_ms) {
    if (!win || !win->hwnd || interval_ms <= 0) return 0;
    // Same hwnd + id replaces the existing timer.
    return SetTimer((HWND)win->hwnd, (UINT_PTR)id, (UINT)interval_ms, NULL) ? 1 : 0;
}

void GW_KillTimer(GW_Window* win, int id) {
    if (!win || !win->hwnd) return;
    KillTimer((HWND)win->hwnd, (UINT_PTR)id);
}

uint64_t GW_GetTicks(void) {
    return (uint64_t)GetTickCount64();
}

/* ---- UTF-8 <-> UTF-16 helpers (heap) ------------------------------------- */

static wchar_t* utf8_to_wide_alloc(const char* s) {
    if (!s) s = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc(sizeof(wchar_t) * n);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char* wide_to_utf8_alloc(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char* s = (char*)malloc(n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

/* ---- clipboard ----------------------------------------------------------- */

int GW_SetClipboardText(GW_Window* win, const char* utf8) {
    wchar_t* w = utf8_to_wide_alloc(utf8);
    if (!w) return 0;
    size_t bytes = (wcslen(w) + 1) * sizeof(wchar_t);
    int ok = 0;
    if (OpenClipboard(win ? (HWND)win->hwnd : NULL)) {
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            if (p) {
                memcpy(p, w, bytes);
                GlobalUnlock(h);
                if (SetClipboardData(CF_UNICODETEXT, h)) ok = 1; // the clipboard owns h now
                else GlobalFree(h);
            } else {
                GlobalFree(h);
            }
        }
        CloseClipboard();
    }
    free(w);
    return ok;
}

char* GW_GetClipboardText(GW_Window* win) {
    char* out = NULL;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return NULL;
    if (!OpenClipboard(win ? (HWND)win->hwnd : NULL)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* w = (const wchar_t*)GlobalLock(h);
        if (w) {
            out = wide_to_utf8_alloc(w);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

/* ---- cursor --------------------------------------------------------------- */

void GW_SetCursor(GW_Window* win, GW_Cursor cursor) {
    if (!win) return;
    LPCWSTR id = IDC_ARROW;
    switch (cursor) {
        case GW_CURSOR_HAND:      id = IDC_HAND; break;
        case GW_CURSOR_IBEAM:     id = IDC_IBEAM; break;
        case GW_CURSOR_SIZE_WE:   id = IDC_SIZEWE; break;
        case GW_CURSOR_SIZE_NS:   id = IDC_SIZENS; break;
        case GW_CURSOR_WAIT:      id = IDC_WAIT; break;
        case GW_CURSOR_CROSSHAIR: id = IDC_CROSS; break;
        default:                  id = IDC_ARROW; break;
    }
    HCURSOR c = LoadCursorW(NULL, id); // shared system cursor: never destroyed
    win->cursor = (void*)c;
    // Apply now if the pointer is over the client area.
    POINT pt;
    if (GetCursorPos(&pt) && WindowFromPoint(pt) == (HWND)win->hwnd) {
        ScreenToClient((HWND)win->hwnd, &pt);
        RECT rc;
        GetClientRect((HWND)win->hwnd, &rc);
        if (PtInRect(&rc, pt)) SetCursor(c);
    }
}

/* ---- high DPI -------------------------------------------------------------- */

typedef BOOL (WINAPI *SetDpiCtxFn)(HANDLE);
typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);

void GW_EnableHighDpi(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    // Windows 10 1703+: per-monitor v2. Older: system aware.
    SetDpiCtxFn set_ctx = (SetDpiCtxFn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (set_ctx && set_ctx((HANDLE)(intptr_t)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */)) return;
    SetProcessDPIAware();
}

float GW_GetDpiScale(GW_Window* win) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    GetDpiForWindowFn get_dpi = user32 ? (GetDpiForWindowFn)(void*)GetProcAddress(user32, "GetDpiForWindow") : NULL;
    UINT dpi = 0;
    if (get_dpi && win && win->hwnd) dpi = get_dpi((HWND)win->hwnd);
    if (dpi == 0) {
        HDC dc = GetDC(NULL);
        dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return dpi ? dpi / 96.0f : 1.0f;
}

/* ---- icons from GW_Image -------------------------------------------------- */

// Builds an HICON of size w x h (nearest-neighbour scaled) from img.
static HICON icon_from_image(GW_Image* img, int w, int h) {
    if (!img || !img->pixels || img->w <= 0 || img->h <= 0 || w <= 0 || h <= 0) return NULL;
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = w;
    bi.bV5Height = -h; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color) return NULL;
    uint32_t* dst = (uint32_t*)bits;
    for (int y = 0; y < h; y++) {
        int sy = y * img->h / h;
        for (int x = 0; x < w; x++) {
            int sx = x * img->w / w;
            dst[y * w + x] = img->pixels[sy * img->w + sx]; // already 0xAARRGGBB
        }
    }
    HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
    ICONINFO ii;
    memset(&ii, 0, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    if (mask) DeleteObject(mask);
    return icon;
}

void GW_SetWindowIcon(GW_Window* win, GW_Image* icon) {
    if (!win || !win->hwnd || !icon) return;
    // (not "small": windows.h defines it as char)
    HICON icon_l = icon_from_image(icon, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    HICON icon_s = icon_from_image(icon, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    if (icon_l) SendMessageW((HWND)win->hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon_l);
    if (icon_s) SendMessageW((HWND)win->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon_s);
    if (icon_l && win->icon_big) DestroyIcon((HICON)win->icon_big);
    if (icon_s && win->icon_small) DestroyIcon((HICON)win->icon_small);
    if (icon_l) win->icon_big = icon_l;
    if (icon_s) win->icon_small = icon_s;
}

/* ---- window extras --------------------------------------------------------- */

void GW_SetMinWindowSize(GW_Window* win, int min_w, int min_h) {
    if (!win) return;
    win->min_w = min_w;
    win->min_h = min_h;
}

void GW_FocusWindow(GW_Window* win) {
    if (!win || !win->hwnd) return;
    HWND hwnd = (HWND)win->hwnd;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    else ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

int GW_IsWindowVisible(GW_Window* win) {
    if (!win || !win->hwnd) return 0;
    return IsWindowVisible((HWND)win->hwnd) && !IsIconic((HWND)win->hwnd);
}

void GW_RequestRedraw(GW_Window* win) {
    if (!win || !win->hwnd) return;
    InvalidateRect((HWND)win->hwnd, NULL, FALSE);
}

/* ---- tray icon -------------------------------------------------------------- */

struct GW_TrayIcon {
    NOTIFYICONDATAW nid;
    HICON icon;
};

static void tray_tip(NOTIFYICONDATAW* nid, const char* tooltip_utf8) {
    nid->szTip[0] = 0;
    if (tooltip_utf8) {
        MultiByteToWideChar(CP_UTF8, 0, tooltip_utf8, -1, nid->szTip, (int)(sizeof(nid->szTip) / sizeof(nid->szTip[0])));
        nid->szTip[sizeof(nid->szTip) / sizeof(nid->szTip[0]) - 1] = 0;
    }
}

GW_TrayIcon* GW_CreateTrayIcon(GW_Window* owner, int id, GW_Image* icon, const char* tooltip_utf8) {
    if (!owner || !owner->hwnd) return NULL;
    GW_TrayIcon* t = (GW_TrayIcon*)calloc(1, sizeof(GW_TrayIcon));
    if (!t) return NULL;
    t->icon = icon_from_image(icon, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    t->nid.cbSize = sizeof(t->nid);
    t->nid.hWnd = (HWND)owner->hwnd;
    t->nid.uID = (UINT)id;
    t->nid.uFlags = NIF_MESSAGE | NIF_TIP | (t->icon ? NIF_ICON : 0);
    t->nid.uCallbackMessage = GW_WM_TRAY;
    t->nid.hIcon = t->icon ? t->icon : LoadIconW(NULL, IDI_APPLICATION);
    t->nid.uFlags |= NIF_ICON;
    tray_tip(&t->nid, tooltip_utf8);
    if (!Shell_NotifyIconW(NIM_ADD, &t->nid)) {
        if (t->icon) DestroyIcon(t->icon);
        free(t);
        return NULL;
    }
    return t;
}

void GW_SetTrayIcon(GW_TrayIcon* t, GW_Image* icon) {
    if (!t) return;
    HICON h = icon_from_image(icon, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    if (!h) return;
    t->nid.hIcon = h;
    t->nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &t->nid);
    if (t->icon) DestroyIcon(t->icon);
    t->icon = h;
}

void GW_SetTrayTooltip(GW_TrayIcon* t, const char* tooltip_utf8) {
    if (!t) return;
    tray_tip(&t->nid, tooltip_utf8);
    t->nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &t->nid);
}

void GW_DestroyTrayIcon(GW_TrayIcon* t) {
    if (!t) return;
    Shell_NotifyIconW(NIM_DELETE, &t->nid);
    if (t->icon) DestroyIcon(t->icon);
    free(t);
}

/* ---- popup menu -------------------------------------------------------------- */

int GW_ShowPopupMenu(GW_Window* owner, const char** items, int count) {
    if (!owner || !owner->hwnd || !items || count <= 0) return -1;
    HMENU menu = CreatePopupMenu();
    if (!menu) return -1;
    for (int i = 0; i < count; i++) {
        if (!items[i] || !items[i][0]) {
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        wchar_t* w = utf8_to_wide_alloc(items[i]);
        AppendMenuW(menu, MF_STRING, (UINT_PTR)(i + 1), w ? w : L"?");
        free(w);
    }
    POINT pt;
    GetCursorPos(&pt);
    HWND hwnd = (HWND)owner->hwnd;
    // Required so the menu closes when clicking elsewhere (tray menus).
    SetForegroundWindow(hwnd);
    int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd > 0 ? cmd - 1 : -1;
}

#endif // _WIN32
