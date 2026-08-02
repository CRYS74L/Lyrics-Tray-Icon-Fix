#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

#define MAX_THREAD_HOOKS 32
#define STOP_EVENT_NAME L"Local\\LyricsTrayIconFixStop"
#define DLL_NAME L"Lyrics Tray Icon Fix PS Restore Hook v0.37-cold-recovery.dll"
#define HOOK_INSTALLED_EVENT_NAME L"Local\\LyricsTrayIconFixPstfThreadHookInstalled"

typedef struct ThreadHook {
    DWORD pid;
    DWORD thread_id;
    HHOOK hook;
    HANDLE process;
    HANDLE thread;
} ThreadHook;

static HMODULE g_dll = NULL;
static HOOKPROC g_hook_proc = NULL;
static HWINEVENTHOOK g_win_event_hook = NULL;
static HANDLE g_hook_installed_event = NULL;
static ThreadHook g_thread_hooks[MAX_THREAD_HOOKS];
static int g_thread_hook_count = 0;

static const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *name = path;
    for (const wchar_t *p = path; p && *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            name = p + 1;
        }
    }
    return name;
}

static int is_pstf_process(DWORD pid) {
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    int match = 0;
    if (!process) {
        return 0;
    }
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        match = _wcsicmp(base_name(path), L"PSTrayFactory.exe") == 0;
    }
    CloseHandle(process);
    return match;
}

static void remove_hook_at(int index) {
    if (index < 0 || index >= g_thread_hook_count) {
        return;
    }
    if (g_thread_hooks[index].hook) {
        UnhookWindowsHookEx(g_thread_hooks[index].hook);
    }
    if (g_thread_hooks[index].thread) {
        CloseHandle(g_thread_hooks[index].thread);
    }
    if (g_thread_hooks[index].process) {
        CloseHandle(g_thread_hooks[index].process);
    }
    --g_thread_hook_count;
    if (index != g_thread_hook_count) {
        g_thread_hooks[index] = g_thread_hooks[g_thread_hook_count];
    }
    ZeroMemory(&g_thread_hooks[g_thread_hook_count], sizeof(ThreadHook));
    if (g_thread_hook_count == 0 && g_hook_installed_event) {
        ResetEvent(g_hook_installed_event);
    }
}

static void remove_finished_hooks(void) {
    for (int i = g_thread_hook_count - 1; i >= 0; --i) {
        DWORD process_wait = g_thread_hooks[i].process
            ? WaitForSingleObject(g_thread_hooks[i].process, 0)
            : WAIT_OBJECT_0;
        DWORD thread_wait = g_thread_hooks[i].thread
            ? WaitForSingleObject(g_thread_hooks[i].thread, 0)
            : WAIT_OBJECT_0;
        if (process_wait == WAIT_OBJECT_0 || thread_wait == WAIT_OBJECT_0) {
            remove_hook_at(i);
        }
    }
}

static void hook_thread(DWORD pid, DWORD thread_id) {
    if (!pid || !thread_id || !g_hook_proc || !g_dll || !is_pstf_process(pid)) {
        return;
    }
    remove_finished_hooks();
    for (int i = 0; i < g_thread_hook_count; ++i) {
        if (g_thread_hooks[i].pid == pid &&
            g_thread_hooks[i].thread_id == thread_id) {
            return;
        }
    }
    if (g_thread_hook_count >= MAX_THREAD_HOOKS) {
        return;
    }

    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, g_hook_proc, g_dll, thread_id);
    if (hook) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        HANDLE thread = OpenThread(SYNCHRONIZE, FALSE, thread_id);
        if (!process || !thread || !PostThreadMessageW(thread_id, WM_NULL, 0, 0)) {
            if (thread) CloseHandle(thread);
            if (process) CloseHandle(process);
            UnhookWindowsHookEx(hook);
            return;
        }
        g_thread_hooks[g_thread_hook_count].pid = pid;
        g_thread_hooks[g_thread_hook_count].thread_id = thread_id;
        g_thread_hooks[g_thread_hook_count].hook = hook;
        g_thread_hooks[g_thread_hook_count].process = process;
        g_thread_hooks[g_thread_hook_count].thread = thread;
        ++g_thread_hook_count;
        if (!g_hook_installed_event) {
            g_hook_installed_event = CreateEventW(
                NULL, TRUE, TRUE, HOOK_INSTALLED_EVENT_NAME);
        } else {
            SetEvent(g_hook_installed_event);
        }
    }
}

