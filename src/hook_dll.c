#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

#include "rules.h"

static wchar_t g_process_name[MAX_PATH];
static wchar_t g_queue_path[MAX_PATH];
static wchar_t g_last_class_name[256];
static wchar_t g_deleted_class_name[256];
static unsigned int g_last_uid = 0xffffffffu;
static unsigned int g_deleted_uid = 0xffffffffu;
static DWORD g_last_delete_tick = 0;
static int g_process_is_target = 0;

static const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *name = path;
    for (const wchar_t *p = path; p && *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            name = p + 1;
        }
    }
    return name;
}

static void uppercase_copy(wchar_t *dest, int dest_count, const wchar_t *src) {
    int i = 0;
    if (!dest || dest_count <= 0) {
        return;
    }
    if (!src) {
        dest[0] = L'\0';
        return;
    }
    for (; i < dest_count - 1 && src[i]; ++i) {
        wchar_t c = src[i];
        if (c >= L'a' && c <= L'z') {
            c = (wchar_t)(c - L'a' + L'A');
        }
        dest[i] = c;
    }
    dest[i] = L'\0';
}

static void queue_rule(const wchar_t *class_name, unsigned int uid) {
    wchar_t exe_upper[MAX_PATH];
    wchar_t line[1024];
    HANDLE file;
    HANDLE event;
    DWORD written = 0;

    if (!g_queue_path[0] || !class_name || !class_name[0]) {
        return;
    }

    uppercase_copy(exe_upper, (int)(sizeof(exe_upper) / sizeof(exe_upper[0])), g_process_name);
    _snwprintf(line, (sizeof(line) / sizeof(line[0])) - 1, L"%ls|%ls|%u\n", exe_upper, class_name, uid);
    line[(sizeof(line) / sizeof(line[0])) - 1] = L'\0';

    file = CreateFileW(g_queue_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &written, NULL);
        CloseHandle(file);
    }

    event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\LyricsTrayIconFixSync");
    if (event) {
        SetEvent(event);
        CloseHandle(event);
    }
}

static void inspect_window(HWND hwnd) {
    wchar_t class_name[256];
    wchar_t window_text[256];
    unsigned int uid = 0;

    if (!g_process_is_target || !hwnd) {
        return;
    }

    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return;
    }

    window_text[0] = L'\0';
    GetWindowTextW(hwnd, window_text, (int)(sizeof(window_text) / sizeof(window_text[0])));
    if (tray_rule_should_hide_window(g_process_name, class_name, window_text)) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    if (tray_rule_uid_for_window(g_process_name, class_name, &uid)) {
        if (g_last_uid == uid && wcscmp(g_last_class_name, class_name) == 0) {
            goto delete_icon;
        }
        wcsncpy(g_last_class_name, class_name, (sizeof(g_last_class_name) / sizeof(g_last_class_name[0])) - 1);
        g_last_class_name[(sizeof(g_last_class_name) / sizeof(g_last_class_name[0])) - 1] = L'\0';
        g_last_uid = uid;
        queue_rule(class_name, uid);

delete_icon: ;
        DWORD now = GetTickCount();
        if (g_deleted_uid != uid ||
            wcscmp(g_deleted_class_name, class_name) != 0 ||
            now - g_last_delete_tick >= 1000) {
            NOTIFYICONDATAW data;
            ZeroMemory(&data, sizeof(data));
            data.cbSize = sizeof(data);
            data.hWnd = hwnd;
            data.uID = uid;
            g_last_delete_tick = now;
            if (Shell_NotifyIconW(NIM_DELETE, &data)) {
                wcsncpy(g_deleted_class_name, class_name, (sizeof(g_deleted_class_name) / sizeof(g_deleted_class_name[0])) - 1);
                g_deleted_class_name[(sizeof(g_deleted_class_name) / sizeof(g_deleted_class_name[0])) - 1] = L'\0';
                g_deleted_uid = uid;
            }
        }
    }
}

__declspec(dllexport) LRESULT CALLBACK CallWndHookProc(int code, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    if (code >= 0) {
        const CWPSTRUCT *message = (const CWPSTRUCT *)lparam;
        if (message) {
            inspect_window(message->hwnd);
        }
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

__declspec(dllexport) LRESULT CALLBACK GetMsgHookProc(int code, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    if (code >= 0) {
        const MSG *message = (const MSG *)lparam;
        if (message) {
            inspect_window(message->hwnd);
        }
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t module_path[MAX_PATH];
        DisableThreadLibraryCalls(instance);

        if (GetModuleFileNameW(instance, module_path, MAX_PATH)) {
            wchar_t *slash = NULL;
            for (wchar_t *p = module_path; *p; ++p) {
                if (*p == L'\\' || *p == L'/') {
                    slash = p;
                }
            }
            if (slash) {
                slash[1] = L'\0';
                wcsncpy(g_queue_path, module_path, MAX_PATH - 1);
                g_queue_path[MAX_PATH - 1] = L'\0';
                wcsncat(g_queue_path, L"ps-tray-sync-queue.log", MAX_PATH - wcslen(g_queue_path) - 1);
            }
        }

        if (GetModuleFileNameW(NULL, module_path, MAX_PATH)) {
            wcsncpy(g_process_name, base_name(module_path), MAX_PATH - 1);
            g_process_name[MAX_PATH - 1] = L'\0';
            g_process_is_target = tray_rule_process_is_target(g_process_name);
        }
    }

    return TRUE;
}
