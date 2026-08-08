#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "rules.h"

static const GUID kGoogleDrivePreservedGuid = {
    0x6BBAE539, 0x2232, 0x434A,
    { 0xA4, 0xE5, 0x9A, 0x33, 0x56, 0x0C, 0x62, 0x83 }
};

static wchar_t g_process_name[MAX_PATH];
static wchar_t g_queue_path[MAX_PATH];
static wchar_t g_last_class_name[256];
static wchar_t g_deleted_class_name[256];
static unsigned int g_last_uid = 0xffffffffu;
static unsigned int g_deleted_uid = 0xffffffffu;
static DWORD g_last_delete_tick = 0;
static int g_process_is_target = 0;

#define MAX_BLOCK_DELETE_STATES 16
typedef struct BlockDeleteState {
    wchar_t class_name[256];
    DWORD last_delete_tick;
} BlockDeleteState;

static BlockDeleteState g_block_delete_states[MAX_BLOCK_DELETE_STATES];

typedef BOOL (WINAPI *ShellNotifyIconWFn)(DWORD, PNOTIFYICONDATAW);
typedef BOOL (WINAPI *ShellNotifyIconAFn)(DWORD, PNOTIFYICONDATAA);

typedef struct DelayImportDescriptor {
    DWORD attributes;
    DWORD name;
    DWORD module_handle;
    DWORD import_address_table;
    DWORD import_name_table;
    DWORD bound_import_address_table;
    DWORD unload_import_address_table;
    DWORD timestamp;
} DelayImportDescriptor;

#define MAX_SHELL_HOOK_SLOTS 64
typedef struct ShellHookW {
    void **slot;
    ShellNotifyIconWFn next;
} ShellHookW;
typedef struct ShellHookA {
    void **slot;
    ShellNotifyIconAFn next;
} ShellHookA;

static ShellNotifyIconWFn g_next_shell_notify_icon_w = NULL;
static ShellNotifyIconAFn g_next_shell_notify_icon_a = NULL;
static int g_shell_notify_iat_hooked = 0;
static volatile LONG g_install_state = 0;
static HANDLE g_shell_notify_ready_event = NULL;
static ShellHookW g_shell_hook_w[MAX_SHELL_HOOK_SLOTS];
static ShellHookA g_shell_hook_a[MAX_SHELL_HOOK_SLOTS];
static int g_shell_hook_w_count = 0;
static int g_shell_hook_a_count = 0;
static BYTE *g_inline_w_trampoline = NULL;
static BYTE *g_inline_a_trampoline = NULL;
static BYTE *g_inline_w_target = NULL;
static BYTE *g_inline_a_target = NULL;
static BYTE g_inline_w_original[14];
static BYTE g_inline_a_original[14];
static int g_inline_hooked = 0;
static HWINEVENTHOOK g_google_event_hook = NULL;
static DWORD g_google_event_thread_id = 0;
static HANDLE g_google_event_thread_handle = NULL;
static HANDLE g_google_cleanup_thread_handle = NULL;
static HWINEVENTHOOK g_message_event_hook = NULL;
static DWORD g_message_event_thread_id = 0;
static HANDLE g_message_event_thread_handle = NULL;

#define LYRICIFY_CLEANUP_MS 10000
#define LYRICIFY_CLEANUP_INTERVAL_MS 200
static HWND g_lyricify_cleanup_hwnd = NULL;
static volatile LONG g_lyricify_cleanup_started = 0;
static HANDLE g_lyricify_cleanup_stop_event = NULL;
static HANDLE g_lyricify_cleanup_thread = NULL;

static void install_shell_notify_iat_hook(void);
static void install_shell_notify_iat_hook_a(void);
static void inspect_window(HWND hwnd);

static const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *name = path;
    for (const wchar_t *p = path; p && *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            name = p + 1;
        }
    }
    return name;
}

