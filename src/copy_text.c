#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance;
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
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        wide_len = MultiByteToWideChar(
            CP_UTF8, 0, (const char *)data + 3, (int)(size - 3), NULL, 0);
        buffer = (wchar_t *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)wide_len * sizeof(wchar_t));
        if (buffer) {
            MultiByteToWideChar(
                CP_UTF8, 0, (const char *)data + 3, (int)(size - 3),
                buffer, wide_len);
        }
    } else if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        wide_len = (int)((size - 2) / sizeof(wchar_t)) + 1;
        buffer = (wchar_t *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)wide_len * sizeof(wchar_t));
        if (buffer) {
            memcpy(buffer, data + 2, size - 2);
            buffer[wide_len - 1] = L'\0';
        }
    } else {
        wide_len = MultiByteToWideChar(CP_ACP, 0, (const char *)data, -1, NULL, 0);
        buffer = (wchar_t *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)wide_len * sizeof(wchar_t));
        if (buffer) {
            MultiByteToWideChar(CP_ACP, 0, (const char *)data, -1, buffer, wide_len);
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    if (!buffer) {
        if (argv) LocalFree(argv);
        return 1;
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        size_t bytes = (wcslen(buffer) + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            void *target = GlobalLock(memory);
            if (target) {
                wcscpy((wchar_t *)target, buffer);
                GlobalUnlock(memory);
            }
            SetClipboardData(CF_UNICODETEXT, memory);
        }
        CloseClipboard();
    }

    HeapFree(GetProcessHeap(), 0, buffer);
    LocalFree(argv);
    return 0;
}
