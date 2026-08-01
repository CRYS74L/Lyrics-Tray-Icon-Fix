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

static int expect_hide_window(const wchar_t *exe, const wchar_t *class_name, const wchar_t *window_text, int expected, const char *name) {
    int actual = tray_rule_should_hide_window(exe, class_name, window_text);
    if (actual != expected) {
        printf("FAIL %s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_guid(const wchar_t *exe, int expected, const char *name) {
    GUID guid;
    int actual = tray_rule_guid_for_process(exe, &guid);
    if (actual != expected) {
        printf("FAIL %s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_block_uid(const wchar_t *exe, const wchar_t *class_name, unsigned int uid, int expected, const char *name) {
    int actual = tray_rule_block_uid_for_window(exe, class_name, uid);
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
    failures += expect_target(L"GoogleDriveFS.exe", 1, "google drive target process");
    failures += expect_target(L"Other.exe", 0, "other process is ignored");

    failures += expect_match(L"Lyricify Lite.exe", L"H.NotifyIcon_e87a2320-3a24-461c-99f6-c38bf4eb8d8b", 0, 1, "lyricify wrong icon");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"H.NotifyIcon_66a3fb03-7068-42a1-b1c9-861baae756c0", 0, 1, "betterlyrics wrong icon");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"H.NotifyIcon_31acf094-878f-42c9-b8e6-c1d89420a6e4", 0, 1, "volatile hnotifyicon class still matches");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"H.NotifyIcon_66a3fb03-7068-42a1-b1c9-861baae756c0", 1, 0, "uid prevents hiding normal sibling");
    failures += expect_match(L"BetterLyrics.WinUI3.exe", L"Windows.UI.Core.CoreWindow", 0, 0, "class prevents hiding normal sibling");
    failures += expect_match(L"Other.exe", L"H.NotifyIcon_66a3fb03-7068-42a1-b1c9-861baae756c0", 0, 0, "exe prevents cross-app hiding");

    failures += expect_match(L"explorer.exe", L"ATL:00007FFE7B28A050", 100, 1, "audio service tray icon");
    failures += expect_match(L"explorer.exe", L"ATL:00007FFF4A42A050", 100, 1, "usb speaker tray icon");
    failures += expect_match(L"explorer.exe", L"ATL:00007FF9BA0DA050", 101, 0, "microphone privacy icon is not a repeated delete rule");
    failures += expect_match(L"explorer.exe", L"ATL:00007FFE7B28A050", 0, 0, "audio service uid must be exact");
    failures += expect_match(L"explorer.exe", L"ATL:00007FF9BA0DA050", 102, 0, "microphone uid must be exact");
    failures += expect_match(L"explorer.exe", L"Shell_TrayWnd", 100, 0, "audio service class must be atl");
    failures += expect_match(L"explorer.exe", L"SystemTray_Main", 101, 0, "microphone class must be atl");
    failures += expect_block_uid(L"explorer.exe", L"ATL:00007FF9BA0DA050", 101, 1, "microphone privacy tray icon is blocked on create");
    failures += expect_block_uid(L"explorer.exe", L"ATL:00007FF9BA0DA050", 100, 0, "microphone block uid must be exact");
    failures += expect_block_uid(L"explorer.exe", L"SystemTray_Main", 101, 0, "microphone block class must be atl");

    failures += expect_match(L"GoogleDriveFS.exe", L"ATL:00007FF7481AE710", 11376, 0, "google drive uid is not a repeated delete rule");
    failures += expect_block_uid(L"GoogleDriveFS.exe", L"ATL:00007FF7481AE710", 11376, 1, "google drive uid tray icon is blocked on create");
    failures += expect_block_uid(L"GoogleDriveFS.exe", L"ATL:00007FF7481AE710", 0, 0, "google drive block uid must be exact");
    failures += expect_block_uid(L"GoogleDriveFS.exe", L"Windows.UI.Core.CoreWindow", 11376, 0, "google drive block class must be atl");

    failures += expect_hide_window(L"GoogleDriveFS.exe", L"DriveDot", L"Google Drive live edit status", 0, "google drive live edit dot is not hidden");
    failures += expect_hide_window(L"GoogleDriveFS.exe", L"ATL:00007FF7481AE710", L"", 0, "google drive normal atl window is not hidden");
    failures += expect_hide_window(L"Other.exe", L"DriveDot", L"Google Drive live edit status", 0, "drive dot rule requires exe");
    failures += expect_guid(L"GoogleDriveFS.exe", 0, "google drive guid icon is kept");
    failures += expect_guid(L"Other.exe", 0, "guid rule requires exe");

    if (failures) {
        return 1;
    }

    printf("rules_test: ok\n");
    return 0;
}