static int is_previous_lyrics_hook(void *address) {
    MEMORY_BASIC_INFORMATION info;
    wchar_t module_path[MAX_PATH];
    const wchar_t *name;

    if (!address || VirtualQuery(address, &info, sizeof(info)) == 0) {
        return 0;
    }
    if (!info.AllocationBase ||
        !GetModuleFileNameW((HMODULE)info.AllocationBase, module_path, MAX_PATH)) {
        return 0;
    }
    name = base_name(module_path);
    return wcsstr(name, L"Lyrics Tray Icon Fix") != NULL &&
           wcsstr(name, L"Hook") != NULL;
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
    if (!tray_rule_should_write_ps_tray_factory(g_process_name)) {
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
    if (_wcsicmp(g_process_name, L"GoogleDriveFS.exe") == 0) {
        return !same_guid(&data->guidItem, &kGoogleDrivePreservedGuid);
    }
    if (!tray_rule_guid_for_process(g_process_name, &rule_guid)) {
        return 0;
    }

    return same_guid(&data->guidItem, &rule_guid);
}

static int should_block_uid_notify(DWORD message, const NOTIFYICONDATAW *data) {
    wchar_t class_name[256];

    if (!data || !data->hWnd || !IsWindow(data->hWnd)) {
        return 0;
    }
    if (message != NIM_ADD && message != NIM_MODIFY && message != NIM_SETVERSION) {
        return 0;
    }
    if (data->uFlags & NIF_GUID) {
        if (_wcsicmp(g_process_name, L"explorer.exe") != 0) {
            return 0;
        }
    }
    if (!GetClassNameW(data->hWnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return 0;
    }

    return tray_rule_block_uid_for_window(g_process_name, class_name, data->uID);
}

static BOOL WINAPI hooked_shell_notify_icon_w(DWORD message, PNOTIFYICONDATAW data) {
    int block_uid = should_block_uid_notify(message, data);
    int block_guid = should_block_guid_notify(message, data);
    if (block_uid) {
        return TRUE;
    }
    if (block_guid) {
        BOOL result = g_next_shell_notify_icon_w
            ? g_next_shell_notify_icon_w(message, data)
            : FALSE;
        if (result &&
            (message == NIM_ADD || message == NIM_MODIFY ||
             message == NIM_SETVERSION)) {
            g_next_shell_notify_icon_w(NIM_DELETE, data);
        }
        return result;
    }
    return g_next_shell_notify_icon_w ? g_next_shell_notify_icon_w(message, data) : FALSE;
}

static int should_block_uid_notify_a(DWORD message, const NOTIFYICONDATAA *data) {
    wchar_t class_name[256];

    if (!data || !data->hWnd || !IsWindow(data->hWnd)) {
        return 0;
    }
    if (message != NIM_ADD && message != NIM_MODIFY && message != NIM_SETVERSION) {
        return 0;
    }
    if (data->uFlags & NIF_GUID) {
        if (_wcsicmp(g_process_name, L"explorer.exe") != 0) {
            return 0;
        }
    }
    if (!GetClassNameW(data->hWnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return 0;
    }

    return tray_rule_block_uid_for_window(g_process_name, class_name, data->uID);
}

static int should_block_guid_notify_a(DWORD message, const NOTIFYICONDATAA *data) {
    GUID rule_guid;

    if (!data || !(data->uFlags & NIF_GUID)) {
        return 0;
    }
    if (message != NIM_ADD && message != NIM_MODIFY && message != NIM_SETVERSION) {
        return 0;
    }
    if (_wcsicmp(g_process_name, L"GoogleDriveFS.exe") == 0) {
        return !same_guid(&data->guidItem, &kGoogleDrivePreservedGuid);
    }
    if (!tray_rule_guid_for_process(g_process_name, &rule_guid)) {
        return 0;
    }

    return same_guid(&data->guidItem, &rule_guid);
}

static BOOL WINAPI hooked_shell_notify_icon_a(DWORD message, PNOTIFYICONDATAA data) {
    int block_uid = should_block_uid_notify_a(message, data);
    int block_guid = should_block_guid_notify_a(message, data);
    if (block_uid) {
        return TRUE;
    }
    if (block_guid) {
        BOOL result = g_next_shell_notify_icon_a
            ? g_next_shell_notify_icon_a(message, data)
            : FALSE;
        if (result &&
            (message == NIM_ADD || message == NIM_MODIFY ||
             message == NIM_SETVERSION)) {
            g_next_shell_notify_icon_a(NIM_DELETE, data);
        }
        return result;
    }
    return g_next_shell_notify_icon_a ? g_next_shell_notify_icon_a(message, data) : FALSE;
}

static BOOL CALLBACK google_drive_cleanup_proc(HWND hwnd, LPARAM lparam) {
    DWORD pid = 0;
    DWORD current_pid = (DWORD)lparam;
    wchar_t class_name[256];

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != current_pid) {
        return TRUE;
    }
    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return TRUE;
    }
    if (wcscmp(class_name, L"DriveDot") == 0) {
        ShowWindow(hwnd, SW_HIDE);
    }
    return TRUE;
}

static DWORD WINAPI google_drive_cleanup_thread(LPVOID param) {
    (void)param;
    Sleep(300);
    EnumWindows(google_drive_cleanup_proc, (LPARAM)GetCurrentProcessId());
    return 0;
}

static void CALLBACK google_drive_event_proc(
    HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object_id, LONG child_id,
    DWORD event_thread, DWORD event_time) {
    wchar_t class_name[256];

    (void)hook;
    (void)event;
    (void)child_id;
    (void)event_thread;
    (void)event_time;
    if (object_id != OBJID_WINDOW || !hwnd) {
        return;
    }
    if (GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0]))) &&
        wcscmp(class_name, L"DriveDot") == 0) {
        ShowWindow(hwnd, SW_HIDE);
    }
}

