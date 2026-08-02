#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

#define MAX_THREAD_HOOKS 32
#define STOP_EVENT_NAME L"Local\\LyricsTrayIconFixStop"
#define DLL_NAME L"Lyrics Tray Icon Fix PS Restore Hook v0.52.dll"
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

static HANDLE find_pstf_process(DWORD *pid_out) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry;

    if (pid_out) {
        *pid_out = 0;
    }
    if (snapshot == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            HANDLE process;
            if (_wcsicmp(entry.szExeFile, L"PSTrayFactory.exe") == 0) {
                process = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
                if (process) {
                    if (pid_out) {
                        *pid_out = entry.th32ProcessID;
                    }
                    CloseHandle(snapshot);
                    return process;
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return NULL;
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
    HANDLE pstf_process = NULL;
    DWORD pstf_pid = 0;
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

    pstf_process = find_pstf_process(&pstf_pid);
    if (pstf_process) {
        hook_pstf_threads(pstf_pid);
    }

    for (;;) {
        DWORD wait;
        HANDLE handles[2];
        DWORD handle_count = 1;

        handles[0] = stop_event;
        if (pstf_process) {
            handles[1] = pstf_process;
            handle_count = 2;
        }
        wait = MsgWaitForMultipleObjects(handle_count, handles, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (pstf_process && wait == WAIT_OBJECT_0 + 1) {
            DWORD old_pid = pstf_pid;
            CloseHandle(pstf_process);
            pstf_process = NULL;
            pstf_pid = 0;
            for (int i = 0; i < 3000; ++i) {
                DWORD new_pid = 0;
                HANDLE process = find_pstf_process(&new_pid);
                if (process) {
                    if (new_pid && new_pid != old_pid) {
                        pstf_process = process;
                        pstf_pid = new_pid;
                        hook_pstf_threads(new_pid);
                        break;
                    }
                    CloseHandle(process);
                }
                Sleep(20);
            }
            continue;
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

    if (pstf_process) {
        CloseHandle(pstf_process);
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
