#include <stdio.h>
#include <wchar.h>

#include "../src/rules.h"

static int expect_match(const wchar_t *exe, const wchar_t *class_name, unsigned int uid, int expected, const char *name) {
    int actual = tray_rule_matches(exe, class_name, uid);
    if (actual != expected) {
        printf("FAIL %s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_target(const wchar_t *exe, int expected, const char *name) {
    int actual = tray_rule_process_is_target(exe);
    if (actual != expected) {
        printf("FAIL %s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;

    failures += expect_target(L"Lyricify Lite.exe", 1, "lyricify target process");
    failures += expect_target(L"BetterLyrics.WinUI3.exe", 1, "betterlyrics target process");
    failures += expect_target(L"explorer.exe", 1, "explorer target process");
    failures += expect_target(L"Other.exe", 0, "other process is ignored");

    failures += expect_match(L"Lyricify Lite.exe", L"H.NotifyIcon_e87a2320-3a24-461c-99f6-c38bf4eb8d8b", 0, 1, "lyricify wrong icon");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"H.NotifyIcon_66a3fb03-7068-42a1-b1c9-861baae756c0", 0, 1, "betterlyrics wrong icon");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"H.NotifyIcon_31acf094-878f-42c9-b8e6-c1d89420a6e4", 0, 1, "volatile hnotifyicon class still matches");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"H.NotifyIcon_66a3fb03-7068-42a1-b1c9-861baae756c0", 1, 0, "uid prevents hiding normal sibling");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"Windows.UI.Core.CoreWindow", 0, 0, "class prevents hiding normal sibling");
    failures += expect_match(L"Other.exe", L"H.NotifyIcon_66a3fb03-7068-42a1-b1c9-861baae756c0", 0, 0, "exe prevents cross-app hiding");
    failures += expect_match(L"explorer.exe", L"ATL:00007FFE7B28A050", 100, 1, "audio service tray icon");
    failures += expect_match(L"explorer.exe", L"ATL:00007FFF4A42A050", 100, 1, "usb speaker tray icon");
    failures += expect_match(L"explorer.exe", L"ATL:00007FFE7B28A050", 0, 0, "audio service uid must be exact");
    failures += expect_match(L"explorer.exe", L"Shell_TrayWnd", 100, 0, "audio service class must be atl");

    if (failures) {
        return 1;
    }

    printf("rules_test: ok\n");
    return 0;
}
