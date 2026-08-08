#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#include "rules.h"

#define TOOL_VERSION L"v0.107"
#define MUTEX_NAME L"Local\\LyricsTrayIconFixMutex"
#define STOP_EVENT_NAME L"Local\\LyricsTrayIconFixStop"
#define SYNC_EVENT_NAME L"Local\\LyricsTrayIconFixSync"
#define DLL_NAME L"Lyrics Tray Icon Fix Hook v0.107.dll"
#define EXPLORER_HOOK_READY_EVENT_NAME L"Local\\LyricsTrayIconFixShellBlockExplorerReady"
#define GOOGLE_DRIVE_HOOK_READY_EVENT_NAME L"Local\\LyricsTrayIconFixShellBlockGoogleDriveReady"
#define PSTF_THREAD_HOOK_EVENT_NAME L"Local\\LyricsTrayIconFixPstfThreadHookInstalled"
#define PSTF_RESTORE_READY_EVENT_NAME L"Local\\LyricsTrayIconFixPstfRestoreReady"
#define EXPLORER_WATCH_READY_EVENT_NAME L"Local\\LyricsTrayIconFixExplorerWatchReady"
#define STARTUP_WATCH_MS 15000
#define STARTUP_WATCH_INTERVAL_MS 50
#define EXPLORER_CLEANUP_MS 60000
#define EXPLORER_CLEANUP_INTERVAL_MS 200
#define MAX_GD_WATCH_PROCESSES 16
#define CHATGPT_CLEANUP_MS 10000
#define CHATGPT_CLEANUP_INTERVAL_MS 200
#define CHATGPT_UID_SCAN_LIMIT 1024

typedef struct SyncWorkerContext {
    HANDLE stop_event;
    HANDLE sync_event;
    wchar_t queue_path[MAX_PATH];
} SyncWorkerContext;

typedef struct CurrentScanContext {
    int matches;
    int hidden_windows;
    int hidden_guid_icons;
} CurrentScanContext;

#define MAX_TARGET_THREAD_HOOKS 512
#define MAX_KNOWN_TARGET_PIDS 64
typedef struct ThreadHook {
    DWORD pid;
    DWORD thread_id;
    HHOOK call_hook;
    HHOOK msg_hook;
    HANDLE process;
    HANDLE thread;
} ThreadHook;

static HMODULE g_hook_dll = NULL;
static HOOKPROC g_call_proc = NULL;
static HOOKPROC g_msg_proc = NULL;
static HWINEVENTHOOK g_target_win_event_hook = NULL;
static ThreadHook g_target_thread_hooks[MAX_TARGET_THREAD_HOOKS];
static int g_target_thread_hook_count = 0;
static DWORD g_known_target_pids[MAX_KNOWN_TARGET_PIDS];
static int g_known_target_pid_count = 0;
static HANDLE g_shutdown_stop_event = NULL;
static volatile LONG g_shutdown_requested = 0;
static HANDLE g_startup_watcher_thread = NULL;
static HANDLE g_explorer_cleanup_thread = NULL;
static HANDLE g_google_drive_watcher_thread = NULL;
static HANDLE g_chatgpt_cleanup_thread = NULL;
static HANDLE g_chatgpt_appeared_event = NULL;
static HANDLE g_google_drive_appeared_event = NULL;
static HANDLE g_service_stop_event = NULL;

static int event_is_signaled(const wchar_t *name);
static CurrentScanContext sync_current_windows(void);
static void start_chatgpt_cleanup(void);

static void write_utf8(HANDLE handle, const wchar_t *format, ...) {
    wchar_t wide[2048];
    char utf8[8192];
    va_list args;
    va_start(args, format);
    _vsnwprintf(wide, sizeof(wide) / sizeof(wide[0]) - 1, format, args);
    wide[(sizeof(wide) / sizeof(wide[0])) - 1] = L'\0';
    va_end(args);

    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (bytes > 1) {
        DWORD written = 0;
        WriteFile(handle, utf8, (DWORD)(bytes - 1), &written, NULL);
    }
}

static void out(const wchar_t *format, ...) {
    wchar_t wide[2048];
    va_list args;
    va_start(args, format);
    _vsnwprintf(wide, sizeof(wide) / sizeof(wide[0]) - 1, format, args);
    wide[(sizeof(wide) / sizeof(wide[0])) - 1] = L'\0';
    va_end(args);
    write_utf8(GetStdHandle(STD_OUTPUT_HANDLE), L"%ls", wide);
}

static void err(const wchar_t *format, ...) {
    wchar_t wide[2048];
    va_list args;
    va_start(args, format);
    _vsnwprintf(wide, sizeof(wide) / sizeof(wide[0]) - 1, format, args);
    wide[(sizeof(wide) / sizeof(wide[0])) - 1] = L'\0';
    va_end(args);
    write_utf8(GetStdHandle(STD_ERROR_HANDLE), L"%ls", wide);
}

static const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *name = path;
    for (const wchar_t *p = path; p && *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            name = p + 1;
        }
    }
    return name;
}

static void exe_directory(wchar_t *buffer, DWORD count) {
    DWORD len = GetModuleFileNameW(NULL, buffer, count);
    if (!len || len >= count) {
        buffer[0] = L'.';
        buffer[1] = L'\0';
        return;
    }

    for (DWORD i = len; i > 0; --i) {
        if (buffer[i - 1] == L'\\' || buffer[i - 1] == L'/') {
            buffer[i - 1] = L'\0';
            return;
        }
    }
}

static int get_process_base_name(DWORD pid, wchar_t *buffer, DWORD count) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return 0;
    }

    wchar_t path[MAX_PATH];
    DWORD path_count = MAX_PATH;
    int ok = QueryFullProcessImageNameW(process, 0, path, &path_count);
    CloseHandle(process);
    if (!ok) {
        return 0;
    }

    wcsncpy(buffer, base_name(path), count - 1);
    buffer[count - 1] = L'\0';
    return 1;
}

static HANDLE open_shell_process(DWORD *pid_out) {
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    DWORD current_session = 0;

    if (pid_out) {
        *pid_out = 0;
    }
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session)) {
        return NULL;
    }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            DWORD candidate_session = 0;
            HANDLE process;
            if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0 ||
                !ProcessIdToSessionId(entry.th32ProcessID, &candidate_session) ||
                candidate_session != current_session) {
                continue;
            }
            process = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (process) {
                if (pid_out) {
                    *pid_out = entry.th32ProcessID;
                }
                CloseHandle(snapshot);
                return process;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return NULL;
}

