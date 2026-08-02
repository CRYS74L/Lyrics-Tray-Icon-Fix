#include "rules.h"

#include <wctype.h>

typedef struct TrayRule {
    const wchar_t *exe_name;
    const wchar_t *class_prefix;
    unsigned int uid;
} TrayRule;

typedef struct WindowRule {
    const wchar_t *exe_name;
    const wchar_t *class_name;
    const wchar_t *window_text;
} WindowRule;

typedef struct GuidRule {
    const wchar_t *exe_name;
    GUID guid;
} GuidRule;

typedef struct BlockUidRule {
    const wchar_t *exe_name;
    const wchar_t *class_prefix;
    unsigned int uid;
    int use_message_hook;
} BlockUidRule;

static const TrayRule kRules[] = {
    { L"LYRICIFY LITE.EXE", L"H.NotifyIcon_", 0 },
    { L"BETTERLYRICS.WINUI3.EXE", L"H.NotifyIcon_", 0 },
};

static const GuidRule kGuidRules[] = {
    { NULL, { 0 } },
};

static const BlockUidRule kBlockUidRules[] = {
    { L"EXPLORER.EXE", L"ATL:", 100, 1 },
    { L"EXPLORER.EXE", L"ATL:", 101, 1 },
    { L"GOOGLEDRIVEFS.EXE", L"ATL:", 11376, 0 },
};

static const WindowRule kWindowRules[] = {
    { NULL, NULL, NULL },
};

static int equals_ignore_case(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        if (towupper(*a) != towupper(*b)) {
            return 0;
        }
        ++a;
        ++b;
    }

    return *a == L'\0' && *b == L'\0';
}

static int starts_with(const wchar_t *value, const wchar_t *prefix) {
    if (!value || !prefix) {
        return 0;
    }

    while (*prefix) {
        if (*value != *prefix) {
            return 0;
        }
        ++value;
        ++prefix;
    }

    return 1;
}

static int equals_text(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) {
        return 0;
    }

    return wcscmp(a, b) == 0;
}

int tray_rule_matches(const wchar_t *exe_name, const wchar_t *class_name, unsigned int uid) {
    if (!exe_name || !class_name) {
        return 0;
    }

    for (int i = 0; i < tray_rule_count(); ++i) {
        if (uid == kRules[i].uid &&
            equals_ignore_case(exe_name, kRules[i].exe_name) &&
            starts_with(class_name, kRules[i].class_prefix)) {
            return 1;
        }
    }

    return 0;
}

int tray_rule_uid_for_window(const wchar_t *exe_name, const wchar_t *class_name, unsigned int *uid) {
    if (!exe_name || !class_name || !uid) {
        return 0;
    }

    for (int i = 0; i < tray_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kRules[i].exe_name) &&
            starts_with(class_name, kRules[i].class_prefix)) {
            *uid = kRules[i].uid;
            return 1;
        }
    }

    return 0;
}

int tray_rule_should_hide_window(const wchar_t *exe_name, const wchar_t *class_name, const wchar_t *window_text) {
    if (!exe_name || !class_name || !window_text) {
        return 0;
    }

    for (int i = 0; i < tray_window_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kWindowRules[i].exe_name) &&
            equals_text(class_name, kWindowRules[i].class_name) &&
            equals_text(window_text, kWindowRules[i].window_text)) {
            return 1;
        }
    }

    return 0;
}

int tray_rule_guid_for_process(const wchar_t *exe_name, GUID *guid) {
    if (!exe_name || !guid) {
        return 0;
    }

    for (int i = 0; i < tray_guid_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kGuidRules[i].exe_name)) {
            *guid = kGuidRules[i].guid;
            return 1;
        }
    }

    return 0;
}

int tray_rule_block_uid_for_window(const wchar_t *exe_name, const wchar_t *class_name, unsigned int uid) {
    if (!exe_name || !class_name) {
        return 0;
    }

    for (int i = 0; i < tray_block_uid_rule_count(); ++i) {
        if (uid == kBlockUidRules[i].uid &&
            equals_ignore_case(exe_name, kBlockUidRules[i].exe_name) &&
            starts_with(class_name, kBlockUidRules[i].class_prefix)) {
            return 1;
        }
    }

    return 0;
}

int tray_rule_block_uid_for_window_class(const wchar_t *exe_name, const wchar_t *class_name, unsigned int *uid) {
    if (!exe_name || !class_name || !uid) {
        return 0;
    }

    for (int i = 0; i < tray_block_uid_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kBlockUidRules[i].exe_name) &&
            starts_with(class_name, kBlockUidRules[i].class_prefix)) {
            *uid = kBlockUidRules[i].uid;
            return 1;
        }
    }

    return 0;
}