static DWORD WINAPI google_drive_event_thread(LPVOID param) {
    MSG message;

    (void)param;
    g_google_event_thread_id = GetCurrentThreadId();
    g_google_event_hook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL, google_drive_event_proc,
        GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_google_event_hook) {
        UnhookWinEvent(g_google_event_hook);
        g_google_event_hook = NULL;
    }
    g_google_event_thread_id = 0;
    return 0;
}

static void CALLBACK message_window_event_proc(
    HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object_id, LONG child_id,
    DWORD event_thread, DWORD event_time) {
    (void)hook;
    (void)event;
    (void)child_id;
    (void)event_thread;
    (void)event_time;
    if (object_id == OBJID_WINDOW && hwnd) {
        inspect_window(hwnd);
    }
}

static DWORD WINAPI message_window_event_thread(LPVOID param) {
    MSG message;

    (void)param;
    g_message_event_thread_id = GetCurrentThreadId();
    g_message_event_hook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL, message_window_event_proc,
        GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_message_event_hook) {
        UnhookWinEvent(g_message_event_hook);
        g_message_event_hook = NULL;
    }
    g_message_event_thread_id = 0;
    return 0;
}

static int hnotify_icon_class(const wchar_t *class_name) {
    return class_name && wcsncmp(class_name, L"H.NotifyIcon_", 13) == 0;
}

static int uid_icon_exists(HWND hwnd, unsigned int uid) {
    NOTIFYICONIDENTIFIER identifier;
    RECT rect;

    ZeroMemory(&identifier, sizeof(identifier));
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = hwnd;
    identifier.uID = uid;
    return Shell_NotifyIconGetRect(&identifier, &rect) == S_OK;
}

static void delete_uid_icon(HWND hwnd, unsigned int uid) {
    NOTIFYICONDATAW data;

    ZeroMemory(&data, sizeof(data));
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = uid;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

static DWORD WINAPI lyricify_cleanup_thread(LPVOID param) {
    HWND hwnd = (HWND)param;
    ULONGLONG start = GetTickCount64();

    while (GetTickCount64() - start < LYRICIFY_CLEANUP_MS) {
        if (uid_icon_exists(hwnd, 0)) {
            delete_uid_icon(hwnd, 0);
            if (!uid_icon_exists(hwnd, 0)) {
                break;
            }
        }
        if (g_lyricify_cleanup_stop_event &&
            WaitForSingleObject(g_lyricify_cleanup_stop_event,
                                LYRICIFY_CLEANUP_INTERVAL_MS) == WAIT_OBJECT_0) {
            break;
        }
    }
    return 0;
}

static void start_lyricify_cleanup(HWND hwnd) {
    HANDLE thread;

    if (!hwnd || InterlockedCompareExchange(&g_lyricify_cleanup_started, 1, 0) != 0) {
        return;
    }
    g_lyricify_cleanup_hwnd = hwnd;
    g_lyricify_cleanup_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, lyricify_cleanup_thread, hwnd, 0, NULL);
    if (!thread) {
        if (g_lyricify_cleanup_stop_event) {
            CloseHandle(g_lyricify_cleanup_stop_event);
            g_lyricify_cleanup_stop_event = NULL;
        }
        InterlockedExchange(&g_lyricify_cleanup_started, 0);
        return;
    }
    g_lyricify_cleanup_thread = thread;
}