static void nudge_process_threads(DWORD pid) {
    HANDLE snapshot;
    THREADENTRY32 entry;

    if (!pid) {
        return;
    }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                PostThreadMessageW(entry.th32ThreadID, WM_NULL, 0, 0);
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static void nudge_hook_target_threads(void) {
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    DWORD current_session = 0;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session)) {
        return;
    }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            DWORD candidate_session = 0;
            if (!tray_rule_process_is_target(entry.szExeFile) ||
                !ProcessIdToSessionId(entry.th32ProcessID, &candidate_session) ||
                candidate_session != current_session) {
                continue;
            }
            nudge_process_threads(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static int launch_self_command(const wchar_t *arguments) {
    wchar_t exe_path[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t command_line[MAX_PATH * 2];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;

    if (!GetModuleFileNameW(NULL, exe_path, MAX_PATH)) {
        return 0;
    }
    exe_directory(directory, MAX_PATH);
    _snwprintf(command_line, (sizeof(command_line) / sizeof(command_line[0])) - 1,
               L"\"%ls\" %ls", exe_path, arguments);
    command_line[(sizeof(command_line) / sizeof(command_line[0])) - 1] = L'\0';
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(exe_path, command_line, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, directory, &startup, &process)) {
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 1;
}

static int pid_is_target_process(DWORD pid) {
    wchar_t name[MAX_PATH];
    if (!get_process_base_name(pid, name, MAX_PATH)) {
        return 0;
    }
    return tray_rule_process_is_target(name);
}

static int known_target_pid(DWORD pid) {
    for (int i = 0; i < g_known_target_pid_count; ++i) {
        if (g_known_target_pids[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static int add_known_target_pid(DWORD pid) {
    if (known_target_pid(pid) || g_known_target_pid_count >= MAX_KNOWN_TARGET_PIDS) {
        return 0;
    }
    g_known_target_pids[g_known_target_pid_count++] = pid;
    return 1;
}

static void remove_target_hook_at(int index) {
    if (index < 0 || index >= g_target_thread_hook_count) {
        return;
    }
    if (g_target_thread_hooks[index].call_hook) {
        UnhookWindowsHookEx(g_target_thread_hooks[index].call_hook);
    }
    if (g_target_thread_hooks[index].msg_hook) {
        UnhookWindowsHookEx(g_target_thread_hooks[index].msg_hook);
    }
    if (g_target_thread_hooks[index].thread) {
        CloseHandle(g_target_thread_hooks[index].thread);
    }
    if (g_target_thread_hooks[index].process) {
        CloseHandle(g_target_thread_hooks[index].process);
    }
    --g_target_thread_hook_count;
    if (index != g_target_thread_hook_count) {
        g_target_thread_hooks[index] = g_target_thread_hooks[g_target_thread_hook_count];
    }
    ZeroMemory(&g_target_thread_hooks[g_target_thread_hook_count], sizeof(ThreadHook));
}

static void remove_finished_target_hooks(void) {
    for (int i = g_target_thread_hook_count - 1; i >= 0; --i) {
        DWORD process_wait = g_target_thread_hooks[i].process
            ? WaitForSingleObject(g_target_thread_hooks[i].process, 0)
            : WAIT_OBJECT_0;
        DWORD thread_wait = g_target_thread_hooks[i].thread
            ? WaitForSingleObject(g_target_thread_hooks[i].thread, 0)
            : WAIT_OBJECT_0;
        if (process_wait == WAIT_OBJECT_0 || thread_wait == WAIT_OBJECT_0) {
            remove_target_hook_at(i);
        }
    }
}

static void write_watchdog_stop_marker(void) {
    wchar_t dir[MAX_PATH];
    wchar_t marker[MAX_PATH];
    HANDLE file;
    DWORD written = 0;
    static const char content[] = "shutdown\n";

    exe_directory(dir, MAX_PATH);
    _snwprintf(marker, MAX_PATH - 1, L"%ls\\watchdog-stop.txt", dir);
    marker[MAX_PATH - 1] = L'\0';
    file = CreateFileW(marker, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, content, (DWORD)(sizeof(content) - 1), &written, NULL);
        CloseHandle(file);
    }
}

static int system_shutting_down(void) {
    if (InterlockedCompareExchange(&g_shutdown_requested, 0, 0)) {
        return 1;
    }
    if (GetSystemMetrics(SM_SHUTTINGDOWN)) {
        InterlockedExchange(&g_shutdown_requested, 1);
        write_watchdog_stop_marker();
        return 1;
    }
    return 0;
}

static BOOL WINAPI console_ctrl_handler(DWORD type) {
    if (type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        InterlockedExchange(&g_shutdown_requested, 1);
        write_watchdog_stop_marker();
        if (g_shutdown_stop_event) {
            SetEvent(g_shutdown_stop_event);
        }
        return FALSE;
    }
    return FALSE;
}

static void remove_all_target_hooks(void) {
    while (g_target_thread_hook_count > 0) {
        remove_target_hook_at(g_target_thread_hook_count - 1);
    }
}

static int find_target_thread_hook(DWORD pid, DWORD thread_id) {
    for (int i = 0; i < g_target_thread_hook_count; ++i) {
        if (g_target_thread_hooks[i].pid == pid &&
            g_target_thread_hooks[i].thread_id == thread_id) {
            return 1;
        }
    }
    return 0;
}

static int process_has_target_hook(DWORD pid) {
    for (int i = 0; i < g_target_thread_hook_count; ++i) {
        if (g_target_thread_hooks[i].pid == pid) {
            return 1;
        }
    }
    return 0;
}

static void hook_target_thread(DWORD pid, DWORD thread_id) {
    HHOOK call_hook;
    HHOOK msg_hook;
    HANDLE process;
    HANDLE thread;

    if (!pid || !thread_id || !g_hook_dll || !g_call_proc || !g_msg_proc ||
        find_target_thread_hook(pid, thread_id)) {
        return;
    }
    remove_finished_target_hooks();
    if (g_target_thread_hook_count >= MAX_TARGET_THREAD_HOOKS) {
        return;
    }

    call_hook = SetWindowsHookExW(WH_CALLWNDPROC, g_call_proc, g_hook_dll, thread_id);
    msg_hook = SetWindowsHookExW(WH_GETMESSAGE, g_msg_proc, g_hook_dll, thread_id);
    if (!call_hook && !msg_hook) {
        return;
    }
    process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    thread = OpenThread(SYNCHRONIZE, FALSE, thread_id);
    if (!process || !thread) {
        if (call_hook) UnhookWindowsHookEx(call_hook);
        if (msg_hook) UnhookWindowsHookEx(msg_hook);
        if (thread) CloseHandle(thread);
        if (process) CloseHandle(process);
        return;
    }

    g_target_thread_hooks[g_target_thread_hook_count].pid = pid;
    g_target_thread_hooks[g_target_thread_hook_count].thread_id = thread_id;
    g_target_thread_hooks[g_target_thread_hook_count].call_hook = call_hook;
    g_target_thread_hooks[g_target_thread_hook_count].msg_hook = msg_hook;
    g_target_thread_hooks[g_target_thread_hook_count].process = process;
    g_target_thread_hooks[g_target_thread_hook_count].thread = thread;
    ++g_target_thread_hook_count;
    PostThreadMessageW(thread_id, WM_NULL, 0, 0);
}

static void hook_target_process_threads(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry;
    wchar_t process_name[MAX_PATH];
    int shell_block = 0;

    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!get_process_base_name(pid, process_name, MAX_PATH)) {
        CloseHandle(snapshot);
        return;
    }
    shell_block = tray_rule_process_uses_shell_notify_block(process_name);
    int one_hook_process = shell_block &&
        (!tray_rule_process_uses_message_hook(process_name) ||
         _wcsicmp(process_name, L"GoogleDriveFS.exe") == 0);
    if (one_hook_process && process_has_target_hook(pid)) {
        CloseHandle(snapshot);
        return;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                hook_target_thread(pid, entry.th32ThreadID);
                if (one_hook_process && process_has_target_hook(pid)) {
                    break;
                }
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static void inject_dll_into_process(DWORD pid) {
    wchar_t directory[MAX_PATH];
    wchar_t dll_path[MAX_PATH];
    HANDLE process;
    SIZE_T size;
    LPVOID remote_path;
    HMODULE kernel32;
    FARPROC load_library;
    HANDLE thread;
    SIZE_T written = 0;

    exe_directory(directory, MAX_PATH);
    _snwprintf(dll_path, MAX_PATH - 1, L"%ls\\%ls", directory, DLL_NAME);
    dll_path[MAX_PATH - 1] = L'\0';

    process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
        FALSE, pid);
    if (!process) {
        return;
    }
    size = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    remote_path = VirtualAllocEx(process, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path ||
        !WriteProcessMemory(process, remote_path, dll_path, size, &written)) {
        if (remote_path) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        }
        CloseHandle(process);
        return;
    }
    kernel32 = GetModuleHandleW(L"kernel32.dll");
    load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : NULL;
    if (load_library) {
        thread = CreateRemoteThread(
            process, NULL, 0, (LPTHREAD_START_ROUTINE)load_library,
            remote_path, 0, NULL);
        if (thread) {
            WaitForSingleObject(thread, 5000);
            CloseHandle(thread);
        }
    }
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    CloseHandle(process);
}

static void hook_existing_target_processes(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry;
    DWORD current_session = 0;

    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session)) {
        CloseHandle(snapshot);
        return;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            DWORD candidate_session = 0;
            if (!tray_rule_process_is_target(entry.szExeFile) ||
                !ProcessIdToSessionId(entry.th32ProcessID, &candidate_session) ||
                candidate_session != current_session) {
                continue;
            }
            int newly_added = add_known_target_pid(entry.th32ProcessID);
            if (tray_rule_process_uses_message_hook(entry.szExeFile) ||
                tray_rule_process_uses_shell_notify_block(entry.szExeFile)) {
                hook_target_process_threads(entry.th32ProcessID);
            }
            if (_wcsicmp(entry.szExeFile, L"ChatGPT.exe") == 0) {
                start_chatgpt_cleanup();
            }
            (void)newly_added;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static void hook_target_window(HWND hwnd) {
    DWORD pid = 0;
    DWORD thread_id;
    wchar_t process_name[MAX_PATH];

    if (!hwnd) {
        return;
    }
    thread_id = GetWindowThreadProcessId(hwnd, &pid);
    if (find_target_thread_hook(pid, thread_id)) {
        return;
    }
    if (known_target_pid(pid) || pid_is_target_process(pid)) {
        int newly_added = add_known_target_pid(pid);
        if (get_process_base_name(pid, process_name, MAX_PATH)) {
            if (tray_rule_process_uses_message_hook(process_name) ||
                tray_rule_process_uses_shell_notify_block(process_name)) {
                hook_target_process_threads(pid);
            }
            if (_wcsicmp(process_name, L"ChatGPT.exe") == 0) {
                start_chatgpt_cleanup();
            }
        }
        if (g_google_drive_appeared_event &&
            get_process_base_name(pid, process_name, MAX_PATH) &&
            _wcsicmp(process_name, L"GoogleDriveFS.exe") == 0) {
            SetEvent(g_google_drive_appeared_event);
        }
        (void)newly_added;
    }
}

static void CALLBACK target_win_event_proc(
    HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object_id, LONG child_id,
    DWORD event_thread, DWORD event_time) {
    (void)hook;
    (void)event;
    (void)child_id;
    (void)event_thread;
    (void)event_time;
    if (object_id == OBJID_WINDOW && hwnd) {
        hook_target_window(hwnd);
    }
}

static int install_target_thread_hooks(void) {
    g_target_win_event_hook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, NULL, target_win_event_proc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hook_existing_target_processes();
    return 1;
}

static int command_recover(DWORD old_explorer_pid) {
    if (system_shutting_down()) {
        return 1;
    }
    for (int i = 0; i < 3000; ++i) {
        DWORD new_pid = 0;
        HANDLE process = open_shell_process(&new_pid);
        if (process) {
            CloseHandle(process);
            if (new_pid && new_pid != old_explorer_pid) {
                if (system_shutting_down()) {
                    return 1;
                }
                return launch_self_command(L"start") ? 0 : 1;
            }
        }
        if ((i % 25) == 0 && system_shutting_down()) {
            return 1;
        }
        Sleep(20);
    }
    return 1;
}

static HANDLE wait_for_new_shell_process(DWORD old_explorer_pid, DWORD *pid_out) {
    for (int i = 0; i < 3000; ++i) {
        DWORD new_pid = 0;
        HANDLE process = open_shell_process(&new_pid);
        if (process) {
            if (new_pid && new_pid != old_explorer_pid) {
                if (pid_out) {
                    *pid_out = new_pid;
                }
                return process;
            }
            CloseHandle(process);
        }
        if (system_shutting_down()) {
            return NULL;
        }
        Sleep(20);
    }
    return NULL;
}

static void write_dword_value(HKEY key, const wchar_t *name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static void write_string_value(HKEY key, const wchar_t *name, const wchar_t *value) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

static void ensure_ps_tray_factory_rule(const wchar_t *exe_name, const wchar_t *class_name, DWORD uid) {
    HKEY root = NULL;
    HKEY item = NULL;
    DWORD index = 0;
    DWORD max_number = 0;
    int found = 0;
    wchar_t subkey_name[128];
    const wchar_t *path = L"Software\\PS Soft Lab\\PS Tray Factory\\TrayIconManager\\AutoHideFiles";

    if (!exe_name || !class_name || !class_name[0]) {
        return;
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &root, NULL) != ERROR_SUCCESS) {
        return;
    }

    while (1) {
        DWORD name_len = (DWORD)(sizeof(subkey_name) / sizeof(subkey_name[0]));
        LONG rc = RegEnumKeyExW(root, index++, subkey_name, &name_len, NULL, NULL, NULL, NULL);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            continue;
        }

        DWORD number = (DWORD)_wtoi(subkey_name);
        if (number > max_number) {
            max_number = number;
        }

        if (RegOpenKeyExW(root, subkey_name, 0, KEY_READ | KEY_WRITE, &item) == ERROR_SUCCESS) {
            wchar_t existing_class[256];
            DWORD type = 0;
            DWORD size = sizeof(existing_class);
            existing_class[0] = L'\0';
            if (RegQueryValueExW(item, L"ClassName", NULL, &type, (BYTE *)existing_class, &size) == ERROR_SUCCESS &&
                type == REG_SZ &&
                wcscmp(existing_class, class_name) == 0) {
                write_string_value(item, L"ExeName", exe_name);
                write_dword_value(item, L"IconState", 2);
                write_dword_value(item, L"UID", uid);
                write_dword_value(item, L"IdByClassName", 1);
                write_dword_value(item, L"TrayMenuVisibility", 0);
                found = 1;
                RegCloseKey(item);
                break;
            }
            RegCloseKey(item);
        }
    }

    if (!found) {
        _snwprintf(subkey_name, (sizeof(subkey_name) / sizeof(subkey_name[0])) - 1, L"%lu", max_number + 1);
        subkey_name[(sizeof(subkey_name) / sizeof(subkey_name[0])) - 1] = L'\0';
        if (RegCreateKeyExW(root, subkey_name, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &item, NULL) == ERROR_SUCCESS) {
            write_string_value(item, L"ExeName", exe_name);
            write_dword_value(item, L"IconState", 2);
            write_dword_value(item, L"Order", max_number + 1);
            write_dword_value(item, L"UID", uid);
            write_dword_value(item, L"LastAccessTime", 46204);
            write_dword_value(item, L"IdByClassName", 1);
            write_string_value(item, L"ClassName", class_name);
            write_dword_value(item, L"AlwaysShowInMenu", 0);
            write_dword_value(item, L"HotKey", 0);
            write_dword_value(item, L"HotKeyIconAction", 0);
            write_string_value(item, L"AltIconFileName", L"");
            write_dword_value(item, L"AltIconIndex", 0);
            write_dword_value(item, L"UseAltIcon", 0);
            write_string_value(item, L"AltTip", L"");
            write_dword_value(item, L"UseAltTip", 0);
            write_dword_value(item, L"TrayMenuVisibility", 0);
            RegCloseKey(item);
        }
    }

    RegCloseKey(root);
}

static void cleanup_stale_google_drive_ps_rules(void) {
    HKEY root;
    int removed;

    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\PS Soft Lab\\PS Tray Factory\\TrayIconManager\\AutoHideFiles",
            0, KEY_READ | KEY_WRITE, &root) != ERROR_SUCCESS) {
        return;
    }

    do {
        removed = 0;
        for (DWORD index = 0; ; ++index) {
            wchar_t subkey[64];
            DWORD subkey_size = (DWORD)(sizeof(subkey) / sizeof(subkey[0]));
            FILETIME last_written;
            HKEY item;
            wchar_t exe_name[64] = { 0 };
            DWORD exe_size = sizeof(exe_name);
            DWORD type = 0;
            LONG enum_result = RegEnumKeyExW(
                root, index, subkey, &subkey_size, NULL, NULL, NULL, &last_written);
            int stale = 0;

            if (enum_result == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (enum_result != ERROR_SUCCESS) {
                continue;
            }
            if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &item) != ERROR_SUCCESS) {
                continue;
            }
            if (RegQueryValueExW(item, L"ExeName", NULL, &type,
                                 (LPBYTE)exe_name, &exe_size) == ERROR_SUCCESS &&
                type == REG_SZ &&
                _wcsicmp(exe_name, L"GOOGLEDRIVEFS.EXE") == 0) {
                stale = 1;
            }
            RegCloseKey(item);
            if (stale) {
                RegDeleteKeyW(root, subkey);
                removed = 1;
                break;
            }
        }
    } while (removed);

    RegCloseKey(root);
}

