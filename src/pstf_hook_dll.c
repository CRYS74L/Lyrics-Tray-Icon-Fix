#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string.h>

typedef BOOL (WINAPI *ShellNotifyIconAFn)(DWORD, PNOTIFYICONDATAA);

static ShellNotifyIconAFn g_next_shell_notify_icon_a = NULL;
static void **g_patched_slot = NULL;
static HANDLE g_ready_event = NULL;
static int g_process_is_pstf = 0;

static const char *base_name(const char *path) {
    const char *name = path;
    for (const char *p = path; p && *p; ++p) {
        if (*p == '\\' || *p == '/') {
            name = p + 1;
        }
    }
    return name;
}

static int is_explorer_atl_window(HWND hwnd) {
    char class_name[256];
    char process_path[MAX_PATH];
    DWORD pid = 0;
    DWORD size = MAX_PATH;
    HANDLE process;
    int match = 0;

    if (!hwnd || !GetClassNameA(hwnd, class_name, (int)sizeof(class_name)) ||
        strncmp(class_name, "ATL:", 4) != 0) {
        return 0;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) {
        return 0;
    }
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return 0;
    }
    if (QueryFullProcessImageNameA(process, 0, process_path, &size)) {
        match = _stricmp(base_name(process_path), "explorer.exe") == 0;
    }
    CloseHandle(process);
    return match;
}

static BOOL WINAPI hooked_shell_notify_icon_a(DWORD message, PNOTIFYICONDATAA data) {
    if (data && (data->uID == 100 || data->uID == 101) &&
        (message == NIM_ADD || message == NIM_MODIFY || message == NIM_SETVERSION) &&
        is_explorer_atl_window(data->hWnd)) {
        return TRUE;
    }
    return g_next_shell_notify_icon_a ? g_next_shell_notify_icon_a(message, data) : FALSE;
}

static void install_iat_hook(void) {
    HMODULE module = GetModuleHandleW(NULL);
    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
    FARPROC original;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY directory;
    IMAGE_IMPORT_DESCRIPTOR *descriptor;

    if (g_patched_slot || !module) {
        return;
    }
    if (!shell32) {
        shell32 = LoadLibraryW(L"shell32.dll");
    }
    if (!shell32) {
        return;
    }
    original = GetProcAddress(shell32, "Shell_NotifyIconA");
    if (!original) {
        return;
    }

    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) {
        return;
    }

    descriptor = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)module + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)((BYTE *)module + descriptor->FirstThunk);
        IMAGE_THUNK_DATA *lookup = descriptor->OriginalFirstThunk
            ? (IMAGE_THUNK_DATA *)((BYTE *)module + descriptor->OriginalFirstThunk)
            : NULL;
        for (; thunk && thunk->u1.Function; ++thunk) {
            int match = thunk->u1.Function == (ULONG_PTR)original;
            if (lookup) {
                if (!IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) {
                    IMAGE_IMPORT_BY_NAME *name =
                        (IMAGE_IMPORT_BY_NAME *)((BYTE *)module + lookup->u1.AddressOfData);
                    match = strcmp((const char *)name->Name, "Shell_NotifyIconA") == 0;
                } else {
                    match = 0;
                }
                ++lookup;
            }
            if (match) {
                void **slot = (void **)&thunk->u1.Function;
                DWORD old_protect = 0;
                if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
                    return;
                }
                g_next_shell_notify_icon_a = (ShellNotifyIconAFn)*slot;
                *slot = (void *)hooked_shell_notify_icon_a;
                VirtualProtect(slot, sizeof(void *), old_protect, &old_protect);
                FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
                g_patched_slot = slot;
                g_ready_event = CreateEventW(
                    NULL, TRUE, TRUE, L"Local\\LyricsTrayIconFixPstfRestoreReady");
                return;
            }
        }
    }
}

static void restore_iat_hook(void) {
    DWORD old_protect = 0;
    if (!g_patched_slot || !g_next_shell_notify_icon_a ||
        *g_patched_slot != (void *)hooked_shell_notify_icon_a) {
        return;
    }
    if (VirtualProtect(g_patched_slot, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        *g_patched_slot = (void *)g_next_shell_notify_icon_a;
        VirtualProtect(g_patched_slot, sizeof(void *), old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), g_patched_slot, sizeof(void *));
    }
    g_patched_slot = NULL;
}

__declspec(dllexport) LRESULT CALLBACK PstfCallWndHookProc(
    int code, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    (void)lparam;
    if (code >= 0 && g_process_is_pstf) {
        install_iat_hook();
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        DisableThreadLibraryCalls(instance);
        if (GetModuleFileNameA(NULL, path, MAX_PATH) &&
            _stricmp(base_name(path), "PSTrayFactory.exe") == 0) {
            g_process_is_pstf = 1;
        }
    } else if (reason == DLL_PROCESS_DETACH && reserved == NULL) {
        restore_iat_hook();
        if (g_ready_event) {
            CloseHandle(g_ready_event);
            g_ready_event = NULL;
        }
    }
    return TRUE;
}
