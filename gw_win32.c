#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gw_internal.h"

#define EVENT_QUEUE_SIZE 256
static GW_Event event_queue[EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;

static void push_event_impl(GW_Window* win, GW_Event ev) {
    ev.window = win;
    int next_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
    if (next_head != queue_tail) {
        event_queue[queue_head] = ev;
        queue_head = next_head;
    }
}
#define push_event(ev) push_event_impl(win, ev)

static int pop_event(GW_Event* ev) {
    if (queue_head == queue_tail) return 0;
    *ev = event_queue[queue_tail];
    queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

static void resize_backbuffer(GW_Window* win, int new_w, int new_h) {
    if (new_w <= 0 || new_h <= 0) return;
    
    // Select out old bitmap and destroy previous HBITMAP
    if (win->mem_dc && win->old_bitmap) {
        SelectObject((HDC)win->mem_dc, (HGDIOBJ)win->old_bitmap);
    }
    if (win->hbitmap) {
        DeleteObject((HBITMAP)win->hbitmap);
    }
    
    win->width = new_w;
    win->height = new_h;
    
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = new_w;
    bmi.bmiHeader.biHeight = -new_h; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* bits = NULL;
    HDC screen_dc = GetDC((HWND)win->hwnd);
    win->hbitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC((HWND)win->hwnd, screen_dc);
    
    win->old_bitmap = SelectObject((HDC)win->mem_dc, (HBITMAP)win->hbitmap);
    win->backbuffer = (uint32_t*)bits;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    GW_Window* win = (GW_Window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    
    switch (message) {
        case WM_CLOSE: {
            GW_Event ev;
            ev.type = GW_EVENT_QUIT;
            push_event(ev);
            return 0; // Handled, let the application call DestroyWindow
        }
        
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        
        case WM_SIZE: {
            int new_w = LOWORD(lParam);
            int new_h = HIWORD(lParam);
            if (win) {
                resize_backbuffer(win, new_w, new_h);
                GW_Event ev;
                ev.type = GW_EVENT_WINDOW_RESIZE;
                ev.resize.width = new_w;
                ev.resize.height = new_h;
                push_event(ev);
            }
            return 0;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (win && win->hbitmap) {
                BitBlt(hdc, 0, 0, win->width, win->height, (HDC)win->mem_dc, 0, 0, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            
            GW_Event ev;
            ev.type = GW_EVENT_WINDOW_EXPOSE;
            push_event(ev);
            return 0;
        }
        
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            GW_Event ev;
            ev.type = GW_EVENT_MOUSE_BUTTON_DOWN;
            ev.mouse_button.x = GET_X_LPARAM(lParam);
            ev.mouse_button.y = GET_Y_LPARAM(lParam);
            ev.mouse_button.button = (message == WM_LBUTTONDOWN) ? 1 : ((message == WM_MBUTTONDOWN) ? 2 : 3);
            push_event(ev);
            return 0;
        }
        
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            GW_Event ev;
            ev.type = GW_EVENT_MOUSE_BUTTON_UP;
            ev.mouse_button.x = GET_X_LPARAM(lParam);
            ev.mouse_button.y = GET_Y_LPARAM(lParam);
            ev.mouse_button.button = (message == WM_LBUTTONUP) ? 1 : ((message == WM_MBUTTONUP) ? 2 : 3);
            push_event(ev);
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            GW_Event ev;
            ev.type = GW_EVENT_MOUSE_MOTION;
            ev.mouse_motion.x = GET_X_LPARAM(lParam);
            ev.mouse_motion.y = GET_Y_LPARAM(lParam);
            push_event(ev);
            return 0;
        }
        
        case WM_KEYDOWN:
        case WM_KEYUP: {
            GW_Event ev;
            ev.type = (message == WM_KEYDOWN) ? GW_EVENT_KEY_DOWN : GW_EVENT_KEY_UP;
            
            int keycode = 0;
            switch (wParam) {
                case VK_BACK:   keycode = GW_KEY_BACKSPACE; break;
                case VK_TAB:    keycode = GW_KEY_TAB; break;
                case VK_RETURN: keycode = GW_KEY_ENTER; break;
                case VK_ESCAPE: keycode = GW_KEY_ESCAPE; break;
                case VK_LEFT:   keycode = GW_KEY_LEFT; break;
                case VK_UP:     keycode = GW_KEY_UP; break;
                case VK_RIGHT:  keycode = GW_KEY_RIGHT; break;
                case VK_DOWN:   keycode = GW_KEY_DOWN; break;
                case VK_DELETE: keycode = GW_KEY_DELETE; break;
                case VK_HOME:   keycode = GW_KEY_HOME; break;
                case VK_END:    keycode = GW_KEY_END; break;
                case VK_PRIOR:  keycode = GW_KEY_PAGEUP; break;
                case VK_NEXT:   keycode = GW_KEY_PAGEDOWN; break;
                default:        keycode = (int)wParam; break;
            }
            
            ev.key.keycode = keycode;
            
            int mods = 0;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= GW_MOD_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= GW_MOD_CTRL;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= GW_MOD_ALT;
            ev.key.mod = mods;
            
            push_event(ev);
            return 0;
        }
        
        case WM_CHAR: {
            GW_Event ev;
            ev.type = GW_EVENT_TEXT_INPUT;
            ev.text.character = (wchar_t)wParam;
            push_event(ev);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            GW_Event ev;
            ev.type = GW_EVENT_KEY_DOWN;
            ev.key.keycode = (delta > 0) ? '+' : '-';
            ev.key.mod = 0;
            push_event(ev);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

GW_Window* GW_CreateWindow(const char* title, int width, int height) {
    HINSTANCE hinstance = GetModuleHandleW(NULL);
    
    static int class_registered = 0;
    if (!class_registered) {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hinstance;
        wc.hIcon = LoadIconW(hinstance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"LibGW_Class";
        
        if (!RegisterClassW(&wc)) {
            fprintf(stderr, "LibGW Error: Failed to register window class.\n");
            return NULL;
        }
        class_registered = 1;
    }
    
    // Convert UTF-8 title to wide characters
    wchar_t wtitle[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    
    RECT wr = { 0, 0, width, height };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    
    HWND hwnd = CreateWindowExW(
        0,
        L"LibGW_Class",
        wtitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hinstance, NULL
    );
    
    if (!hwnd) {
        fprintf(stderr, "LibGW Error: Failed to create window.\n");
        return NULL;
    }
    
    GW_Window* win = (GW_Window*)malloc(sizeof(GW_Window));
    if (!win) {
        DestroyWindow(hwnd);
        return NULL;
    }
    memset(win, 0, sizeof(GW_Window));
    win->hwnd = (void*)hwnd;
    win->width = width;
    win->height = height;
    
    HDC screen_dc = GetDC(hwnd);
    win->hdc = (void*)screen_dc;
    win->mem_dc = (void*)CreateCompatibleDC(screen_dc);
    
    // Allocate the DIB section for the backbuffer
    resize_backbuffer(win, width, height);
    
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)win);
    
    return win;
}

void GW_DestroyWindow(GW_Window* win) {
    if (!win) return;
    HWND hwnd = (HWND)win->hwnd;
    
    if (win->mem_dc && win->old_bitmap) {
        SelectObject((HDC)win->mem_dc, (HGDIOBJ)win->old_bitmap);
    }
    if (win->hbitmap) {
        DeleteObject((HBITMAP)win->hbitmap);
    }
    if (win->mem_dc) {
        DeleteDC((HDC)win->mem_dc);
    }
    if (win->hdc) {
        ReleaseDC(hwnd, (HDC)win->hdc);
    }
    
    DestroyWindow(hwnd);
    free(win);
}

void GW_ShowWindow(GW_Window* win, int show) {
    if (!win) return;
    ShowWindow((HWND)win->hwnd, show ? SW_SHOW : SW_HIDE);
    if (show) {
        SetFocus((HWND)win->hwnd);
    }
    UpdateWindow((HWND)win->hwnd);
}

void GW_GetWindowSize(GW_Window* win, int* width, int* height) {
    if (!win) return;
    if (width) *width = win->width;
    if (height) *height = win->height;
}

void GW_SetWindowTitle(GW_Window* win, const char* title) {
    if (!win) return;
    wchar_t wtitle[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    SetWindowTextW((HWND)win->hwnd, wtitle);
}

int GW_WaitEvent(GW_Event* event) {
    if (!event) return 0;
    
    if (pop_event(event)) {
        return 1;
    }
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        
        if (pop_event(event)) {
            return 1;
        }
    }
    
    event->type = GW_EVENT_QUIT;
    return 0;
}

int GW_PollEvent(GW_Event* event) {
    if (!event) return 0;
    
    if (pop_event(event)) {
        return 1;
    }
    
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        
        if (pop_event(event)) {
            return 1;
        }
    }
    
    return 0;
}

uint32_t* GW_GetBackbuffer(GW_Window* win, int* width, int* height) {
    if (!win) return NULL;
    if (width) *width = win->width;
    if (height) *height = win->height;
    return win->backbuffer;
}

void GW_Present(GW_Window* win) {
    if (!win || !win->hwnd || !win->hbitmap) return;
    HWND hwnd = (HWND)win->hwnd;
    HDC hdc = GetDC(hwnd);
    BitBlt(hdc, 0, 0, win->width, win->height, (HDC)win->mem_dc, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, hdc);
}

int GW_ShowMessageBox(GW_Window* parent, const char* title, const wchar_t* text, const wchar_t** buttons, int num_buttons) {
    wchar_t wtitle[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    
    HWND hwnd_parent = parent ? (HWND)parent->hwnd : NULL;
    
    if (num_buttons == 3) {
        // e.g., "Guardar", "No guardar", "Cancelar" -> IDYES, IDNO, IDCANCEL
        int ret = MessageBoxW(hwnd_parent, text, wtitle, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (ret == IDYES) return 0;
        if (ret == IDNO) return 1;
        return 2;
    } else if (num_buttons == 2) {
        int ret = MessageBoxW(hwnd_parent, text, wtitle, MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES) return 0;
        return 1;
    } else {
        MessageBoxW(hwnd_parent, text, wtitle, MB_OK | MB_ICONINFORMATION);
        return 0;
    }
}

char* GW_ShowOpenFileDialog(GW_Window* parent, const char* title, const char* filter) {
    OPENFILENAMEW ofn;
    static wchar_t szFile[512];
    szFile[0] = L'\0';
    
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent ? (HWND)parent->hwnd : NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    
    wchar_t wtitle[256] = {0};
    if (title) MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    ofn.lpstrTitle = title ? wtitle : NULL;
    
    wchar_t wfilter[512] = {0};
    if (filter) {
        int i = 0;
        int j = 0;
        while (filter[i] != '\0' && j < 510) {
            if (filter[i] == '|') {
                wfilter[j++] = L'\0';
            } else {
                wfilter[j++] = (wchar_t)filter[i];
            }
            i++;
        }
        wfilter[j++] = L'\0';
        wfilter[j++] = L'\0';
        ofn.lpstrFilter = wfilter;
    } else {
        ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    }
    
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn) == TRUE) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, NULL, 0, NULL, NULL);
        char* res = (char*)malloc(size_needed);
        if (res) {
            WideCharToMultiByte(CP_UTF8, 0, szFile, -1, res, size_needed, NULL, NULL);
        }
        return res;
    }
    return NULL;
}

char* GW_ShowSaveFileDialog(GW_Window* parent, const char* title, const char* filter) {
    OPENFILENAMEW ofn;
    static wchar_t szFile[512];
    szFile[0] = L'\0';
    
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent ? (HWND)parent->hwnd : NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    
    wchar_t wtitle[256] = {0};
    if (title) MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    ofn.lpstrTitle = title ? wtitle : NULL;
    
    wchar_t wfilter[512] = {0};
    if (filter) {
        int i = 0;
        int j = 0;
        while (filter[i] != '\0' && j < 510) {
            if (filter[i] == '|') {
                wfilter[j++] = L'\0';
            } else {
                wfilter[j++] = (wchar_t)filter[i];
            }
            i++;
        }
        wfilter[j++] = L'\0';
        wfilter[j++] = L'\0';
        ofn.lpstrFilter = wfilter;
    } else {
        ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    }
    
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    
    if (GetSaveFileNameW(&ofn) == TRUE) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, NULL, 0, NULL, NULL);
        char* res = (char*)malloc(size_needed);
        if (res) {
            WideCharToMultiByte(CP_UTF8, 0, szFile, -1, res, size_needed, NULL, NULL);
        }
        return res;
    }
    return NULL;
}

int GW_UTF8ToWide(const char* utf8, wchar_t* wide, int max_wide_chars) {
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, max_wide_chars);
}