static BOOL CALLBACK scan_current_windows_proc(HWND hwnd, LPARAM lparam) {
    CurrentScanContext *ctx = (CurrentScanContext *)lparam;
    DWORD pid = 0;
    wchar_t exe_name[MAX_PATH];
    wchar_t class_name[256];
    wchar_t window_text[256];
    GUID guid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !get_process_base_name(pid, exe_name, MAX_PATH)) {
        return TRUE;
    }

    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return TRUE;
    }

    if (tray_rule_guid_for_process(exe_name, &guid)) {
        if (_wcsicmp(exe_name, L"ChatGPT.exe") != 0) {
            NOTIFYICONDATAW data;
            ZeroMemory(&data, sizeof(data));
            data.cbSize = sizeof(data);
            data.hWnd = hwnd;
            data.uFlags = NIF_GUID;
            data.guidItem = guid;
            if (Shell_NotifyIconW(NIM_DELETE, &data)) {
                ++ctx->hidden_guid_icons;
            }
        }
    }

    window_text[0] = L'\0';
    GetWindowTextW(hwnd, window_text, (int)(sizeof(window_text) / sizeof(window_text[0])));
    if (tray_rule_should_hide_window(exe_name, class_name, window_text)) {
        ShowWindow(hwnd, SW_HIDE);
        ++ctx->hidden_windows;
        return TRUE;
    }

    unsigned int uid = 0;
    if (tray_rule_uid_for_window(exe_name, class_name, &uid)) {
        NOTIFYICONDATAW data;
        if (tray_rule_should_write_ps_tray_factory(exe_name)) {
            ensure_ps_tray_factory_rule(exe_name, class_name, uid);
        }
        ZeroMemory(&data, sizeof(data));
        data.cbSize = sizeof(data);
        data.hWnd = hwnd;
        data.uID = uid;
        Shell_NotifyIconW(NIM_DELETE, &data);
        ++ctx->matches;
    }

    for (int i = 0; i < tray_block_uid_rule_count(); ++i) {
        NOTIFYICONDATAW data;
        if (!tray_rule_block_uid_for_window_class_at(exe_name, class_name, i, &uid)) {
            continue;
        }
        if (_wcsicmp(exe_name, L"GoogleDriveFS.exe") == 0 &&
            wcsstr(window_text, L"Google") != NULL) {
            continue;
        }
        ZeroMemory(&data, sizeof(data));
        data.cbSize = sizeof(data);
        data.hWnd = hwnd;
        data.uID = uid;
        Shell_NotifyIconW(NIM_DELETE, &data);
        ++ctx->matches;
    }

    return TRUE;
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

