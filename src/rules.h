#ifndef LYRICS_TRAY_RULES_H
#define LYRICS_TRAY_RULES_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

int tray_rule_matches(const wchar_t *exe_name, const wchar_t *class_name, unsigned int uid);
int tray_rule_uid_for_window(const wchar_t *exe_name, const wchar_t *class_name, unsigned int *uid);
int tray_rule_should_hide_window(const wchar_t *exe_name, const wchar_t *class_name, const wchar_t *window_text);
int tray_rule_guid_for_process(const wchar_t *exe_name, GUID *guid);
int tray_rule_block_uid_for_window(const wchar_t *exe_name, const wchar_t *class_name, unsigned int uid);
int tray_rule_block_uid_for_window_class(const wchar_t *exe_name, const wchar_t *class_name, unsigned int *uid);
int tray_rule_block_uid_for_window_class_at(const wchar_t *exe_name, const wchar_t *class_name,
                                            int index, unsigned int *uid);
int tray_rule_block_uid_uses_message_hook(int index);
int tray_rule_process_is_target(const wchar_t *exe_name);
int tray_rule_process_uses_message_hook(const wchar_t *exe_name);
int tray_rule_process_uses_shell_notify_block(const wchar_t *exe_name);
int tray_rule_should_write_ps_tray_factory(const wchar_t *exe_name);
int tray_rule_count(void);
const wchar_t *tray_rule_exe(int index);
const wchar_t *tray_rule_class_name(int index);
unsigned int tray_rule_uid(int index);
int tray_window_rule_count(void);
const wchar_t *tray_window_rule_exe(int index);
const wchar_t *tray_window_rule_class_name(int index);
const wchar_t *tray_window_rule_text(int index);
int tray_guid_rule_count(void);
const wchar_t *tray_guid_rule_exe(int index);
const GUID *tray_guid_rule_guid(int index);
int tray_block_uid_rule_count(void);
const wchar_t *tray_block_uid_rule_exe(int index);
const wchar_t *tray_block_uid_rule_class_name(int index);
unsigned int tray_block_uid_rule_uid(int index);

#ifdef __cplusplus
}
#endif

#endif
