/*
 * nanox.h
 *
 * Nano-inspired UI helpers for MicroEmacs (nanox layer)
 */

#ifndef NANOX_H_
#define NANOX_H_

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

enum nanox_lamp_state {
    NANOX_LAMP_OFF = 0,
    NANOX_LAMP_WARN,
    NANOX_LAMP_ERROR,
};

struct nanox_config {
    bool hint_bar;
    bool warning_lamp;
    char warning_format[8];
    char error_format[8];
    int help_key;
    char help_language[8];
    bool soft_tab;
    int soft_tab_width;
    bool case_sensitive_default;
};

extern struct nanox_config nanox_cfg;
extern char file_reserve[4][PATH_MAX];

void nanox_init(void);
void nanox_apply_config(void);

void nanox_set_lamp(enum nanox_lamp_state state);
enum nanox_lamp_state nanox_current_lamp(void);
const char *nanox_lamp_label(void);

int nanox_text_rows(void);
int nanox_hint_top_row(void);
int nanox_hint_bottom_row(void);

void nanox_notify_message(const char *text);

bool nanox_help_is_active(void);
void nanox_help_render(void);
int nanox_help_command(int f, int n);
int nanox_help_handle_key(int key);

int reserve_set_1(int f, int n);
int reserve_set_2(int f, int n);
int reserve_set_3(int f, int n);
int reserve_set_4(int f, int n);
int reserve_jump_1(int f, int n);
int reserve_jump_2(int f, int n);
int reserve_jump_3(int f, int n);
int reserve_jump_4(int f, int n);

void nanox_message_prefix(const char *input, char *output, size_t outsz);
void help_close(void);
void nanox_cleanup(void);

/* Paste slot functions */
int paste_slot_handle_key(int c);
int check_paste_slot_active(void);

#endif /* NANOX_H_ */