static BOOL CALLBACK chatgpt_uid_probe_proc(HWND hwnd, LPARAM lparam) {
    DWORD pid = 0;
    wchar_t exe_name[MAX_PATH];
    int *found = (int *)lparam;

    if (!found) {
        return FALSE;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !get_process_base_name(pid, exe_name, MAX_PATH) ||
        _wcsicmp(exe_name, L"ChatGPT.exe") != 0) {
        return TRUE;
    }

    for (unsigned int uid = 0; uid < CHATGPT_UID_SCAN_LIMIT; ++uid) {
        NOTIFYICONIDENTIFIER identifier;
        RECT rect;

        ZeroMemory(&identifier, sizeof(identifier));
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = hwnd;
        identifier.uID = uid;
        if (Shell_NotifyIconGetRect(&identifier, &rect) == S_OK) {
            *found = 1;
            return FALSE;
        }
    }
    return TRUE;
}

static int chatgpt_has_uid_icon(void) {
    int found = 0;
    EnumWindows(chatgpt_uid_probe_proc, (LPARAM)&found);
    return found;
}

static void cleanup_chatgpt_duplicate_icons(CurrentScanContext *ctx) {
    if (!chatgpt_has_uid_icon()) {
        return;
    }

    for (int i = 0; i < tray_guid_rule_count(); ++i) {
        const wchar_t *exe = tray_guid_rule_exe(i);
        const GUID *guid = tray_guid_rule_guid(i);
        NOTIFYICONDATAW data;

        if (_wcsicmp(exe, L"ChatGPT.exe") != 0 || !guid ||
            !guid_icon_exists(guid)) {
            continue;
        }
        ZeroMemory(&data, sizeof(data));
        data.cbSize = sizeof(data);
        data.uFlags = NIF_GUID;
        data.guidItem = *guid;
        if (Shell_NotifyIconW(NIM_DELETE, &data) && ctx) {
            ++ctx->hidden_guid_icons;
        }
    }
}