static int patch_shell_notify_slot(void **function_slot, void *original, BYTE *module_base, SIZE_T module_size) {
    DWORD old_protect = 0;
    void *current;
    ShellNotifyIconWFn next;

    if (!function_slot || g_shell_hook_w_count >= MAX_SHELL_HOOK_SLOTS ||
        *function_slot == (void *)hooked_shell_notify_icon_w) {
        return 0;
    }
    for (int i = 0; i < g_shell_hook_w_count; ++i) {
        if (g_shell_hook_w[i].slot == function_slot) {
            return 0;
        }
    }

    current = *function_slot;
    if (is_previous_lyrics_hook(current)) {
        next = (ShellNotifyIconWFn)original;
    } else if (!current ||
        (BYTE *)current == original ||
        ((BYTE *)current >= module_base && (BYTE *)current < module_base + module_size)) {
        next = (ShellNotifyIconWFn)original;
    } else {
        next = (ShellNotifyIconWFn)current;
    }
    if (!VirtualProtect(function_slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return 0;
    }
    *function_slot = (void *)hooked_shell_notify_icon_w;
    VirtualProtect(function_slot, sizeof(void *), old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), function_slot, sizeof(void *));
    g_shell_hook_w[g_shell_hook_w_count].slot = function_slot;
    g_shell_hook_w[g_shell_hook_w_count].next = next;
    ++g_shell_hook_w_count;
    g_next_shell_notify_icon_w = next;
    return 1;
}

static int patch_shell_notify_slot_a(void **function_slot, void *original, BYTE *module_base, SIZE_T module_size) {
    DWORD old_protect = 0;
    void *current;
    ShellNotifyIconAFn next;

    if (!function_slot || g_shell_hook_a_count >= MAX_SHELL_HOOK_SLOTS ||
        *function_slot == (void *)hooked_shell_notify_icon_a) {
        return 0;
    }
    for (int i = 0; i < g_shell_hook_a_count; ++i) {
        if (g_shell_hook_a[i].slot == function_slot) {
            return 0;
        }
    }

    current = *function_slot;
    if (is_previous_lyrics_hook(current)) {
        next = (ShellNotifyIconAFn)original;
    } else if (!current ||
        (BYTE *)current == original ||
        ((BYTE *)current >= module_base && (BYTE *)current < module_base + module_size)) {
        next = (ShellNotifyIconAFn)original;
    } else {
        next = (ShellNotifyIconAFn)current;
    }
    if (!VirtualProtect(function_slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return 0;
    }
    *function_slot = (void *)hooked_shell_notify_icon_a;
    VirtualProtect(function_slot, sizeof(void *), old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), function_slot, sizeof(void *));
    g_shell_hook_a[g_shell_hook_a_count].slot = function_slot;
    g_shell_hook_a[g_shell_hook_a_count].next = next;
    ++g_shell_hook_a_count;
    g_next_shell_notify_icon_a = next;
    return 1;
}

