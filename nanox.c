/*
 * nanox.c
 *
 * Nano-inspired UI helpers for MicroEmacs (nanox layer)
 */

#include "nanox.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "util.h"
#include "version.h"
#include "highlight.h"
#include "platform.h"
#include "colorscheme.h"
#include "paste_slot.h"

extern struct terminal term;

struct nanox_config nanox_cfg = {
    .hint_bar = true,
    .warning_lamp = true,
    .warning_format = "--W",
    .error_format = "--E",
    .help_key = SPEC | 'P',
    .help_language = "en",
    .soft_tab = false,
    .soft_tab_width = 8,
    .case_sensitive_default = false,
};

char file_reserve[4][PATH_MAX];

static enum nanox_lamp_state lamp_state = NANOX_LAMP_OFF;

struct nanox_help_topic {
    char *title;
    char **lines;
    size_t line_count;
    size_t line_cap;
};

static bool help_active;
static int help_selected;
static int help_scroll;
static bool help_show_section;
static int help_section_scroll;
static int help_section_sub_scroll;

static struct nanox_help_topic *dynamic_topics = NULL;
static size_t dynamic_topic_count = 0;

static int get_visual_row_count(const char *line, int width)
{
    if (!line || !*line) return 1; /* Empty line takes 1 row */
    if (width < 1) width = 1;

    int rows = 0;
    size_t len = strlen(line);
    size_t current_pos = 0;

    while (current_pos < len) {
        rows++;
        int chunk_width = 0;
        size_t idx = current_pos;
        int chunk_len = 0;

        while (idx < len) {
             unicode_t u;
             unsigned bytes = utf8_to_unicode((unsigned char*)line, idx, len, &u);
             if (bytes == 0) bytes = 1;
             int w = unicode_width(u);
             if (w < 0) w = 0;

             if (chunk_width + w > width) break;

             chunk_width += w;
             chunk_len += bytes;
             idx += bytes;
        }

        if (chunk_len == 0 && idx < len) {
             unicode_t u;
             unsigned bytes = utf8_to_unicode((unsigned char*)line, idx, len, &u);
             chunk_len = bytes;
        }
        current_pos += chunk_len;
    }
    return rows;
}

static void load_help_file(void)
{
    if (dynamic_topics) return;

    static char localized_name[32];
    char *path = NULL;

    if (nanox_cfg.help_language[0]) {
        snprintf(localized_name, sizeof(localized_name), "emacs-%s.hlp", nanox_cfg.help_language);
        path = flook(localized_name, TRUE);
    }
    if (!path)
        path = flook("emacs.hlp", TRUE);

    /* Fallback: Check User Config Directory */
    static char config_path[PATH_MAX];
    if (!path) {
        char dir[512];
        nanox_get_user_config_dir(dir, sizeof(dir));
        nanox_path_join(config_path, sizeof(config_path), dir, "emacs.hlp");
        if (nanox_file_exists(config_path))
            path = config_path;
    }

    if (!path) return;

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[256];
    struct nanox_help_topic *curr = NULL;

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = 0;
        }

        if (strncmp(line, "=>", 2) == 0) {
            struct nanox_help_topic *new_topics = realloc(dynamic_topics, sizeof(struct nanox_help_topic) * (dynamic_topic_count + 1));
            if (!new_topics) break;
            dynamic_topics = new_topics;
            curr = &dynamic_topics[dynamic_topic_count++];
            char *title = line + 2;
            while (*title == ' ') title++;
            curr->title = malloc(strlen(title) + 1);
            if (curr->title) strcpy(curr->title, title);
            curr->lines = NULL;
            curr->line_count = 0;
            curr->line_cap = 0;
        } else if (curr && strncmp(line, "-------", 7) != 0) {
            if (curr->line_count >= curr->line_cap) {
                size_t new_cap = curr->line_cap ? curr->line_cap * 2 : 16;
                char **new_lines = realloc(curr->lines, sizeof(char *) * new_cap);
                if (!new_lines) break;
                curr->lines = new_lines;
                curr->line_cap = new_cap;
            }
            curr->lines[curr->line_count] = malloc(strlen(line) + 1);
            if (curr->lines[curr->line_count]) {
                strcpy(curr->lines[curr->line_count], line);
                curr->line_count++;
            }
        }
    }
    fclose(fp);
}