static void hook_window(HWND hwnd) {
    DWORD pid = 0;
    DWORD thread_id;

    if (!hwnd) {
        return;
    }
    thread_id = GetWindowThreadProcessId(hwnd, &pid);
    if (!is_pstf_process(pid)) {
        return;
    }
    hook_thread(pid, thread_id);
    PostMessageW(hwnd, WM_NULL, 0, 0);
}

static void hook_pstf_threads(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry;

    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                hook_thread(pid, entry.th32ThreadID);
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static void hook_existing_pstf_processes(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry;

    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"PSTrayFactory.exe") == 0) {
                hook_pstf_threads(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static BOOL CALLBACK enum_window_proc(HWND hwnd, LPARAM lparam) {
    (void)lparam;
    hook_window(hwnd);
    return TRUE;
}

static void CALLBACK win_event_proc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                    LONG object_id, LONG child_id,
                                    DWORD event_thread, DWORD event_time) {
    (void)hook;
    (void)event;
    (void)child_id;
    (void)event_thread;
    (void)event_time;
    if (object_id == OBJID_WINDOW && hwnd) {
        hook_window(hwnd);
    }
}

static void exe_directory(wchar_t *buffer, DWORD count) {
    DWORD length = GetModuleFileNameW(NULL, buffer, count);
    if (!length || length >= count) {
        buffer[0] = L'\0';
        return;
    }
    for (DWORD i = length; i > 0; --i) {
        if (buffer[i - 1] == L'\\' || buffer[i - 1] == L'/') {
            buffer[i] = L'\0';
            return;
        }
    }
    buffer[0] = L'\0';
}

int wmain(void) {
    HANDLE stop_event = OpenEventW(SYNCHRONIZE, FALSE, STOP_EVENT_NAME);
    wchar_t directory[MAX_PATH];
    wchar_t dll_path[MAX_PATH];

    if (!stop_event) {
        return 2;
    }
    exe_directory(directory, MAX_PATH);
    _snwprintf(dll_path, MAX_PATH - 1, L"%ls%ls", directory, DLL_NAME);
    dll_path[MAX_PATH - 1] = L'\0';
    g_dll = LoadLibraryW(dll_path);
    if (!g_dll) {
        CloseHandle(stop_event);
        return 3;
    }
    g_hook_proc = (HOOKPROC)GetProcAddress(g_dll, "PstfCallWndHookProc");
    if (!g_hook_proc) {
        g_hook_proc = (HOOKPROC)GetProcAddress(g_dll, "PstfCallWndHookProc@12");
    }
    if (!g_hook_proc) {
        g_hook_proc = (HOOKPROC)GetProcAddress(g_dll, "_PstfCallWndHookProc@12");
    }
    if (!g_hook_proc) {
        FreeLibrary(g_dll);
        CloseHandle(stop_event);
        return 4;
    }

    g_win_event_hook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL, win_event_proc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hook_existing_pstf_processes();
    EnumWindows(enum_window_proc, 0);

    for (;;) {
        DWORD wait = MsgWaitForMultipleObjects(1, &stop_event, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            MSG message;
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }
        break;
    }

    if (g_win_event_hook) {
        UnhookWinEvent(g_win_event_hook);
    }
    while (g_thread_hook_count > 0) {
        remove_hook_at(g_thread_hook_count - 1);
    }
    if (g_hook_installed_event) {
        CloseHandle(g_hook_installed_event);
    }
    FreeLibrary(g_dll);
    CloseHandle(stop_event);
    return 0;
}