static void restore_shell_notify_iat_hook(void) {
    DWORD old_protect = 0;

    for (int i = 0; i < g_shell_hook_w_count; ++i) {
        if (g_shell_hook_w[i].slot &&
            *g_shell_hook_w[i].slot == (void *)hooked_shell_notify_icon_w) {
            if (VirtualProtect(g_shell_hook_w[i].slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
                *g_shell_hook_w[i].slot = (void *)g_shell_hook_w[i].next;
                VirtualProtect(g_shell_hook_w[i].slot, sizeof(void *), old_protect, &old_protect);
                FlushInstructionCache(GetCurrentProcess(), g_shell_hook_w[i].slot, sizeof(void *));
            }
        }
        ZeroMemory(&g_shell_hook_w[i], sizeof(g_shell_hook_w[i]));
    }
    for (int i = 0; i < g_shell_hook_a_count; ++i) {
        if (g_shell_hook_a[i].slot &&
            *g_shell_hook_a[i].slot == (void *)hooked_shell_notify_icon_a) {
            if (VirtualProtect(g_shell_hook_a[i].slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
                *g_shell_hook_a[i].slot = (void *)g_shell_hook_a[i].next;
                VirtualProtect(g_shell_hook_a[i].slot, sizeof(void *), old_protect, &old_protect);
                FlushInstructionCache(GetCurrentProcess(), g_shell_hook_a[i].slot, sizeof(void *));
            }
        }
        ZeroMemory(&g_shell_hook_a[i], sizeof(g_shell_hook_a[i]));
    }
    g_shell_hook_w_count = 0;
    g_shell_hook_a_count = 0;
    g_shell_notify_iat_hooked = 0;
    InterlockedExchange(&g_install_state, 0);
}

static int install_inline_shell_hook(BYTE *target, BYTE **trampoline_out, BYTE *original,
                                     void *hook) {
    BYTE *trampoline;
    DWORD old_protect = 0;
    BYTE *return_address;
    BYTE *hook_address;

    if (!target || !trampoline_out || *trampoline_out || !hook) {
        return 0;
    }
    trampoline = (BYTE *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        return 0;
    }
    memcpy(trampoline, target, 14);
    trampoline[14] = 0xFF;
    trampoline[15] = 0x25;
    trampoline[16] = 0;
    trampoline[17] = 0;
    trampoline[18] = 0;
    trampoline[19] = 0;
    return_address = target + 14;
    memcpy(trampoline + 20, &return_address, sizeof(return_address));

    if (!VirtualProtect(target, 14, PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return 0;
    }
    memcpy(original, target, 14);
    target[0] = 0xFF;
    target[1] = 0x25;
    target[2] = 0;
    target[3] = 0;
    target[4] = 0;
    target[5] = 0;
    hook_address = (BYTE *)hook;
    memcpy(target + 6, &hook_address, sizeof(hook_address));
    VirtualProtect(target, 14, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), target, 14);

    *trampoline_out = trampoline;
    return 1;
}

static void restore_inline_shell_hooks(void) {
    DWORD old_protect = 0;

    if (g_inline_w_target && g_inline_w_trampoline) {
        if (VirtualProtect(g_inline_w_target, 14, PAGE_EXECUTE_READWRITE, &old_protect)) {
            memcpy(g_inline_w_target, g_inline_w_original, 14);
            VirtualProtect(g_inline_w_target, 14, old_protect, &old_protect);
            FlushInstructionCache(GetCurrentProcess(), g_inline_w_target, 14);
        }
        VirtualFree(g_inline_w_trampoline, 0, MEM_RELEASE);
        g_inline_w_trampoline = NULL;
        g_inline_w_target = NULL;
    }
    if (g_inline_a_target && g_inline_a_trampoline) {
        if (VirtualProtect(g_inline_a_target, 14, PAGE_EXECUTE_READWRITE, &old_protect)) {
            memcpy(g_inline_a_target, g_inline_a_original, 14);
            VirtualProtect(g_inline_a_target, 14, old_protect, &old_protect);
            FlushInstructionCache(GetCurrentProcess(), g_inline_a_target, 14);
        }
        VirtualFree(g_inline_a_trampoline, 0, MEM_RELEASE);
        g_inline_a_trampoline = NULL;
        g_inline_a_target = NULL;
    }
    g_inline_hooked = 0;
}

static int is_our_hook_module(HMODULE module) {
    wchar_t module_path[MAX_PATH];
    const wchar_t *name;

    if (!module || !GetModuleFileNameW(module, module_path, MAX_PATH)) {
        return 0;
    }
    name = base_name(module_path);
    return wcsstr(name, L"Lyrics Tray Icon Fix Hook") != NULL;
}

static void scan_module_shell_imports(HMODULE module, FARPROC original_w, FARPROC original_a) {
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY import_dir;
    IMAGE_DATA_DIRECTORY delay_import_dir;
    IMAGE_IMPORT_DESCRIPTOR *import_desc;
    SIZE_T module_size;
    wchar_t module_path[MAX_PATH];
    const wchar_t *module_name = L"";
    int allow_delay_import = 0;

    if (!module || is_our_hook_module(module)) {
        return;
    }
    if (GetModuleFileNameW(module, module_path, MAX_PATH)) {
        module_name = base_name(module_path);
        allow_delay_import = _wcsicmp(module_name, L"stobject.dll") == 0;
    }
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    module_size = nt->OptionalHeader.SizeOfImage;

    import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_dir.VirtualAddress) {
        import_desc = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)module + import_dir.VirtualAddress);
        for (; import_desc->Name; ++import_desc) {
            IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)((BYTE *)module + import_desc->FirstThunk);
            IMAGE_THUNK_DATA *lookup = import_desc->OriginalFirstThunk
                ? (IMAGE_THUNK_DATA *)((BYTE *)module + import_desc->OriginalFirstThunk)
                : NULL;
            for (; thunk && thunk->u1.Function; ++thunk) {
                void **function_slot = (void **)&thunk->u1.Function;
                int is_w = *function_slot == (void *)original_w;
                int is_a = *function_slot == (void *)original_a;

                if (lookup) {
                    if (!IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) {
                        IMAGE_IMPORT_BY_NAME *import_name =
                            (IMAGE_IMPORT_BY_NAME *)((BYTE *)module + lookup->u1.AddressOfData);
                        if (strcmp((const char *)import_name->Name, "Shell_NotifyIconW") == 0) {
                            is_w = 1;
                            is_a = 0;
                        } else if (strcmp((const char *)import_name->Name, "Shell_NotifyIconA") == 0) {
                            is_a = 1;
                            is_w = 0;
                        } else {
                            is_w = 0;
                            is_a = 0;
                        }
                    } else {
                        is_w = 0;
                        is_a = 0;
                    }
                    ++lookup;
                }

                if (is_w && patch_shell_notify_slot(function_slot, (void *)original_w,
                                                    (BYTE *)module, module_size)) {
                    g_shell_notify_iat_hooked = 1;
                }
                if (is_a && patch_shell_notify_slot_a(function_slot, (void *)original_a,
                                                      (BYTE *)module, module_size)) {
                    g_shell_notify_iat_hooked = 1;
                }
            }
        }
    }

    delay_import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (allow_delay_import && delay_import_dir.VirtualAddress) {
        DelayImportDescriptor *delay_desc =
            (DelayImportDescriptor *)((BYTE *)module + delay_import_dir.VirtualAddress);
        for (; delay_desc->name; ++delay_desc) {
            IMAGE_THUNK_DATA *thunk;
            IMAGE_THUNK_DATA *lookup;

            if (!(delay_desc->attributes & 1) ||
                !delay_desc->import_address_table || !delay_desc->import_name_table) {
                continue;
            }
            thunk = (IMAGE_THUNK_DATA *)((BYTE *)module + delay_desc->import_address_table);
            lookup = (IMAGE_THUNK_DATA *)((BYTE *)module + delay_desc->import_name_table);
            for (; thunk->u1.Function && lookup->u1.AddressOfData; ++thunk, ++lookup) {
                IMAGE_IMPORT_BY_NAME *import_name;
                if (IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) {
                    continue;
                }
                import_name = (IMAGE_IMPORT_BY_NAME *)((BYTE *)module + lookup->u1.AddressOfData);
                if (strcmp((const char *)import_name->Name, "Shell_NotifyIconW") == 0 &&
                    patch_shell_notify_slot((void **)&thunk->u1.Function, (void *)original_w,
                                            (BYTE *)module, module_size)) {
                    g_shell_notify_iat_hooked = 1;
                }
                if (strcmp((const char *)import_name->Name, "Shell_NotifyIconA") == 0 &&
                    patch_shell_notify_slot_a((void **)&thunk->u1.Function, (void *)original_a,
                                              (BYTE *)module, module_size)) {
                    g_shell_notify_iat_hooked = 1;
                }
            }
        }
    }
}

