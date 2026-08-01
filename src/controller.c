#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#include "rules.h"

#define TOOL_VERSION L"v0.23-target-google-drive-livedot"
#define MUTEX_NAME L"Local\\LyricsTrayIconFixMutex"
#define STOP_EVENT_NAME L"Local\\LyricsTrayIconFixStop"
#define SYNC_EVENT_NAME L"Local\\LyricsTrayIconFixSync"
#define DLL_NAME L"Lyrics Tray Icon Fix Hook v0.23.dll"

typedef struct SyncWorkerContext {
    HANDLE stop_event;
    HANDLE sync_event;
    wchar_t queue_path[MAX_PATH];
} SyncWorkerContext;

typedef struct CurrentScanContext {
    int matches;
    int hidden_windows;
} CurrentScanContext;

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

static BOOL CALLBACK scan_current_windows_proc(HWND hwnd, LPARAM lparam) {
    CurrentScanContext *ctx = (CurrentScanContext *)lparam;
    DWORD pid = 0;
    wchar_t exe_name[MAX_PATH];
    wchar_t class_name[256];
    wchar_t window_text[256];

    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !get_process_base_name(pid, exe_name, MAX_PATH)) {
        return TRUE;
    }

    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) {
        return TRUE;
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
        ensure_ps_tray_factory_rule(exe_name, class_name, uid);
        ZeroMemory(&data, sizeof(data));
        data.cbSize = sizeof(data);
        data.hWnd = hwnd;
        data.uID = uid;
        Shell_NotifyIconW(NIM_DELETE, &data);
        ++ctx->matches;
    }

    return TRUE;
}

static CurrentScanContext sync_current_windows(void) {
    CurrentScanContext ctx;
    ctx.matches = 0;
    ctx.hidden_windows = 0;
    EnumWindows(scan_current_windows_proc, (LPARAM)&ctx);
    return ctx;
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

    ensure_ps_tray_factory_rule(exe, class_name, (DWORD)_wtoi(uid_text));
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
}

static int command_status(void) {
    CurrentScanContext ctx;

    print_rules();
    out(L"Background: %ls\n", is_running() ? L"running" : L"stopped");
    ctx = sync_current_windows();
    out(L"Current matched notify icons: %d\n", ctx.matches);
    out(L"Current hidden windows: %d\n", ctx.hidden_windows);
    return 0;
}

static int command_apply(void) {
    CurrentScanContext ctx = sync_current_windows();
    out(L"%ls apply: notify_icons=%d, hidden_windows=%d, pstf_readtray=not_sent\n",
        TOOL_VERSION,
        ctx.matches,
        ctx.hidden_windows);
    return 0;
}

static int command_stop(void) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, STOP_EVENT_NAME);
    if (!event) {
        out(L"Background: already stopped\n");
    } else {
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
        CloseHandle(stop_event);
        CloseHandle(mutex);
        return 1;
    }

    HHOOK call_hook = SetWindowsHookExW(WH_CALLWNDPROC, call_proc, dll, 0);
    HHOOK msg_hook = SetWindowsHookExW(WH_GETMESSAGE, msg_proc, dll, 0);
    if (!call_hook || !msg_hook) {
        err(L"Cannot install message hooks: %lu\n", GetLastError());
        if (call_hook) UnhookWindowsHookEx(call_hook);
        if (msg_hook) UnhookWindowsHookEx(msg_hook);
        FreeLibrary(dll);
        SetEvent(stop_event);
        WaitForSingleObject(sync_thread, 3000);
        CloseHandle(sync_thread);
        CloseHandle(sync_event);
        CloseHandle(stop_event);
        CloseHandle(mutex);
        return 1;
    }

    out(L"%ls started\n", TOOL_VERSION);
    WaitForSingleObject(stop_event, INFINITE);

    UnhookWindowsHookEx(call_hook);
    UnhookWindowsHookEx(msg_hook);
    FreeLibrary(dll);
    WaitForSingleObject(sync_thread, 3000);
    CloseHandle(sync_thread);
    CloseHandle(sync_event);
    CloseHandle(stop_event);
    CloseHandle(mutex);
    out(L"%ls stopped\n", TOOL_VERSION);
    return 0;
}

static void usage(void) {
    print_rules();
    out(L"\nUsage:\n");
    out(L"  Lyrics Tray Icon Fix.exe start   start PS Tray Factory route\n");
    out(L"  Lyrics Tray Icon Fix.exe stop    stop background hooks\n");
    out(L"  Lyrics Tray Icon Fix.exe apply   sync current rules once\n");
    out(L"  Lyrics Tray Icon Fix.exe status  show status\n");
}

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
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

    usage();
    return 1;
}
