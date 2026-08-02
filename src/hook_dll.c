#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

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

static ShellNotifyIconWFn g_next_shell_notify_icon_w = NULL;
static ShellNotifyIconAFn g_next_shell_notify_icon_a = NULL;
static int g_shell_notify_iat_hooked = 0;
static volatile LONG g_install_state = 0;
static HANDLE g_shell_notify_ready_event = NULL;
static void **g_patched_shell_notify_slot = NULL;
static void **g_patched_shell_notify_slot_a = NULL;
static HWINEVENTHOOK g_google_event_hook = NULL;
static DWORD g_google_event_thread_id = 0;

static void install_shell_notify_iat_hook(void);

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

    if (!data || !data->hWnd) {
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
        return TRUE;
    }
    return g_next_shell_notify_icon_w ? g_next_shell_notify_icon_w(message, data) : FALSE;
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

static int patch_shell_notify_slot(void **function_slot, void *original, BYTE *module_base, SIZE_T module_size) {
    DWORD old_protect = 0;
    void *current;

    if (!function_slot || g_patched_shell_notify_slot ||
        *function_slot == (void *)hooked_shell_notify_icon_w) {
        return 0;
    }

    current = *function_slot;
    if (!VirtualProtect(function_slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return 0;
    }

    if ((BYTE *)current >= module_base && (BYTE *)current < module_base + module_size) {
        g_next_shell_notify_icon_w = (ShellNotifyIconWFn)original;
    } else {
        g_next_shell_notify_icon_w = (ShellNotifyIconWFn)current;
    }
    *function_slot = (void *)hooked_shell_notify_icon_w;
    VirtualProtect(function_slot, sizeof(void *), old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), function_slot, sizeof(void *));
    g_patched_shell_notify_slot = function_slot;
    return 1;
}

static void restore_shell_notify_iat_hook(void) {
    DWORD old_protect = 0;

    if (!g_patched_shell_notify_slot || !g_next_shell_notify_icon_w ||
        *g_patched_shell_notify_slot != (void *)hooked_shell_notify_icon_w) {
        return;
    }
    if (VirtualProtect(g_patched_shell_notify_slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        *g_patched_shell_notify_slot = (void *)g_next_shell_notify_icon_w;
        VirtualProtect(g_patched_shell_notify_slot, sizeof(void *), old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), g_patched_shell_notify_slot, sizeof(void *));
    }
    g_patched_shell_notify_slot = NULL;
    g_shell_notify_iat_hooked = 0;
    InterlockedExchange(&g_install_state, 0);
}

static void install_shell_notify_iat_hook(void) {
    HMODULE module;
    HMODULE shell32;
    FARPROC original;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY import_dir;
    IMAGE_DATA_DIRECTORY delay_import_dir;
    IMAGE_IMPORT_DESCRIPTOR *import_desc;
    SIZE_T module_size;

    if (InterlockedCompareExchange(&g_install_state, 1, 0) != 0) {
        return;
    }

    if (_wcsicmp(g_process_name, L"explorer.exe") == 0) {
        module = GetModuleHandleW(L"stobject.dll");
    } else {
        module = GetModuleHandleW(NULL);
    }
    shell32 = GetModuleHandleW(L"shell32.dll");
    if (!module) {
        InterlockedExchange(&g_install_state, 0);
        return;
    }
    if (!shell32) {
        shell32 = LoadLibraryW(L"shell32.dll");
        if (!shell32) {
            InterlockedExchange(&g_install_state, 0);
            return;
        }
    }

    original = GetProcAddress(shell32, "Shell_NotifyIconW");
    if (!original) {
        InterlockedExchange(&g_install_state, 0);
        return;
    }
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        InterlockedExchange(&g_install_state, 0);
        return;
    }
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        InterlockedExchange(&g_install_state, 0);
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
                int is_shell_notify_icon_w = *function_slot == (void *)original;

                if (lookup) {
                    if (!IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) {
                        IMAGE_IMPORT_BY_NAME *import_name =
                            (IMAGE_IMPORT_BY_NAME *)((BYTE *)module + lookup->u1.AddressOfData);
                        is_shell_notify_icon_w = strcmp((const char *)import_name->Name, "Shell_NotifyIconW") == 0;
                    } else {
                        is_shell_notify_icon_w = 0;
                    }
                    ++lookup;
                }

                if (is_shell_notify_icon_w && patch_shell_notify_slot(function_slot, (void *)original,
                                                                       (BYTE *)module, module_size)) {
                    g_shell_notify_iat_hooked = 1;
                    goto installed;
                }
            }
        }
    }

    delay_import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (delay_import_dir.VirtualAddress) {
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
                    patch_shell_notify_slot((void **)&thunk->u1.Function, (void *)original,
                                            (BYTE *)module, module_size)) {
                    g_shell_notify_iat_hooked = 1;
                    goto installed;
                }
            }
        }
    }

installed:
    if (g_shell_notify_iat_hooked && !g_shell_notify_ready_event) {
        const wchar_t *event_name = _wcsicmp(g_process_name, L"explorer.exe") == 0
            ? L"Local\\LyricsTrayIconFixShellBlockExplorerReady"
            : L"Local\\LyricsTrayIconFixShellBlockGoogleDriveReady";
        g_shell_notify_ready_event = CreateEventW(NULL, TRUE, TRUE, event_name);
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
            if (tray_rule_process_uses_shell_notify_block(g_process_name)) {
                install_shell_notify_iat_hook();
                if (_wcsicmp(g_process_name, L"GoogleDriveFS.exe") == 0) {
                    HANDLE thread = CreateThread(
                        NULL, 0, google_drive_cleanup_thread, NULL, 0, NULL);
                    if (thread) {
                        CloseHandle(thread);
                    }
                    thread = CreateThread(
                        NULL, 0, google_drive_event_thread, NULL, 0, NULL);
                    if (thread) {
                        CloseHandle(thread);
                    }
                }
            }
        }
    } else if (reason == DLL_PROCESS_DETACH && reserved == NULL) {
        restore_shell_notify_iat_hook();
        if (g_shell_notify_ready_event) {
            CloseHandle(g_shell_notify_ready_event);
            g_shell_notify_ready_event = NULL;
        }
        if (g_google_event_thread_id) {
            PostThreadMessageW(g_google_event_thread_id, WM_QUIT, 0, 0);
        }
    }

    return TRUE;
}