static void delete_rule_guid_icons(CurrentScanContext *ctx) {
    for (int i = 0; i < tray_guid_rule_count(); ++i) {
        const wchar_t *exe = tray_guid_rule_exe(i);
        const GUID *guid = tray_guid_rule_guid(i);
        NOTIFYICONDATAW data;

        if (_wcsicmp(exe, L"ChatGPT.exe") == 0) {
            continue;
        }
        if (!guid || !guid_icon_exists(guid)) {
            continue;
        }
        ZeroMemory(&data, sizeof(data));
        data.cbSize = sizeof(data);
        data.uFlags = NIF_GUID;
        data.guidItem = *guid;
        if (Shell_NotifyIconW(NIM_DELETE, &data)) {
            ++ctx->hidden_guid_icons;
        }
    }
}

static CurrentScanContext sync_current_windows(void) {
    CurrentScanContext ctx;
    ctx.matches = 0;
    ctx.hidden_windows = 0;
    ctx.hidden_guid_icons = 0;
    EnumWindows(scan_current_windows_proc, (LPARAM)&ctx);
    delete_rule_guid_icons(&ctx);
    cleanup_chatgpt_duplicate_icons(&ctx);
    return ctx;
}

static DWORD WINAPI startup_target_watcher(LPVOID param) {
    HANDLE stop_event = (HANDLE)param;
    ULONGLONG start = GetTickCount64();
    int cleaned = 0;

    while (GetTickCount64() - start < STARTUP_WATCH_MS) {
        if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) {
            break;
        }
        hook_existing_target_processes();
        if (event_is_signaled(GOOGLE_DRIVE_HOOK_READY_EVENT_NAME)) {
            if (!cleaned) {
                sync_current_windows();
                cleaned = 1;
            }
            break;
        }
        WaitForSingleObject(stop_event, STARTUP_WATCH_INTERVAL_MS);
    }
    return 0;
}

static BOOL CALLBACK google_drive_duplicate_cleanup_proc(HWND hwnd, LPARAM lparam) {
    DWORD pid = 0;
    wchar_t exe_name[MAX_PATH];
    wchar_t class_name[256];
    wchar_t window_text[256];
    NOTIFYICONIDENTIFIER identifier;
    RECT rect;

    (void)lparam;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !get_process_base_name(pid, exe_name, MAX_PATH) ||
        _wcsicmp(exe_name, L"GoogleDriveFS.exe") != 0) {
        return TRUE;
    }
    if (!GetClassNameW(hwnd, class_name,
                       (int)(sizeof(class_name) / sizeof(class_name[0]))) ||
        wcsncmp(class_name, L"ATL:", 4) != 0) {
        return TRUE;
    }
    GetWindowTextW(hwnd, window_text,
                   (int)(sizeof(window_text) / sizeof(window_text[0])));
    if (wcsstr(window_text, L"Google") != NULL) {
        return TRUE;
    }

    ZeroMemory(&identifier, sizeof(identifier));
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = hwnd;
    identifier.uID = 11376;
    if (Shell_NotifyIconGetRect(&identifier, &rect) == S_OK) {
        NOTIFYICONDATAW data;
        ZeroMemory(&data, sizeof(data));
        data.cbSize = sizeof(data);
        data.hWnd = hwnd;
        data.uID = 11376;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
    return TRUE;
}

static void cleanup_google_drive_duplicate_icons(void) {
    EnumWindows(google_drive_duplicate_cleanup_proc, 0);
}

static DWORD WINAPI explorer_restart_cleanup_thread(LPVOID param) {
    HANDLE stop_event = (HANDLE)param;
    ULONGLONG start = GetTickCount64();

    while (GetTickCount64() - start < EXPLORER_CLEANUP_MS) {
        if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) {
            break;
        }
        sync_current_windows();
        cleanup_google_drive_duplicate_icons();
        if (WaitForSingleObject(stop_event, EXPLORER_CLEANUP_INTERVAL_MS) ==
            WAIT_OBJECT_0) {
            break;
        }
    }
    return 0;
}