int tray_rule_block_uid_for_window_class_at(const wchar_t *exe_name, const wchar_t *class_name,
                                            int index, unsigned int *uid) {
    if (!exe_name || !class_name || !uid || index < 0 || index >= tray_block_uid_rule_count()) {
        return 0;
    }
    if (!equals_ignore_case(exe_name, kBlockUidRules[index].exe_name) ||
        !starts_with(class_name, kBlockUidRules[index].class_prefix)) {
        return 0;
    }
    *uid = kBlockUidRules[index].uid;
    return 1;
}

int tray_rule_block_uid_uses_message_hook(int index) {
    if (index < 0 || index >= tray_block_uid_rule_count()) {
        return 0;
    }
    return kBlockUidRules[index].use_message_hook;
}

int tray_rule_process_is_target(const wchar_t *exe_name) {
    return tray_rule_process_uses_message_hook(exe_name) ||
           tray_rule_process_uses_shell_notify_block(exe_name);
}

int tray_rule_process_uses_message_hook(const wchar_t *exe_name) {
    if (!exe_name) {
        return 0;
    }

    for (int i = 0; i < tray_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kRules[i].exe_name)) {
            return 1;
        }
    }

    for (int i = 0; i < tray_window_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kWindowRules[i].exe_name)) {
            return 1;
        }
    }

    for (int i = 0; i < tray_guid_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kGuidRules[i].exe_name)) {
            return 1;
        }
    }

    for (int i = 0; i < tray_block_uid_rule_count(); ++i) {
        if (kBlockUidRules[i].use_message_hook &&
            equals_ignore_case(exe_name, kBlockUidRules[i].exe_name)) {
            return 1;
        }
    }

    return 0;
}

int tray_rule_process_uses_shell_notify_block(const wchar_t *exe_name) {
    if (!exe_name) {
        return 0;
    }

    for (int i = 0; i < tray_guid_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kGuidRules[i].exe_name)) {
            return 1;
        }
    }

    for (int i = 0; i < tray_block_uid_rule_count(); ++i) {
        if (equals_ignore_case(exe_name, kBlockUidRules[i].exe_name)) {
            return 1;
        }
    }

    return 0;
}

int tray_rule_count(void) {
    return (int)(sizeof(kRules) / sizeof(kRules[0]));
}

const wchar_t *tray_rule_exe(int index) {
    if (index < 0 || index >= tray_rule_count()) {
        return L"";
    }
    return kRules[index].exe_name;
}

const wchar_t *tray_rule_class_name(int index) {
    if (index < 0 || index >= tray_rule_count()) {
        return L"";
    }
    return kRules[index].class_prefix;
}

unsigned int tray_rule_uid(int index) {
    if (index < 0 || index >= tray_rule_count()) {
        return 0;
    }
    return kRules[index].uid;
}

int tray_window_rule_count(void) {
    return 0;
}

const wchar_t *tray_window_rule_exe(int index) {
    if (index < 0 || index >= tray_window_rule_count()) {
        return L"";
    }
    return kWindowRules[index].exe_name;
}

const wchar_t *tray_window_rule_class_name(int index) {
    if (index < 0 || index >= tray_window_rule_count()) {
        return L"";
    }
    return kWindowRules[index].class_name;
}

const wchar_t *tray_window_rule_text(int index) {
    if (index < 0 || index >= tray_window_rule_count()) {
        return L"";
    }
    return kWindowRules[index].window_text;
}

int tray_guid_rule_count(void) {
    return 0;
}

const wchar_t *tray_guid_rule_exe(int index) {
    if (index < 0 || index >= tray_guid_rule_count()) {
        return L"";
    }
    return kGuidRules[index].exe_name;
}

const GUID *tray_guid_rule_guid(int index) {
    if (index < 0 || index >= tray_guid_rule_count()) {
        return NULL;
    }
    return &kGuidRules[index].guid;
}

int tray_block_uid_rule_count(void) {
    return (int)(sizeof(kBlockUidRules) / sizeof(kBlockUidRules[0]));
}

const wchar_t *tray_block_uid_rule_exe(int index) {
    if (index < 0 || index >= tray_block_uid_rule_count()) {
        return L"";
    }
    return kBlockUidRules[index].exe_name;
}

const wchar_t *tray_block_uid_rule_class_name(int index) {
    if (index < 0 || index >= tray_block_uid_rule_count()) {
        return L"";
    }
    return kBlockUidRules[index].class_prefix;
}

unsigned int tray_block_uid_rule_uid(int index) {
    if (index < 0 || index >= tray_block_uid_rule_count()) {
        return 0;
    }
    return kBlockUidRules[index].uid;
}