int GW_WideToUTF8(const wchar_t* wide, char* utf8, int max_utf8_bytes) {
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, max_utf8_bytes, NULL, NULL);
}

// Windows implementation of GW_Process
struct GW_Process {
    HANDLE h_process;
    HANDLE h_stdin_write;
    HANDLE h_stdout_read;
};

char* GW_SearchPath(const char* filename) {
    wchar_t wfilename[260];
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, wfilename, 260);
    
    wchar_t wbuffer[MAX_PATH];
    wchar_t* file_part;
    
    // First try standard search
    DWORD res = SearchPathW(NULL, wfilename, NULL, MAX_PATH, wbuffer, &file_part);
    if (res == 0) {
        // Try appending .exe
        res = SearchPathW(NULL, wfilename, L".exe", MAX_PATH, wbuffer, &file_part);
    }
    
    if (res > 0 && res < MAX_PATH) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, NULL, 0, NULL, NULL);
        char* path = (char*)malloc(size_needed);
        if (path) {
            WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, path, size_needed, NULL, NULL);
        }
        return path;
    }
    
    return NULL;
}

GW_Process* GW_SpawnProcess(const char* shell_command) {
    char* full_path = GW_SearchPath(shell_command);
    if (!full_path) {
        return NULL;
    }
    
    wchar_t wcmd[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, full_path, -1, wcmd, MAX_PATH);
    free(full_path);
    
    HANDLE h_stdin_read = NULL;
    HANDLE h_stdin_write = NULL;
    HANDLE h_stdout_read = NULL;
    HANDLE h_stdout_write = NULL;
    
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    if (!CreatePipe(&h_stdout_read, &h_stdout_write, &sa, 0)) {
        return NULL;
    }
    if (!CreatePipe(&h_stdin_read, &h_stdin_write, &sa, 0)) {
        CloseHandle(h_stdout_read);
        CloseHandle(h_stdout_write);
        return NULL;
    }
    
    SetHandleInformation(h_stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(h_stdin_write, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = h_stdout_write;
    si.hStdOutput = h_stdout_write;
    si.hStdInput = h_stdin_read;
    si.dwFlags |= STARTF_USESTDHANDLES;
    
    memset(&pi, 0, sizeof(pi));
    
    wchar_t cmd_line[MAX_PATH];
    wcscpy_s(cmd_line, MAX_PATH, wcmd);
    
    BOOL success = CreateProcessW(
        NULL,
        cmd_line,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );
    
    CloseHandle(h_stdout_write);
    CloseHandle(h_stdin_read);
    
    if (!success) {
        CloseHandle(h_stdout_read);
        CloseHandle(h_stdin_write);
        return NULL;
    }
    
    CloseHandle(pi.hThread);
    
    GW_Process* proc = (GW_Process*)malloc(sizeof(GW_Process));
    if (!proc) {
        CloseHandle(pi.hProcess);
        CloseHandle(h_stdout_read);
        CloseHandle(h_stdin_write);
        return NULL;
    }
    proc->h_process = pi.hProcess;
    proc->h_stdin_write = h_stdin_write;
    proc->h_stdout_read = h_stdout_read;
    
    return proc;
}

int GW_ReadProcess(GW_Process* proc, char* buffer, int max_len) {
    if (!proc || !proc->h_stdout_read) return -1;
    
    DWORD bytes_avail = 0;
    if (!PeekNamedPipe(proc->h_stdout_read, NULL, 0, NULL, &bytes_avail, NULL)) {
        return -1; // Pipe broken
    }
    
    if (bytes_avail == 0) {
        return 0; // No data available
    }
    
    DWORD to_read = bytes_avail > (DWORD)max_len ? (DWORD)max_len : bytes_avail;
    DWORD bytes_read = 0;
    if (!ReadFile(proc->h_stdout_read, buffer, to_read, &bytes_read, NULL)) {
        return -1;
    }
    
    return (int)bytes_read;
}

int GW_WriteProcess(GW_Process* proc, const char* buffer, int len) {
    if (!proc || !proc->h_stdin_write) return -1;
    
    DWORD bytes_written = 0;
    if (!WriteFile(proc->h_stdin_write, buffer, (DWORD)len, &bytes_written, NULL)) {
        return -1;
    }
    
    return (int)bytes_written;
}

void GW_CloseProcess(GW_Process* proc) {
    if (!proc) return;
    if (proc->h_stdin_write) CloseHandle(proc->h_stdin_write);
    if (proc->h_stdout_read) CloseHandle(proc->h_stdout_read);
    if (proc->h_process) {
        TerminateProcess(proc->h_process, 0);
        CloseHandle(proc->h_process);
    }
    free(proc);
}

#endif // _WIN32