static void start_duplicate_cleanup_thread(void) {
    if (g_explorer_cleanup_thread) {
        WaitForSingleObject(g_explorer_cleanup_thread, 1000);
        CloseHandle(g_explorer_cleanup_thread);
        g_explorer_cleanup_thread = NULL;
    }
    g_explorer_cleanup_thread = CreateThread(
        NULL, 0, explorer_restart_cleanup_thread, g_service_stop_event, 0, NULL);
}

static DWORD WINAPI chatgpt_cleanup_thread(LPVOID param) {
    HANDLE appeared_event = (HANDLE)param;

    for (;;) {
        HANDLE handles[2];
        DWORD wait;

        if (!g_service_stop_event || !appeared_event) {
            break;
        }
        handles[0] = g_service_stop_event;
        handles[1] = appeared_event;
        wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait != WAIT_OBJECT_0 + 1) {
            Sleep(100);
            continue;
        }
        ResetEvent(appeared_event);

        ULONGLONG start = GetTickCount64();
        while (GetTickCount64() - start < CHATGPT_CLEANUP_MS) {
            if (WaitForSingleObject(g_service_stop_event, 0) == WAIT_OBJECT_0) {
                return 0;
            }
            cleanup_chatgpt_duplicate_icons(NULL);
            if (WaitForSingleObject(g_service_stop_event,
                                    CHATGPT_CLEANUP_INTERVAL_MS) ==
                WAIT_OBJECT_0) {
                return 0;
            }
        }
    }
    return 0;
}

static void start_chatgpt_cleanup(void) {
    if (g_chatgpt_appeared_event) {
        SetEvent(g_chatgpt_appeared_event);
    }
}

static DWORD WINAPI google_drive_watcher_thread(LPVOID param) {
    (void)param;
    for (;;) {
        HANDLE process_handles[MAX_GD_WATCH_PROCESSES];
        int process_count = 0;
        HANDLE snapshot;
        PROCESSENTRY32W entry;

        if (g_service_stop_event &&
            WaitForSingleObject(g_service_stop_event, 0) == WAIT_OBJECT_0) {
            break;
        }

        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            ZeroMemory(&entry, sizeof(entry));
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    HANDLE process;
                    if (_wcsicmp(entry.szExeFile, L"GoogleDriveFS.exe") != 0) {
                        continue;
                    }
                    process = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
                    if (process) {
                        if (process_count < MAX_GD_WATCH_PROCESSES) {
                            process_handles[process_count++] = process;
                        } else {
                            CloseHandle(process);
                        }
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }

        if (process_count == 0) {
            HANDLE wait_handles[2];
            DWORD wait;

            wait_handles[0] = g_service_stop_event;
            wait_handles[1] = g_google_drive_appeared_event;
            wait = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) {
                break;
            }
            if (wait == WAIT_OBJECT_0 + 1 && g_google_drive_appeared_event) {
                ResetEvent(g_google_drive_appeared_event);
            }
            continue;
        }

        {
            HANDLE wait_handles[MAX_GD_WATCH_PROCESSES + 2];
            DWORD wait;

            wait_handles[0] = g_service_stop_event;
            wait_handles[1] = g_google_drive_appeared_event;
            for (int i = 0; i < process_count; ++i) {
                wait_handles[i + 2] = process_handles[i];
            }
            wait = WaitForMultipleObjects(process_count + 2, wait_handles,
                                          FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) {
                for (int i = 0; i < process_count; ++i) {
                    CloseHandle(process_handles[i]);
                }
                break;
            }
            if (wait == WAIT_OBJECT_0 + 1 && g_google_drive_appeared_event) {
                ResetEvent(g_google_drive_appeared_event);
            }
            if (wait != WAIT_OBJECT_0 && wait != WAIT_OBJECT_0 + 1) {
                start_duplicate_cleanup_thread();
            }
            for (int i = 0; i < process_count; ++i) {
                CloseHandle(process_handles[i]);
            }
        }
    }
    return 0;
}

static void process_sync_line(wchar_t *line, int *changed) {
    wchar_t *exe = line;
    wchar_t *class_name = wcschr(line, L'|');
    wchar_t *uid_text;

    if (!class_name) {
        return;
    }
    *class_name++ = L'\0';
    uid_text = wcschr(class_name, L'|');
    if (!uid_text) {
        return;
    }
    *uid_text++ = L'\0';

    if (!tray_rule_matches(exe, class_name, (unsigned int)_wtoi(uid_text))) {
        return;
    }

    if (tray_rule_should_write_ps_tray_factory(exe)) {
        ensure_ps_tray_factory_rule(exe, class_name, (DWORD)_wtoi(uid_text));
    }
    *changed = 1;
}

static int process_sync_queue(const wchar_t *queue_path) {
    HANDLE file;
    DWORD size = 0;
    DWORD read = 0;
    wchar_t *buffer;
    wchar_t *line;
    int changed = 0;

    file = CreateFileW(queue_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 1024 * 1024) {
        CloseHandle(file);
        DeleteFileW(queue_path);
        return 0;
    }

    buffer = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + sizeof(wchar_t));
    if (!buffer) {
        CloseHandle(file);
        return 0;
    }

    if (ReadFile(file, buffer, size, &read, NULL) && read > 0) {
        buffer[read / sizeof(wchar_t)] = L'\0';
        line = buffer;
        while (line && *line) {
            wchar_t *next = wcschr(line, L'\n');
            if (next) {
                *next++ = L'\0';
            }
            if (*line) {
                process_sync_line(line, &changed);
            }
            line = next;
        }
    }

    HeapFree(GetProcessHeap(), 0, buffer);
    CloseHandle(file);
    DeleteFileW(queue_path);
    return changed;
}

static DWORD WINAPI sync_worker_thread(LPVOID param) {
    SyncWorkerContext *ctx = (SyncWorkerContext *)param;
    HANDLE handles[2];

    sync_current_windows();
    process_sync_queue(ctx->queue_path);

    handles[0] = ctx->stop_event;
    handles[1] = ctx->sync_event;
    while (1) {
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            process_sync_queue(ctx->queue_path);
        }
    }

    process_sync_queue(ctx->queue_path);
    return 0;
}

static int is_running(void) {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, MUTEX_NAME);
    if (!mutex) {
        return 0;
    }
    CloseHandle(mutex);
    return 1;
}

static int event_is_signaled(const wchar_t *name) {
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name);
    DWORD wait;
    if (!event) {
        return 0;
    }
    wait = WaitForSingleObject(event, 0);
    CloseHandle(event);
    return wait == WAIT_OBJECT_0;
}

