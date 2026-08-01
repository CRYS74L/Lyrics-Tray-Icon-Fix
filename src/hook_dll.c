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

typedef BOOL (WINAPI *ShellNotifyIconWFn)(DWORD, PNOTIFYICONDATAW);

static ShellNotifyIconWFn g_original_shell_notify_icon_w = NULL;
static int g_shell_notify_iat_hooked = 0;

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

static int guid_icon_exists(const GUID *guid) {
    NOTIFYICONIDENTIFIER identifier;
    RECT rect;

    if (!guid) {
        return 0;
    }

    ZeroMemory(&identifier, sizeof(identifier));
    identifier.cbSize = sizeof(identifier);
    identifier.guidItem = *guid;
    return Shell_NotifyIconGetRect(&identifier, &rect) == S_OK;
}

static int same_guid(const GUID *a, const GUID *b) {
    return a && b &&
           a->Data1 == b->Data1 &&
           a->Data2 == b->Data2 &&
           a->Data3 == b->Data3 &&
           a->Data4[0] == b->Data4[0] &&
           a->Data4[1] == b->Data4[1] &&
           a->Data4[2] == b->Data4[2] &&
           a->Data4[3] == b->Data4[3] &&
           a->Data4[4] == b->Data4[4] &&
           a->Data4[5] == b->Data4[5] &&
           a->Data4[6] == b->Data4[6] &&
           a->Data4[7] == b->Data4[7];
}

static int should_block_guid_notify(DWORD message, const NOTIFYICONDATAW *data) {
    GUID rule_guid;

    if (!data || !(data->uFlags & NIF_GUID)) {
        return 0;
    }
    if (message != NIM_ADD && message != NIM_MODIFY && message != NIM_SETVERSION) {
        return 0;
    }
    if (!tray_rule_guid_for_process(g_process_name, &rule_guid)) {
        return 0;
    }

    return same_guid(&data->guidItem, &rule_guid);
}

static int should_block_uid_notify(DWORD message, const NOTIFYICONDATAW *data) {
    wchar_t class_name[256];

    if (!data || !data->hWnd) {
        return 0;
    }
    if (message != NIM_ADD && message != NIM_MODIFY && message != NIM_SETVERSION) {
        return 0;
    }
    if (data->uFlags & NIF_GUID) {
        return 0;
    }
    if (!GetClassNameW(data->hWnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return 0;
    }

    return tray_rule_block_uid_for_window(g_process_name, class_name, data->uID);
}

static BOOL CALLBACK delete_current_block_uid_icon_proc(HWND hwnd, LPARAM lparam) {
    DWORD pid = 0;
    DWORD current_pid = (DWORD)lparam;
    wchar_t class_name[256];
    unsigned int uid = 0;
    NOTIFYICONDATAW data;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != current_pid) {
        return TRUE;
    }

    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return TRUE;
    }

    if (!tray_rule_block_uid_for_window_class(g_process_name, class_name, &uid)) {
        return TRUE;
    }

    ZeroMemory(&data, sizeof(data));
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = uid;
    Shell_NotifyIconW(NIM_DELETE, &data);
    return TRUE;
}

static DWORD WINAPI delete_current_block_uid_icons_thread(LPVOID param) {
    (void)param;
    Sleep(200);
    EnumWindows(delete_current_block_uid_icon_proc, (LPARAM)GetCurrentProcessId());
    return 0;
}

static BOOL WINAPI hooked_shell_notify_icon_w(DWORD message, PNOTIFYICONDATAW data) {
    if (should_block_uid_notify(message, data)) {
        return TRUE;
    }
    if (should_block_guid_notify(message, data)) {
        return TRUE;
    }
    return g_original_shell_notify_icon_w ? g_original_shell_notify_icon_w(message, data) : FALSE;
}

static void install_shell_notify_iat_hook(void) {
    HMODULE module;
    HMODULE shell32;
    FARPROC original;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY import_dir;
    IMAGE_IMPORT_DESCRIPTOR *import_desc;

    if (g_shell_notify_iat_hooked) {
        return;
    }

    module = GetModuleHandleW(NULL);
    shell32 = GetModuleHandleW(L"shell32.dll");
    if (!module) {
        return;
    }
    if (!shell32) {
        shell32 = LoadLibraryW(L"shell32.dll");
        if (!shell32) {
            return;
        }
    }

    original = GetProcAddress(shell32, "Shell_NotifyIconW");
    if (!original) {
        return;
    }
    g_original_shell_notify_icon_w = (ShellNotifyIconWFn)original;

    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }

    import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!import_dir.VirtualAddress) {
        return;
    }

    import_desc = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)module + import_dir.VirtualAddress);
    for (; import_desc->Name; ++import_desc) {
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)((BYTE *)module + import_desc->FirstThunk);
        for (; thunk && thunk->u1.Function; ++thunk) {
            void **function_slot = (void **)&thunk->u1.Function;
            if (*function_slot == (void *)original) {
                DWORD old_protect = 0;
                if (VirtualProtect(function_slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
                    *function_slot = (void *)hooked_shell_notify_icon_w;
                    VirtualProtect(function_slot, sizeof(void *), old_protect, &old_protect);
                    FlushInstructionCache(GetCurrentProcess(), function_slot, sizeof(void *));
                    g_shell_notify_iat_hooked = 1;
                }
            }
        }
    }
}

static void inspect_window(HWND hwnd) {
    wchar_t class_name[256];
    wchar_t window_text[256];
    unsigned int uid = 0;
    GUID guid;

    if (!g_process_is_target || !hwnd) {
        return;
    }

    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return;
    }

    if (tray_rule_guid_for_process(g_process_name, &guid)) {
        if (guid_icon_exists(&guid)) {
            NOTIFYICONDATAW data;
            ZeroMemory(&data, sizeof(data));
            data.cbSize = sizeof(data);
            data.hWnd = hwnd;
            data.uFlags = NIF_GUID;
            data.guidItem = guid;
            Shell_NotifyIconW(NIM_DELETE, &data);
        }
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
        GUID guid;
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
            g_process_is_target = tray_rule_process_uses_message_hook(g_process_name);
            if (tray_rule_process_uses_shell_notify_block(g_process_name)) {
                HANDLE thread;
                install_shell_notify_iat_hook();
                thread = CreateThread(NULL, 0, delete_current_block_uid_icons_thread, NULL, 0, NULL);
                if (thread) {
                    CloseHandle(thread);
                }
            }
        }
    }

    return TRUE;
}
