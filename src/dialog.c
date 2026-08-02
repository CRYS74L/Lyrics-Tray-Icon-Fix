#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#define ID_TEXT 1001
#define ID_COPY 1002
#define ID_OK 1003

static const wchar_t *g_message = L"";

static void copy_message(HWND owner) {
    if (!OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    size_t bytes = (wcslen(g_message) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void *target = GlobalLock(memory);
        if (target) {
            wcscpy((wchar_t *)target, g_message);
            GlobalUnlock(memory);
        }
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

static LRESULT CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            HFONT font = CreateFontW(
                -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
                        HWND text = CreateWindowExW(
                0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                18, 18, 584, 350, hwnd, (HMENU)ID_TEXT, GetModuleHandleW(NULL), NULL);
            HWND copy = CreateWindowExW(
                0, L"BUTTON", L"复制",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                368, 390, 100, 32, hwnd, (HMENU)ID_COPY, GetModuleHandleW(NULL), NULL);
            HWND ok = CreateWindowExW(
                0, L"BUTTON", L"确定",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
                486, 390, 100, 32, hwnd, (HMENU)ID_OK, GetModuleHandleW(NULL), NULL);
            SendMessageW((HWND)GetDlgItem(hwnd, ID_TEXT), WM_SETFONT, (WPARAM)font, TRUE);
            SetWindowTextW(text, g_message);
            SendMessageW(copy, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(ok, WM_SETFONT, (WPARAM)font, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == ID_COPY) {
                copy_message(hwnd);
            } else if (LOWORD(wparam) == ID_OK) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)previous;
    (void)command_line;
    (void)show;

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2) {
        if (argv) LocalFree(argv);
        return 1;
    }

    HANDLE file = CreateFileW(
        argv[1], GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD size = 0;
    if (file != INVALID_HANDLE_VALUE) {
        size = GetFileSize(file, NULL);
    }

        unsigned char *data = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
    if (!data) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (argv) LocalFree(argv);
        return 1;
    }
    if (file != INVALID_HANDLE_VALUE && size > 0) {
        DWORD read = 0;
        ReadFile(file, data, size, &read, NULL);
        CloseHandle(file);
    }
    data[size] = '\0';

    int wide_len = 0;
    wchar_t *buffer = NULL;
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        wide_len = (int)((size - 2) / sizeof(wchar_t)) + 1;
        buffer = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)wide_len * sizeof(wchar_t));
        if (buffer) {
            memcpy(buffer, data + 2, size - 2);
            buffer[wide_len - 1] = L'\0';
        }
    } else {
        wide_len = MultiByteToWideChar(CP_ACP, 0, (const char *)data, -1, NULL, 0);
        buffer = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)wide_len * sizeof(wchar_t));
        if (buffer) {
            MultiByteToWideChar(CP_ACP, 0, (const char *)data, -1, buffer, wide_len);
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    if (!buffer) {
        if (argv) LocalFree(argv);
        return 1;
    }
    g_message = buffer;
    LocalFree(argv);

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = dialog_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)(ULONG_PTR)32512);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"LyricsTrayIconFixDialog";
    if (!RegisterClassW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"LyricsTrayIconFixDialog",
        L"Lyrics Tray Icon Fix",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 470,
        NULL, NULL, instance, NULL);
    if (!hwnd) {
        return 1;
    }

    RECT rect;
    GetWindowRect(hwnd, &rect);
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - (rect.right - rect.left)) / 2;
    int y = (screen_h - (rect.bottom - rect.top)) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd, SW_SHOW);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