static void print_rules(void) {
    out(L"Version: %ls\n", TOOL_VERSION);
    out(L"Notify icon rules:\n");
    for (int i = 0; i < tray_rule_count(); ++i) {
        out(L"  %d. exe=%ls class=%ls uid=%u\n",
            i + 1,
            tray_rule_exe(i),
            tray_rule_class_name(i),
            tray_rule_uid(i));
    }
    out(L"Window hide rules:\n");
    for (int i = 0; i < tray_window_rule_count(); ++i) {
        out(L"  %d. exe=%ls class=%ls text=%ls\n",
            i + 1,
            tray_window_rule_exe(i),
            tray_window_rule_class_name(i),
            tray_window_rule_text(i));
    }
    out(L"GUID icon rules:\n");
    for (int i = 0; i < tray_guid_rule_count(); ++i) {
        const GUID *guid = tray_guid_rule_guid(i);
        if (!guid) {
            continue;
        }
        out(L"  %d. exe=%ls guid={%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
            i + 1,
            tray_guid_rule_exe(i),
            guid->Data1,
            guid->Data2,
            guid->Data3,
            guid->Data4[0],
            guid->Data4[1],
            guid->Data4[2],
            guid->Data4[3],
            guid->Data4[4],
            guid->Data4[5],
            guid->Data4[6],
            guid->Data4[7]);
    }
    out(L"Shell notify block rules:\n");
    for (int i = 0; i < tray_block_uid_rule_count(); ++i) {
        out(L"  %d. exe=%ls class=%ls uid=%u\n",
            i + 1,
            tray_block_uid_rule_exe(i),
            tray_block_uid_rule_class_name(i),
            tray_block_uid_rule_uid(i));
    }
}

static int command_status(void) {
    CurrentScanContext ctx;

    print_rules();
    out(L"Background: %ls\n", is_running() ? L"running" : L"stopped");
    out(L"Explorer restart watcher: %ls\n",
        event_is_signaled(EXPLORER_WATCH_READY_EVENT_NAME) ? L"ready" : L"not_ready");
    out(L"Create-stage hook: explorer=%ls, google_drive=%ls\n",
        event_is_signaled(EXPLORER_HOOK_READY_EVENT_NAME) ? L"ready" : L"not_ready",
        event_is_signaled(GOOGLE_DRIVE_HOOK_READY_EVENT_NAME) ? L"ready" : L"not_ready");
    out(L"PS Tray Factory restore guard: thread_hook=%ls, iat=%ls\n",
        event_is_signaled(PSTF_THREAD_HOOK_EVENT_NAME) ? L"ready" : L"not_ready",
        event_is_signaled(PSTF_RESTORE_READY_EVENT_NAME) ? L"ready" : L"not_ready");
    ctx = sync_current_windows();
    out(L"Current matched notify icons: %d\n", ctx.matches);
    out(L"Current hidden windows: %d\n", ctx.hidden_windows);
    out(L"Current hidden GUID icons: %d\n", ctx.hidden_guid_icons);
    return 0;
}

static int command_apply(void) {
    CurrentScanContext ctx = sync_current_windows();
    out(L"%ls apply: notify_icons=%d, hidden_windows=%d, guid_icons=%d, pstf_readtray=not_sent\n",
        TOOL_VERSION,
        ctx.matches,
        ctx.hidden_windows,
        ctx.hidden_guid_icons);
    return 0;
}

static int command_stop(void) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, STOP_EVENT_NAME);
    if (!event) {
        out(L"Background: already stopped\n");
    } else {
        write_watchdog_stop_marker();
        SetEvent(event);
        CloseHandle(event);
        out(L"Stop signal sent\n");
    }

    for (int i = 0; i < 30 && is_running(); ++i) {
        Sleep(100);
    }
    return 0;
}