static void config_defaults(void)
{
    nanox_cfg.hint_bar = true;
    nanox_cfg.warning_lamp = true;
    mystrscpy(nanox_cfg.warning_format, "--W", sizeof(nanox_cfg.warning_format));
    mystrscpy(nanox_cfg.error_format, "--E", sizeof(nanox_cfg.error_format));
    nanox_cfg.help_key = SPEC | 'P';
    mystrscpy(nanox_cfg.help_language, "en", sizeof(nanox_cfg.help_language));
    nanox_cfg.soft_tab = false;
    nanox_cfg.soft_tab_width = 8;
    nanox_cfg.case_sensitive_default = false;
}

static bool parse_bool(const char *value, bool *out)
{
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
        strcmp(value, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 ||
        strcmp(value, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static void mark_config_error(void)
{
    nanox_set_lamp(NANOX_LAMP_ERROR);
}

static void parse_ui_option(const char *key, const char *value)
{
    if (strcasecmp(key, "hint_bar") == 0) {
        if (!parse_bool(value, &nanox_cfg.hint_bar))
            mark_config_error();
    } else if (strcasecmp(key, "warning_lamp") == 0) {
        if (!parse_bool(value, &nanox_cfg.warning_lamp))
            mark_config_error();
    } else if (strcasecmp(key, "warning_format") == 0) {
        mystrscpy(nanox_cfg.warning_format, value, sizeof(nanox_cfg.warning_format));
    } else if (strcasecmp(key, "error_format") == 0) {
        mystrscpy(nanox_cfg.error_format, value, sizeof(nanox_cfg.error_format));
    } else if (strcasecmp(key, "help_key") == 0) {
        if (strcasecmp(value, "F1") == 0)
            nanox_cfg.help_key = SPEC | 'P';
        else
            mark_config_error();
    } else if (strcasecmp(key, "help_language") == 0) {
        if (!*value) {
            mark_config_error();
        } else {
            mystrscpy(nanox_cfg.help_language, value, sizeof(nanox_cfg.help_language));
            for (char *p = nanox_cfg.help_language; *p; ++p)
                *p = tolower((unsigned char)*p);
        }
    }
}

static void parse_edit_option(const char *key, const char *value)
{
    if (strcasecmp(key, "soft_tab") == 0) {
        if (!parse_bool(value, &nanox_cfg.soft_tab))
            mark_config_error();
    } else if (strcasecmp(key, "soft_tab_width") == 0) {
        int width = atoi(value);
        if (width <= 0)
            mark_config_error();
        else
            nanox_cfg.soft_tab_width = width;
    }
}

static void parse_search_option(const char *key, const char *value)
{
    if (strcasecmp(key, "case_sensitive_default") == 0) {
        if (!parse_bool(value, &nanox_cfg.case_sensitive_default))
            mark_config_error();
    }
}

static void parse_config_line(const char *section, char *line)
{
    char *equals = strchr(line, '=');

    if (!equals)
        return;
    *equals = 0;
    char *key = line;
    char *value = equals + 1;
    while (*key && isspace((unsigned char)*key))
        key++;
    while (*value && isspace((unsigned char)*value))
        value++;

    char *end = value + strlen(value);
    while (end > value && isspace((unsigned char)*(end - 1)))
        *--end = 0;
    end = key + strlen(key);
    while (end > key && isspace((unsigned char)*(end - 1)))
        *--end = 0;

    if (strcasecmp(section, "ui") == 0)
        parse_ui_option(key, value);
    else if (strcasecmp(section, "edit") == 0)
        parse_edit_option(key, value);
    else if (strcasecmp(section, "search") == 0)
        parse_search_option(key, value);
}

static void parse_config_file(void)
{
    char path[PATH_MAX];
    char dir[512];
    FILE *fp = NULL;
    char line[512];
    char section[32] = "";

    /* 1. Try Config Dir */
    nanox_get_user_config_dir(dir, sizeof(dir));
    nanox_path_join(path, sizeof(path), dir, "config");
    fp = fopen(path, "r");

    /* 2. Try Data Dir */
    if (!fp) {
        nanox_get_user_data_dir(dir, sizeof(dir));
        nanox_path_join(path, sizeof(path), dir, "config");
        fp = fopen(path, "r");
    }

    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp)) {
        char *ptr = line;

        while (*ptr && isspace((unsigned char)*ptr))
            ptr++;
        if (*ptr == '#' || *ptr == ';' || *ptr == 0)
            continue;
        if (*ptr == '[') {
            char *close = strchr(ptr, ']');
            if (close) {
                *close = 0;
                mystrscpy(section, ptr + 1, sizeof(section));
            }
            continue;
        }
        parse_config_line(section, ptr);
    }
    fclose(fp);
}

static bool join_if_exists(char *out, size_t cap, const char *dir, const char *file)
{
    if (!dir || !*dir)
        return false;
    nanox_path_join(out, cap, dir, file);
    if (!out[0])
        return false;
    return nanox_file_exists(out);
}

static bool find_highlight_rules(char *out, size_t cap)
{
    if (!out || cap == 0)
        return false;

    out[0] = 0;
    char dir[512];

    nanox_get_user_config_dir(dir, sizeof(dir));
    if (join_if_exists(out, cap, dir, "highlight.ini"))
        return true;
    if (join_if_exists(out, cap, dir, "syntax.ini"))
        return true;

    nanox_get_user_data_dir(dir, sizeof(dir));
    if (join_if_exists(out, cap, dir, "highlight.ini"))
        return true;
    if (join_if_exists(out, cap, dir, "syntax.ini"))
        return true;

    const char *fallbacks[] = {
        "configs/nanox/syntax.ini",
        "syntax.ini",
    };

    for (size_t i = 0; i < ARRAY_SIZE(fallbacks); i++) {
        if (nanox_file_exists(fallbacks[i])) {
            mystrscpy(out, fallbacks[i], cap);
            return true;
        }
    }
    return false;
}

void nanox_apply_config(void)
{
    if (nanox_cfg.soft_tab)
        tabsize = nanox_cfg.soft_tab_width;
    else
        tabsize = 0;

    if (nanox_cfg.case_sensitive_default)
        gmode |= MDEXACT;
    else
        gmode &= ~MDEXACT;
}

void nanox_init(void)
{
    config_defaults();
    parse_config_file();
    nanox_apply_config();

    /* Initialize Syntax Highlighting */
    char path[1024];

    if (!find_highlight_rules(path, sizeof(path)))
        path[0] = 0;

    highlight_init(path[0] ? path : NULL);
}

void nanox_set_lamp(enum nanox_lamp_state state)
{
    if (!nanox_cfg.warning_lamp)
        return;
    if (state == NANOX_LAMP_OFF) {
        lamp_state = NANOX_LAMP_OFF;
        return;
    }
    if (state > lamp_state)
        lamp_state = state;
}

enum nanox_lamp_state nanox_current_lamp(void)
{
    if (!nanox_cfg.warning_lamp)
        return NANOX_LAMP_OFF;
    return lamp_state;
}

const char *nanox_lamp_label(void)
{
    switch (nanox_current_lamp()) {
    case NANOX_LAMP_WARN:
        return nanox_cfg.warning_format;
    case NANOX_LAMP_ERROR:
        return nanox_cfg.error_format;
    default:
        return "";
    }
}

int nanox_text_rows(void)
{
    int rows = term.t_nrow - 2;
    if (rows < 1)
        rows = 1;
    return rows;
}

int nanox_hint_top_row(void)
{
    int row = term.t_nrow - 2;
    return row < 0 ? 0 : row;
}

int nanox_hint_bottom_row(void)
{
    int row = term.t_nrow - 1;
    return row < 0 ? 0 : row;
}

static void replace_all(char *buffer, size_t bufsz, const char *needle, const char *replacement)
{
    char tmp[1024];
    size_t needle_len = strlen(needle);
    size_t repl_len = strlen(replacement);
    size_t tmp_size = sizeof(tmp);

    if (!needle_len)
        return;

    while (1) {
        char *pos = strstr(buffer, needle);
        size_t prefix_len, suffix_len, total;

        if (!pos)
            break;
        prefix_len = pos - buffer;
        suffix_len = strlen(pos + needle_len);
        total = prefix_len + repl_len + suffix_len;
        if (total + 1 > tmp_size)
            break;
        memcpy(tmp, buffer, prefix_len);
        memcpy(tmp + prefix_len, replacement, repl_len);
        memcpy(tmp + prefix_len + repl_len, pos + needle_len, suffix_len + 1);
        mystrscpy(buffer, tmp, bufsz);
    }
}

void nanox_message_prefix(const char *input, char *output, size_t outsz)
{
    char temp[1024];

    mystrscpy(temp, input ? input : "", sizeof(temp));
    replace_all(temp, sizeof(temp), "Buffer List", "File List");
    replace_all(temp, sizeof(temp), "buffer list", "file list");
    replace_all(temp, sizeof(temp), "Switch Buffer", "Switch File");
    replace_all(temp, sizeof(temp), "switch buffer", "switch file");
    replace_all(temp, sizeof(temp), "Buffer", "File");
    replace_all(temp, sizeof(temp), "buffer", "file");
    replace_all(temp, sizeof(temp), "BUFFER", "FILE");

    if (!*temp) {
        mystrscpy(output, temp, outsz);
        return;
    }

//  if (strncmp(temp, "[File Mode]", 11) == 0)
//      mystrscpy(output, temp, outsz);
//  else
//      snprintf(output, outsz, "[File Mode] %s", temp);
    snprintf(output, outsz, "%s", temp);
}

void nanox_notify_message(const char *text)
{
    if (!text || !*text)
        nanox_set_lamp(NANOX_LAMP_OFF);
}

static void help_puts(const char *text)
{
    while (*text)
        ttputc(*text++);
}

static void help_putsn(const char *text, int n)
{
    for (int i = 0; i < n && text[i]; i++)
        ttputc(text[i]);
}

/* Help System Enhancements */

void nanox_help_render(void)
{
    load_help_file();
    if (!dynamic_topics) return;

    /* Handle screen resize while help is active */
    extern int chg_width, chg_height;
    if (chg_width || chg_height) {
        extern int newscreensize(int h, int w);
        newscreensize(chg_height, chg_width);
    }

    /* Force Clean White Screen (Black FG, White BG) */
    help_puts("\033[0m");       /* Reset */
    help_puts("\033[30;47m");   /* Black on White */
    help_puts("\033[H");        /* Home Cursor */
    help_puts("\033[2J");       /* Clear Screen */

    int visible_rows = nanox_hint_top_row() - 3;
    if (visible_rows < 1) visible_rows = 1;
    int max_r = nanox_hint_top_row() - 1;

    movecursor(0, 0);
    if (help_show_section) {
        /* Header */
        help_puts(" [ HELP: ");
        help_puts(dynamic_topics[help_selected].title);
        help_puts(" ]");

        movecursor(1, 0);
        for (int i = 0; i < term.t_ncol; i++) help_puts("-");

        /* Content with Visual Wrapping */
        int r = 2;
        struct nanox_help_topic *topic = &dynamic_topics[help_selected];
        
        for (size_t i = help_section_scroll; i < topic->line_count && r < max_r; ++i) {
            char *line = topic->lines[i];
            size_t len = strlen(line);

            /* If empty line, print nothing but move down */
            if (len == 0) {
                if (i == help_section_scroll && help_section_sub_scroll > 0) {
                    /* Should not happen for empty line, but safety check */
                } else {
                    movecursor(r, 0);
                    help_puts("  ");
                    r++;
                }
                continue;
            }

            size_t current_pos = 0;
            int avail_width = term.t_ncol - 2;
            if (avail_width < 1) avail_width = 1;
            int current_sub_line = 0;

            while (current_pos < len && r < max_r) {
                int chunk_len = 0;
                int chunk_width = 0;
                size_t idx = current_pos;

                /* Calculate how much fits */
                while (idx < len) {
                    unicode_t u;
                    unsigned bytes = utf8_to_unicode((unsigned char*)line, idx, len, &u);
                    if (bytes == 0) bytes = 1; 

                    int w = unicode_width(u);
                    if (w < 0) w = 0;

                    if (chunk_width + w > avail_width) break;

                    chunk_width += w;
                    chunk_len += bytes;
                    idx += bytes;
                }
                
                /* Ensure progress */
                if (chunk_len == 0 && idx < len) {
                     unicode_t u;
                     unsigned bytes = utf8_to_unicode((unsigned char*)line, idx, len, &u);
                     chunk_len = bytes;
                }

                /* Check if we should skip this visual line due to scrolling */
                if (i == help_section_scroll && current_sub_line < help_section_sub_scroll) {
                    current_sub_line++;
                    current_pos += chunk_len;
                    continue;
                }

                movecursor(r, 0);
                help_puts("  ");
                help_putsn(line + current_pos, chunk_len);

                current_pos += chunk_len;
                current_sub_line++;
                r++;
            }
        }

        /* Footer */
        movecursor(nanox_hint_top_row() - 1, 0);
        for (int i = 0; i < term.t_ncol; i++) help_puts("-");
        movecursor(nanox_hint_top_row() - 1, 2);
        help_puts(" i/k to Scroll Up/Down, Backspace: Back, F1: Close ");
    } else {
        /* Header */
        help_puts(" [ Nanox Help System ]");

        movecursor(1, 0);
        for (int i = 0; i < term.t_ncol; i++) help_puts("-");

        /* Menu */
        int r = 2;
        for (size_t i = help_scroll; i < dynamic_topic_count && r < max_r; ++i, ++r) {
            movecursor(r, 0);
            if ((int)i == help_selected) {
                help_puts(" > ");
                help_puts(dynamic_topics[i].title);
                help_puts(" <");
            } else {
                help_puts("   ");
                help_puts(dynamic_topics[i].title);
            }
        }

        /* Footer */
        movecursor(nanox_hint_top_row() - 1, 0);
        for (int i = 0; i < term.t_ncol; i++) help_puts("-");
        movecursor(nanox_hint_top_row() - 1, 2);
        help_puts(" Up: i, Down: k, Enter: View, F1/Esc: Exit ");
    }
    ttflush();
}
int nanox_help_command(int f, int n)
{
    load_help_file();
    if (!dynamic_topics) {
        mlwrite("Error: Help file (emacs.hlp) not found!");
        nanox_set_lamp(NANOX_LAMP_ERROR);
        return FALSE;
    }

    help_active = true;
    help_selected = 0;
    help_scroll = 0;
    help_show_section = false;
    help_section_scroll = 0;
    help_section_sub_scroll = 0;

    /* * Do NOT call update(TRUE) here. On large multi-byte files, 
     * update() hangs calculating columns. We skip it and draw directly.
     */
    sgarbf = TRUE;
    nanox_help_render();
    return TRUE;
}

void help_close(void)
{
    help_active = false;
    sgarbf = TRUE;
    update(TRUE);  /* Force immediate screen redraw when exiting help */
}

int nanox_help_handle_key(int key)
{
    if (!help_active)
        return FALSE;

    if (key < 0) return TRUE;

    /* Normalize key for navigation (ignore modifiers) */
    int base_key = key;
    if (key & SPEC) {
        base_key = SPEC | (key & 0xFF);
    }

    int visible_rows = nanox_hint_top_row() - 3;
    if (visible_rows < 1) visible_rows = 1;

    switch (base_key) {
    case CONTROL | 'G':
    case CONTROL | '[':
    case 27:
    case SPEC | 'P': /* F1 */
        help_close();
        return TRUE;

    case 0x7F: /* Backspace */
    case CONTROL | 'H':
        if (help_show_section) {
            help_show_section = false;
        } else {
            help_close();
            return TRUE;
        }
        break;

    case CONTROL | 'M':
    case '\r':
    case CONTROL | 'J':
    case '\n':
        if (!help_show_section) {
            help_show_section = true;
            help_section_scroll = 0;
        }
        break;

    case 'i': /* Up */
        if (help_show_section) {
            if (help_section_sub_scroll > 0) {
                help_section_sub_scroll--;
            } else if (help_section_scroll > 0) {
                help_section_scroll--;
                struct nanox_help_topic *topic = &dynamic_topics[help_selected];
                int rows = get_visual_row_count(topic->lines[help_section_scroll], term.t_ncol - 2);
                help_section_sub_scroll = rows > 0 ? rows - 1 : 0;
            }
        } else {
            if (help_selected > 0) {
                --help_selected;
                if (help_selected < help_scroll) help_scroll = help_selected;
            }
        }
        break;

    case 'k': /* Down */
        if (help_show_section) {
            struct nanox_help_topic *topic = &dynamic_topics[help_selected];
            int rows = get_visual_row_count(topic->lines[help_section_scroll], term.t_ncol - 2);
            
            if (help_section_sub_scroll + 1 < rows) {
                help_section_sub_scroll++;
            } else if (help_section_scroll < (int)topic->line_count - 1) {
                help_section_scroll++;
                help_section_sub_scroll = 0;
            }
        } else {
            if (help_selected < (int)dynamic_topic_count - 1) {
                ++help_selected;
                if (help_selected >= help_scroll + visible_rows)
                    help_scroll++;
            }
        }
        break;

    default:
        break;
    }
    nanox_help_render();
    return TRUE;
}

static const char *slot_name(int slot)
{
    static const char *names[] = { "F9", "F10", "F11", "F12" };
    if (slot < 0 || slot >= 4)
        return "?";
    return names[slot];
}

static int reserve_set(int slot)
{
    char prompt[64];
    char path[PATH_MAX];
    char msg[PATH_MAX + 64];
    int rc;

    if (slot < 0 || slot >= 4) return FALSE;

    snprintf(prompt, sizeof(prompt), "Reserve %s file: ", slot_name(slot));
    rc = minibuf_input(prompt, path, sizeof(path));
    if (rc != TRUE)
        return rc;
    if (!*path)
        return FALSE;
    mystrscpy(file_reserve[slot], path, sizeof(file_reserve[slot]));
    snprintf(msg, sizeof(msg), "Reserved %s = %s", slot_name(slot), path);
    minibuf_show(msg);
    return TRUE;
}

static int reserve_jump(int slot)
{
    int rc;
    char msg[PATH_MAX + 64];

    if (slot < 0 || slot >= 4) return FALSE;

    if (!file_reserve[slot][0]) {
        nanox_set_lamp(NANOX_LAMP_WARN);
        snprintf(msg, sizeof(msg), "%s is empty (use Ctrl+%s to reserve)",
            slot_name(slot), slot_name(slot));
        minibuf_show(msg);
        return FALSE;
    }
    rc = getfile(file_reserve[slot], TRUE);
    if (rc == TRUE) {
        snprintf(msg, sizeof(msg), "Jump %s -> %s", slot_name(slot), file_reserve[slot]);
        minibuf_show(msg);
        nanox_set_lamp(NANOX_LAMP_OFF);
    } else {
        nanox_set_lamp(NANOX_LAMP_ERROR);
    }
    return rc;
}

int reserve_set_1(int f, int n) { return reserve_set(0); }
int reserve_set_2(int f, int n) { return reserve_set(1); }
int reserve_set_3(int f, int n) { return reserve_set(2); }
int reserve_set_4(int f, int n) { return reserve_set(3); }

int reserve_jump_1(int f, int n) { return reserve_jump(0); }
int reserve_jump_2(int f, int n) { return reserve_jump(1); }
int reserve_jump_3(int f, int n) { return reserve_jump(2); }
int reserve_jump_4(int f, int n) { return reserve_jump(3); }

void nanox_cleanup(void)
{
    if (dynamic_topics) {
        for (size_t i = 0; i < dynamic_topic_count; i++) {
            struct nanox_help_topic *topic = &dynamic_topics[i];
            if (topic->title) free(topic->title);
            if (topic->lines) {
                for (size_t j = 0; j < topic->line_count; j++) {
                    if (topic->lines[j]) free(topic->lines[j]);
                }
                free(topic->lines);
            }
        }
        free(dynamic_topics);
        dynamic_topics = NULL;
        dynamic_topic_count = 0;
    }
}

bool nanox_help_is_active(void) {
    return help_active;
}

/* Call original uEmacs help function or create a dummy */
int help(int f, int n) {
    return nanox_help_command(f, n);
}

/* Paste Slot Window handling */

int paste_slot_handle_key(int c)
{
    extern void paste_slot_set_active(int active);
    extern void paste_slot_clear(void);
    extern int paste_slot_insert(void);
    extern int update(int force);
    extern int sgarbf;
    int action_taken = FALSE;
    
    /* Check for 'p' or 'P' key or Enter - insert paste */
    if (c == 'p' || c == 'P' || c == '\r' || c == '\n' || c == 13) {
        /* Insert the paste slot content */
        paste_slot_insert();
        action_taken = TRUE;
    }
    /* ESC key - cancel */
    else if (c == (CONTROL | '[') || c == 27) {
        action_taken = TRUE;
    }
    
    if (action_taken) {
        paste_slot_set_active(0);
        paste_slot_clear();
        /* Force full window and modeline redraw to restore editor UI immediately */
        curwp->w_flag |= WFHARD | WFMODE;
        sgarbf = TRUE;
        update(TRUE);
        return TRUE;
    }
    
    /* For now, just show message for other keys */
    mlwrite("Press 'p' to paste, ESC to cancel");
    return TRUE;
}

int check_paste_slot_active(void)
{
    extern int paste_slot_is_active(void);
    return paste_slot_is_active();
}