static void install_shell_notify_iat_hook(void) {
    HMODULE shell32;
    FARPROC original_w;
    FARPROC original_a;

    if (InterlockedCompareExchange(&g_install_state, 1, 0) != 0) {
        return;
    }
    shell32 = GetModuleHandleW(L"shell32.dll");
    if (!shell32) {
        shell32 = LoadLibraryW(L"shell32.dll");
        if (!shell32) {
            InterlockedExchange(&g_install_state, 0);
            return;
        }
    }
    original_w = GetProcAddress(shell32, "Shell_NotifyIconW");
    original_a = GetProcAddress(shell32, "Shell_NotifyIconA");
    if (!original_w || !original_a) {
        InterlockedExchange(&g_install_state, 0);
        return;
    }

    if (_wcsicmp(g_process_name, L"explorer.exe") == 0 ||
        _wcsicmp(g_process_name, L"ChatGPT.exe") == 0) {
        g_next_shell_notify_icon_w = (ShellNotifyIconWFn)original_w;
        g_next_shell_notify_icon_a = (ShellNotifyIconAFn)original_a;
        if (install_inline_shell_hook((BYTE *)original_w, &g_inline_w_trampoline,
                                      g_inline_w_original, hooked_shell_notify_icon_w)) {
            g_inline_w_target = (BYTE *)original_w;
            g_next_shell_notify_icon_w = (ShellNotifyIconWFn)g_inline_w_trampoline;
            g_shell_notify_iat_hooked = 1;
            g_inline_hooked = 1;
        }
        if (install_inline_shell_hook((BYTE *)original_a, &g_inline_a_trampoline,
                                      g_inline_a_original, hooked_shell_notify_icon_a)) {
            g_inline_a_target = (BYTE *)original_a;
            g_next_shell_notify_icon_a = (ShellNotifyIconAFn)g_inline_a_trampoline;
            g_shell_notify_iat_hooked = 1;
            g_inline_hooked = 1;
        }
    } else {
        scan_module_shell_imports(GetModuleHandleW(NULL), original_w, original_a);
    }

    if (g_shell_notify_iat_hooked && !g_shell_notify_ready_event) {
        const wchar_t *event_name = NULL;
        if (_wcsicmp(g_process_name, L"explorer.exe") == 0) {
            event_name = L"Local\\LyricsTrayIconFixShellBlockExplorerReady";
        } else if (_wcsicmp(g_process_name, L"GoogleDriveFS.exe") == 0) {
            event_name = L"Local\\LyricsTrayIconFixShellBlockGoogleDriveReady";
        }
        if (event_name) {
            g_shell_notify_ready_event = CreateEventW(NULL, TRUE, TRUE, event_name);
        }
    }
    InterlockedExchange(&g_install_state, g_shell_notify_iat_hooked ? 2 : 0);
}
static void inspect_window(HWND hwnd) {
    wchar_t class_name[256];
    wchar_t window_text[256];
    unsigned int uid = 0;
    GUID guid;

    int uses_shell_block = tray_rule_process_uses_shell_notify_block(g_process_name);
    if (uses_shell_block && !g_shell_notify_iat_hooked) {
        install_shell_notify_iat_hook();
        return;
    }
    if (uses_shell_block && !tray_rule_process_uses_message_hook(g_process_name)) {
        return;
    }

    if (!g_process_is_target || !hwnd) {
        return;
    }

    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return;
    }

    if (hnotify_icon_class(class_name) &&
        (_wcsicmp(g_process_name, L"Lyricify Lite.exe") == 0 ||
         _wcsicmp(g_process_name, L"BetterLyrics.WinUI3.exe") == 0)) {
        start_lyricify_cleanup(hwnd);
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

    for (int i = 0; i < tray_block_uid_rule_count() && i < MAX_BLOCK_DELETE_STATES; ++i) {
        BlockDeleteState *state = &g_block_delete_states[i];
        DWORD now;

        if (!tray_rule_block_uid_uses_message_hook(i) ||
            !tray_rule_block_uid_for_window_class_at(g_process_name, class_name, i, &uid)) {
            continue;
        }
        now = GetTickCount();
        if (state->last_delete_tick == 0 ||
            wcscmp(state->class_name, class_name) != 0 ||
            now - state->last_delete_tick >= 1000) {
            NOTIFYICONDATAW data;
            ZeroMemory(&data, sizeof(data));
            data.cbSize = sizeof(data);
            data.hWnd = hwnd;
            data.uID = uid;
            state->last_delete_tick = now;
            wcsncpy(state->class_name, class_name,
                    (sizeof(state->class_name) / sizeof(state->class_name[0])) - 1);
            state->class_name[(sizeof(state->class_name) /
                               sizeof(state->class_name[0])) - 1] = L'\0';
            Shell_NotifyIconW(NIM_DELETE, &data);
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
            g_process_is_target = tray_rule_process_uses_message_hook(g_process_name);
            if (_wcsicmp(g_process_name, L"LYRICIFY LITE.EXE") == 0 ||
                _wcsicmp(g_process_name, L"BETTERLYRICS.WINUI3.EXE") == 0) {
                HANDLE thread = CreateThread(
                    NULL, 0, message_window_event_thread, NULL, 0, NULL);
                if (thread) {
                    g_message_event_thread_handle = thread;
                }
            }
            if (tray_rule_process_uses_shell_notify_block(g_process_name)) {
                install_shell_notify_iat_hook();
                if (_wcsicmp(g_process_name, L"GoogleDriveFS.exe") == 0) {
                    HANDLE thread = CreateThread(
                        NULL, 0, google_drive_cleanup_thread, NULL, 0, NULL);
                    if (thread) {
                        g_google_cleanup_thread_handle = thread;
                    }
                    thread = CreateThread(
                        NULL, 0, google_drive_event_thread, NULL, 0, NULL);
                    if (thread) {
                        g_google_event_thread_handle = thread;
                    }
                }
            }
        }
    } else if (reason == DLL_PROCESS_DETACH && reserved == NULL) {
        restore_inline_shell_hooks();
        restore_shell_notify_iat_hook();
        if (g_shell_notify_ready_event) {
            CloseHandle(g_shell_notify_ready_event);
            g_shell_notify_ready_event = NULL;
        }
        if (g_google_event_thread_id) {
            PostThreadMessageW(g_google_event_thread_id, WM_QUIT, 0, 0);
        }
        if (g_message_event_thread_id) {
            PostThreadMessageW(g_message_event_thread_id, WM_QUIT, 0, 0);
        }
        if (g_lyricify_cleanup_stop_event) {
            SetEvent(g_lyricify_cleanup_stop_event);
        }
        if (g_google_event_thread_handle) {
            WaitForSingleObject(g_google_event_thread_handle, 3000);
            CloseHandle(g_google_event_thread_handle);
            g_google_event_thread_handle = NULL;
        }
        if (g_google_cleanup_thread_handle) {
            WaitForSingleObject(g_google_cleanup_thread_handle, 3000);
            CloseHandle(g_google_cleanup_thread_handle);
            g_google_cleanup_thread_handle = NULL;
        }
        if (g_message_event_thread_handle) {
            WaitForSingleObject(g_message_event_thread_handle, 3000);
            CloseHandle(g_message_event_thread_handle);
            g_message_event_thread_handle = NULL;
        }
        if (g_lyricify_cleanup_thread) {
            WaitForSingleObject(g_lyricify_cleanup_thread, 1500);
            CloseHandle(g_lyricify_cleanup_thread);
            g_lyricify_cleanup_thread = NULL;
        }
        if (g_lyricify_cleanup_stop_event) {
            CloseHandle(g_lyricify_cleanup_stop_event);
            g_lyricify_cleanup_stop_event = NULL;
        }
    }

    return TRUE;
}