static int command_start(void) {
    int restart_after_shell = 0;
    DWORD explorer_pid = 0;

    if (system_shutting_down()) {
        return 1;
    }

    HANDLE mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (!mutex) {
        err(L"Cannot create mutex: %lu\n", GetLastError());
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        out(L"Background: already running\n");
        command_apply();
        CloseHandle(mutex);
        return 0;
    }

    HANDLE stop_event = CreateEventW(NULL, TRUE, FALSE, STOP_EVENT_NAME);
    HANDLE sync_event = CreateEventW(NULL, FALSE, FALSE, SYNC_EVENT_NAME);
    if (!stop_event || !sync_event) {
        err(L"Cannot create events: %lu\n", GetLastError());
        if (sync_event) CloseHandle(sync_event);
        if (stop_event) CloseHandle(stop_event);
        CloseHandle(mutex);
        return 1;
    }
    ResetEvent(stop_event);
    g_shutdown_stop_event = stop_event;
    g_service_stop_event = stop_event;
    g_google_drive_appeared_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_chatgpt_appeared_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_chatgpt_appeared_event) {
        g_chatgpt_cleanup_thread = CreateThread(
            NULL, 0, chatgpt_cleanup_thread, g_chatgpt_appeared_event, 0, NULL);
    }
    cleanup_stale_google_drive_ps_rules();

    wchar_t dir[MAX_PATH];
    wchar_t dll_path[MAX_PATH];
    SyncWorkerContext sync_context;
    ZeroMemory(&sync_context, sizeof(sync_context));
    sync_context.stop_event = stop_event;
    sync_context.sync_event = sync_event;
    exe_directory(dir, MAX_PATH);
    _snwprintf(sync_context.queue_path, MAX_PATH - 1, L"%ls\\ps-tray-sync-queue.log", dir);
    sync_context.queue_path[MAX_PATH - 1] = L'\0';
    DeleteFileW(sync_context.queue_path);

    HANDLE sync_thread = CreateThread(NULL, 0, sync_worker_thread, &sync_context, 0, NULL);
    if (!sync_thread) {
        err(L"Cannot create worker thread: %lu\n", GetLastError());
        CloseHandle(sync_event);
        g_shutdown_stop_event = NULL;
        CloseHandle(stop_event);
        CloseHandle(mutex);
        return 1;
    }

    _snwprintf(dll_path, MAX_PATH - 1, L"%ls\\%ls", dir, DLL_NAME);
    dll_path[MAX_PATH - 1] = L'\0';

    HMODULE dll = LoadLibraryW(dll_path);
    if (!dll) {
        err(L"Cannot load %ls: %lu\n", dll_path, GetLastError());
        SetEvent(stop_event);
        WaitForSingleObject(sync_thread, 3000);
        CloseHandle(sync_thread);
        CloseHandle(sync_event);
        g_shutdown_stop_event = NULL;
        CloseHandle(stop_event);
        CloseHandle(mutex);
        return 1;
    }

    HOOKPROC call_proc = (HOOKPROC)GetProcAddress(dll, "CallWndHookProc");
    HOOKPROC msg_proc = (HOOKPROC)GetProcAddress(dll, "GetMsgHookProc");
    if (!call_proc || !msg_proc) {
        err(L"DLL exports missing\n");
        FreeLibrary(dll);
        SetEvent(stop_event);
        WaitForSingleObject(sync_thread, 3000);
        CloseHandle(sync_thread);
        CloseHandle(sync_event);
        g_shutdown_stop_event = NULL;
        CloseHandle(stop_event);
        CloseHandle(mutex);
        return 1;
    }

    g_hook_dll = dll;
    g_call_proc = call_proc;
    g_msg_proc = msg_proc;
    install_target_thread_hooks();
    g_startup_watcher_thread = CreateThread(
        NULL, 0, startup_target_watcher, stop_event, 0, NULL);
    g_google_drive_watcher_thread = CreateThread(
        NULL, 0, google_drive_watcher_thread, NULL, 0, NULL);

    HANDLE explorer_process = open_shell_process(&explorer_pid);
    HANDLE explorer_watch_ready = CreateEventW(
        NULL, TRUE, explorer_process != NULL, EXPLORER_WATCH_READY_EVENT_NAME);
    if (!explorer_watch_ready) {
        err(L"Cannot create Explorer watcher status event: %lu\n", GetLastError());
    }
    out(L"%ls started\n", TOOL_VERSION);
    for (;;) {
        DWORD wait;
        MSG msg;
        HANDLE handles[2];
        DWORD handle_count = 1;

        handles[0] = stop_event;
        if (explorer_process) {
            handles[1] = explorer_process;
            handle_count = 2;
        }

        wait = MsgWaitForMultipleObjects(handle_count, handles, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (explorer_process && wait == WAIT_OBJECT_0 + 1) {
            DWORD old_explorer_pid = explorer_pid;
            CloseHandle(explorer_process);
            explorer_process = NULL;
            explorer_pid = 0;
            if (explorer_watch_ready) {
                ResetEvent(explorer_watch_ready);
            }

            if (system_shutting_down()) {
                CloseHandle(explorer_process);
                explorer_process = NULL;
                explorer_pid = 0;
                break;
            }
            explorer_process = wait_for_new_shell_process(old_explorer_pid, &explorer_pid);
            if (!explorer_process) {
                restart_after_shell = system_shutting_down() ? 0 : 1;
                explorer_pid = old_explorer_pid;
                break;
            }

            if (explorer_watch_ready) {
                SetEvent(explorer_watch_ready);
            }
            hook_target_process_threads(explorer_pid);
            hook_existing_target_processes();
            start_duplicate_cleanup_thread();
            continue;
        }
        if (wait == WAIT_OBJECT_0 + handle_count) {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            continue;
        }
        err(L"Hook wait failed: %lu\n", GetLastError());
        Sleep(100);
        continue;
    }

    SetEvent(stop_event);
    if (g_target_win_event_hook) {
        UnhookWinEvent(g_target_win_event_hook);
        g_target_win_event_hook = NULL;
    }
    remove_all_target_hooks();
    if (g_startup_watcher_thread) {
        WaitForSingleObject(g_startup_watcher_thread, 3000);
        CloseHandle(g_startup_watcher_thread);
        g_startup_watcher_thread = NULL;
    }
    if (g_explorer_cleanup_thread) {
        WaitForSingleObject(g_explorer_cleanup_thread, 3000);
        CloseHandle(g_explorer_cleanup_thread);
        g_explorer_cleanup_thread = NULL;
    }
    if (g_chatgpt_cleanup_thread) {
        WaitForSingleObject(g_chatgpt_cleanup_thread, 3000);
        CloseHandle(g_chatgpt_cleanup_thread);
        g_chatgpt_cleanup_thread = NULL;
    }
    if (g_chatgpt_appeared_event) {
        CloseHandle(g_chatgpt_appeared_event);
        g_chatgpt_appeared_event = NULL;
    }
    if (g_google_drive_watcher_thread) {
        WaitForSingleObject(g_google_drive_watcher_thread, 3000);
        CloseHandle(g_google_drive_watcher_thread);
        g_google_drive_watcher_thread = NULL;
    }
    if (g_google_drive_appeared_event) {
        CloseHandle(g_google_drive_appeared_event);
        g_google_drive_appeared_event = NULL;
    }
    g_hook_dll = NULL;
    g_call_proc = NULL;
    g_msg_proc = NULL;
    FreeLibrary(dll);
    if (explorer_watch_ready) CloseHandle(explorer_watch_ready);
    if (explorer_process) CloseHandle(explorer_process);
    WaitForSingleObject(sync_thread, 3000);
    CloseHandle(sync_thread);
    CloseHandle(sync_event);
    g_shutdown_stop_event = NULL;
    g_service_stop_event = NULL;
    CloseHandle(stop_event);
    CloseHandle(mutex);
    if (restart_after_shell) {
        wchar_t arguments[64];
        _snwprintf(arguments, (sizeof(arguments) / sizeof(arguments[0])) - 1,
                   L"recover %lu", explorer_pid);
        arguments[(sizeof(arguments) / sizeof(arguments[0])) - 1] = L'\0';
        launch_self_command(arguments);
    }
    out(L"%ls stopped\n", TOOL_VERSION);
    return 0;
}

static void usage(void) {
    print_rules();
    out(L"\nUsage:\n");
    out(L"  Lyrics Tray Icon Fix v0.107.exe start   start PS Tray Factory route\n");
    out(L"  Lyrics Tray Icon Fix v0.107.exe stop    stop background hooks\n");
    out(L"  Lyrics Tray Icon Fix v0.107.exe apply   sync current rules once\n");
    out(L"  Lyrics Tray Icon Fix v0.107.exe status  show status\n");
    out(L"  Lyrics Tray Icon Fix v0.107.exe recover  internal bounded Shell recovery\n");
}

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    if (argc < 2) {
        usage();
        return 0;
    }

    if (_wcsicmp(argv[1], L"start") == 0) {
        return command_start();
    }
    if (_wcsicmp(argv[1], L"stop") == 0) {
        return command_stop();
    }
    if (_wcsicmp(argv[1], L"apply") == 0) {
        return command_apply();
    }
    if (_wcsicmp(argv[1], L"status") == 0) {
        return command_status();
    }
    if (_wcsicmp(argv[1], L"recover") == 0 && argc >= 3) {
        return command_recover((DWORD)wcstoul(argv[2], NULL, 10));
    }

    usage();
    return 1;
}
