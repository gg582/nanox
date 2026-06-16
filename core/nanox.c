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
#include <unistd.h>

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
#include "scraper.h"
#include "video.h"

extern struct video **vscreen;
extern struct terminal *term;

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
    .autocomplete = true,
    .use_lsp = false,
    .use_auto_doc_completion = true,
    .no_function_slot = false,
};

char file_reserve[NANOX_SLOT_MAX][PATH_MAX];
int last_slot_index = -1;

bool should_redraw_underbar = false;

static enum nanox_lamp_state lamp_state = NANOX_LAMP_OFF;
static char **startup_slot_queue = NULL;
static size_t startup_slot_queue_count = 0;
static size_t startup_slot_queue_cap = 0;
static size_t startup_slot_queue_next = 0;

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

void nanox_request_underbar_redraw(void)
{
    should_redraw_underbar = true;
}

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
    char dir[512];
    static char config_path[PATH_MAX];
    static char localized_config_path[PATH_MAX];

    nanox_get_user_config_dir(dir, sizeof(dir));

    if (nanox_cfg.help_language[0]) {
        snprintf(localized_name, sizeof(localized_name), "emacs-%s.hlp", nanox_cfg.help_language);
        nanox_path_join(localized_config_path, sizeof(localized_config_path), dir, localized_name);
        if (nanox_file_exists(localized_config_path))
            path = localized_config_path;
    }

    if (!path) {
        nanox_path_join(config_path, sizeof(config_path), dir, "emacs.hlp");
        if (nanox_file_exists(config_path))
            path = config_path;
    }

    if (!path && nanox_file_exists("emacs.hlp"))
        path = "emacs.hlp";

    if (!path && nanox_cfg.help_language[0])
        path = flook(localized_name, TRUE);
    if (!path)
        path = flook("emacs.hlp", TRUE);

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
            if (curr->title) mystrscpy(curr->title, title, strlen(title) + 1);
            curr->lines = NULL;
            curr->line_count = 0;
            curr->line_cap = 0;
        } else {
            if (!curr && len > 0) {
                /* Create a default section if we have content before any => header */
                struct nanox_help_topic *new_topics = realloc(dynamic_topics, sizeof(struct nanox_help_topic) * (dynamic_topic_count + 1));
                if (!new_topics) break;
                dynamic_topics = new_topics;
                curr = &dynamic_topics[dynamic_topic_count++];
                curr->title = malloc(strlen("Help Information") + 1);
                if (curr->title) mystrscpy(curr->title, "Help Information", strlen("Help Information") + 1);
                curr->lines = NULL;
                curr->line_count = 0;
                curr->line_cap = 0;
            }
            if (curr) {
                if (curr->line_count >= curr->line_cap) {
                    size_t new_cap = curr->line_cap ? curr->line_cap * 2 : 16;
                    char **new_lines = realloc(curr->lines, sizeof(char *) * new_cap);
                    if (!new_lines) break;
                    curr->lines = new_lines;
                    curr->line_cap = new_cap;
                }
                curr->lines[curr->line_count] = malloc(strlen(line) + 1);
                if (curr->lines[curr->line_count]) {
                    mystrscpy(curr->lines[curr->line_count], line, strlen(line) + 1);
                    curr->line_count++;
                }
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
    nanox_cfg.autocomplete = true;
    nanox_cfg.use_lsp = false;
    nanox_cfg.use_auto_doc_completion = true;
    nanox_cfg.nonr = false;
    nanox_cfg.no_function_slot = false;
    nanox_cfg.cold_storage_timeout = 30;
    nanox_cfg.ai_enabled = false;
    mystrscpy(nanox_cfg.ai_model, "qwen2.5-coder:1.5b", sizeof(nanox_cfg.ai_model));
    mystrscpy(nanox_cfg.ai_endpoint, "http://localhost:11434/api/generate", sizeof(nanox_cfg.ai_endpoint));
    nanox_cfg.ai_temperature = 0.2;
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
    } else if (strcasecmp(key, "nonr") == 0) {
        if (!parse_bool(value, &nanox_cfg.nonr))
            mark_config_error();
    } else if (strcasecmp(key, "no_function_slot") == 0) {
        if (!parse_bool(value, &nanox_cfg.no_function_slot))
            mark_config_error();
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
    } else if (strcasecmp(key, "autocomplete") == 0) {
        if (!parse_bool(value, &nanox_cfg.autocomplete))
            mark_config_error();
    } else if (strcasecmp(key, "use_lsp") == 0) {
        if (!parse_bool(value, &nanox_cfg.use_lsp))
            mark_config_error();
    } else if (strcasecmp(key, "use_auto_doc_completion") == 0) {
        if (!parse_bool(value, &nanox_cfg.use_auto_doc_completion))
            mark_config_error();
    } else if (strcasecmp(key, "cold_storage_timeout") == 0) {
        int timeout = atoi(value);
        if (timeout < 0)
            mark_config_error();
        else
            nanox_cfg.cold_storage_timeout = timeout;
    }
}

static void parse_search_option(const char *key, const char *value)
{
    if (strcasecmp(key, "case_sensitive_default") == 0) {
        if (!parse_bool(value, &nanox_cfg.case_sensitive_default))
            mark_config_error();
    }
}

static void parse_ai_option(const char *key, const char *value)
{
    if (strcasecmp(key, "enabled") == 0) {
        if (!parse_bool(value, &nanox_cfg.ai_enabled))
            mark_config_error();
    } else if (strcasecmp(key, "model") == 0) {
        mystrscpy(nanox_cfg.ai_model, value, sizeof(nanox_cfg.ai_model));
    } else if (strcasecmp(key, "endpoint") == 0) {
        mystrscpy(nanox_cfg.ai_endpoint, value, sizeof(nanox_cfg.ai_endpoint));
    } else if (strcasecmp(key, "temperature") == 0) {
        nanox_cfg.ai_temperature = atof(value);
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
    else if (strcasecmp(section, "ai") == 0)
        parse_ai_option(key, value);
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
    scraper_init();
    last_slot_index = -1;
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
    int rows = term->t_nrow - 2;
    if (rows < 1)
        rows = 1;
    return rows;
}

int nanox_text_cols(void)
{
    int cols = term->t_ncol;
    if (!nanox_cfg.nonr)
        cols -= 8;
    /* Use 1 column margin for safety instead of 2 */
    cols -= 1;
    if (cols < 1)
        cols = 1;
    return cols;
}

int nanox_hint_top_row(void)
{
    int row = term->t_nrow - 2;
    return row < 0 ? 0 : row;
}

int nanox_hint_bottom_row(void)
{
    int row = term->t_nrow - 1;
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

static void help_puts_width(const char *text, int max_cols)
{
    if (!text || max_cols <= 0)
        return;

    size_t len = strlen(text);
    size_t idx = 0;
    int used = 0;
    const unsigned char *bytes = (const unsigned char *)text;

    while (idx < len) {
        unicode_t uc;
        int consumed = utf8_to_unicode((unsigned char *)bytes, idx, len, &uc);
        if (consumed <= 0)
            break;

        int width = mystrnlen_raw_w(uc);
        if (used + width > max_cols)
            break;

        ttputc(uc);
        used += width;
        idx += consumed;
    }
}

/* Help System Enhancements */

static const char *nanox_help_sheet[] = {
    "===============================================================================",
    "=>                      NANOX SYSTEM BINDINGS & SEARCH SPEC",
    "-------------------------------------------------------------------------------",
    "FILE & SLOT CONTROL     EDITING & SEARCH        INDENT / OUTDENT",
    "F2 / ^S : Save File     ^K : Cut Current Line   ^J : Start Indent Range",
    "F3 / ^O : Open File     F7 / ^X : Cut(S:End)    ^H : Start Outdent Range",
    "F4 / ^Q : Quit nanox    F6 / ^W : Copy(S:End)   Tab/gg: Apply Range",
"F1 / ^H : Help Menu     ^V : Command Mode       BS : Cancel Range",
"F9-F12 : File Slots     F8/^Y : Paste           ------------------",
    "===============================================================================",
"* Ctrl+V opens command mode (empty bracketed paste also falls back here)",
    "* Terminal paste in color mode opens a green preview; press p to insert it",
    "* viblock-edit inserts the same text on each selected line",
    "* viblock-replace replaces the whole selected block with one input string",
    "* viblock-set-nr start-end [rev] rewrites numbered list prefixes in range",
    "* viblock-flip a-b c-d swaps two non-overlapping line ranges",
    "* F9-F12 are slot jumps; Ctrl+Alt+9, 0, -, = map to F9-F12",
    "* F8 (and Ctrl+Alt+8) pastes from the copy/cut buffer",
    "* nx *.txt queues files into slots instead of opening every file immediately",
    "* no_function_slot = true changes the hint bar to ^A+num Slot and switches",
    "  slot access to Ctrl+Alt+number mode with 64 slots",
    "* Indent/Outdent: Ctrl+J (indent) or Ctrl+H (outdent) to mark start line",
    "* Move cursor to end line, then press Tab or gg to apply to the range",
    "* Auto-detects file indentation width (spaces or tabs)",
    "* BS: Backspace cancels current range operation",
    "===============================================================================",
    "=>                      LSP & WORKSPACE FILE TREE",
    "-------------------------------------------------------------------------------",
    "* LSP & Autocomplete Priority: Sorts completion candidates using semantic",
    "  priority (Snippets > Keywords > Functions). Exact/prefix matches get boosted.",
    "  Cross-Reference: Integrates with Workspace File Tree for a complete IDE experience.",
    "* Workspace File Tree: Type openFileTree (or openFileView) to open left panel,",
    "  closeFileTree to close. Type runFileTree for interactive navigation mode",
    "  (Arrows to move, Enter to toggle/open, Shift+Backspace to go up).",
    "  Cross-Reference: Combines with LSP Autocomplete for fast workspace editing.",
    "* AI Copilot (Ollama): Enable it in ~/.config/nanox/config under [ai]. Press Ctrl+Alt+A",
    "  or run ai to complete. 'y' to accept/keep, 'n' (or any key) to reject/revert.",
    NULL
};

static bool help_topic_has_title(const struct nanox_help_topic *topic)
{
    if (!topic || !topic->title)
        return false;
    return strcmp(topic->title, "Help Information") != 0;
}

static int help_total_lines(void)
{
    int total = 0;
    if (dynamic_topics && dynamic_topic_count > 0) {
        for (size_t t = 0; t < dynamic_topic_count; ++t) {
            struct nanox_help_topic *topic = &dynamic_topics[t];
            if (!topic)
                continue;
            if (help_topic_has_title(topic))
                ++total;
            total += (int)topic->line_count;
        }
    } else {
        for (int i = 0; nanox_help_sheet[i] != NULL; ++i)
            ++total;
    }
    return total;
}

void nanox_help_render(void)
{
    if (!help_active)
        return;

    int max_r = nanox_hint_top_row() - 1;

    /* Clear display area */
    TTsetcolors(-1, -1);
    for (int r = 0; r <= max_r; ++r) {
        movecursor(r, 0);
        for (int c = 0; c < term->t_ncol; ++c) help_puts(" ");
    }

    /* Render Sheet */
    movecursor(0, 0);
    help_puts(" [ Nanox Help Sheet ]");

    int content_rows = max_r - 2;
    if (content_rows < 0)
        content_rows = 0;
    int total_lines = help_total_lines();
    int max_scroll = total_lines - content_rows;
    if (max_scroll < 0)
        max_scroll = 0;
    if (help_scroll < 0)
        help_scroll = 0;
    if (help_scroll > max_scroll)
        help_scroll = max_scroll;
    int skip = help_scroll;
    int rendered = 0;
    int r = 2;

    /* Prefer dynamic topics if loaded */
    if (dynamic_topics && dynamic_topic_count > 0) {
        for (size_t t = 0; t < dynamic_topic_count && r < max_r; t++) {
            struct nanox_help_topic *topic = &dynamic_topics[t];
            if (!topic)
                continue;
            if (help_topic_has_title(topic)) {
                if (skip > 0) {
                    --skip;
                } else if (rendered < content_rows) {
                    movecursor(r++, 2);
                    help_puts("=> ");
                    int avail = term->t_ncol - 2 - 3;
                    if (avail > 0)
                        help_puts_width(topic->title, avail);
                    ++rendered;
                }
            }
            for (size_t l = 0; l < topic->line_count && r < max_r; l++) {
                if (skip > 0) {
                    --skip;
                    continue;
                }
                if (rendered >= content_rows)
                    break;
                movecursor(r++, 2);
                help_puts_width(topic->lines[l], term->t_ncol - 2);
                ++rendered;
            }
            if (rendered >= content_rows)
                break;
        }
    } else {
        int avail = term->t_ncol - 2;
        for (int i = 0; nanox_help_sheet[i] != NULL && r < max_r; ++i) {
            if (skip > 0) {
                --skip;
                continue;
            }
            if (rendered >= content_rows)
                break;
            movecursor(r++, 2);
            help_puts_width(nanox_help_sheet[i], avail);
            ++rendered;
        }
    }

    /* Footer */
    movecursor(max_r, 0);
    for (int i = 0; i < term->t_ncol; i++) help_puts("-");
    movecursor(max_r, 2);
    help_puts_width(" Press F1, ESC or Backspace to Exit Help ", term->t_ncol - 2);

    TTsetcolors(-1, -1);
    ttflush();
}
int nanox_traditional_help_command(int f, int n)
{
    load_help_file();
    help_active = true;
    help_scroll = 0;
    sgarbf = TRUE;
    nanox_help_render();
    return TRUE;
}

void help_close(void)
{
    help_active = false;
    sgarbf = TRUE;
    update(TRUE);
}

int nanox_help_handle_key(int key)
{
    if (!help_active)
        return FALSE;

    /* Normalize key for exit (F1, ESC, Backspace, ^H, ^G) */
    int base_key = key;
    if (key & SPEC) {
        base_key = SPEC | (key & 0xFF);
    }

    switch (base_key) {
    case CONTROL | 'G':
    case CONTROL | '[':
    case 27:
    case SPEC | 'P': /* F1 */
    case 0x7F:       /* Backspace */
    case CONTROL | 'H':
        help_close();
        return TRUE;
    case SPEC | 'A': /* Up Arrow */
        if (help_scroll > 0)
            --help_scroll;
        nanox_help_render();
        return TRUE;
    case SPEC | 'B': /* Down Arrow */
        ++help_scroll;
        nanox_help_render();
        return TRUE;
    default:
        /* Any other key also closes help or just ignore */
        break;
    }
    return TRUE;
}

int nanox_slot_capacity(void)
{
    return nanox_cfg.no_function_slot ? NANOX_SLOT_MAX : 4;
}

static const char *slot_name(int slot)
{
    static char label[32];
    static const char *names[] = { "F9", "F10", "F11", "F12" };

    if (slot < 0 || slot >= nanox_slot_capacity())
        return "?";
    if (!nanox_cfg.no_function_slot && slot < 4)
        return names[slot];
    snprintf(label, sizeof(label), "slot %d", slot + 1);
    return label;
}

static void seed_startup_slots(void)
{
    int max_slots = nanox_slot_capacity();

    /* Stop when we run out of visible slots or queued startup files. */
    for (int i = 0; i < max_slots && startup_slot_queue_next < startup_slot_queue_count; ++i) {
        if (file_reserve[i][0])
            continue;
        mystrscpy(file_reserve[i], startup_slot_queue[startup_slot_queue_next++], sizeof(file_reserve[i]));
    }
}

void nanox_queue_startup_file(const char *path)
{
    char **new_queue;
    char *copy;

    if (!path || !*path)
        return;

    if (startup_slot_queue_count == startup_slot_queue_cap) {
        size_t new_cap = startup_slot_queue_cap ? startup_slot_queue_cap * 2 : 32;
        new_queue = realloc(startup_slot_queue, sizeof(char *) * new_cap);
        if (!new_queue)
            return;
        startup_slot_queue = new_queue;
        startup_slot_queue_cap = new_cap;
    }

    copy = malloc(strlen(path) + 1);
    if (!copy)
        return;
    strcpy(copy, path);
    startup_slot_queue[startup_slot_queue_count++] = copy;
}

size_t nanox_startup_file_count(void)
{
    return startup_slot_queue_count;
}

const char *nanox_startup_file_at(size_t index)
{
    if (index >= startup_slot_queue_count)
        return NULL;
    return startup_slot_queue[index];
}

void nanox_handle_closed_file(const char *path)
{
    int max_slots = nanox_slot_capacity();

    if (!path || !*path)
        return;

    for (int i = 0; i < max_slots; ++i) {
        if (strcmp(file_reserve[i], path) != 0)
            continue;
        file_reserve[i][0] = '\0';
        if (startup_slot_queue_next < startup_slot_queue_count) {
            mystrscpy(file_reserve[i], startup_slot_queue[startup_slot_queue_next++], sizeof(file_reserve[i]));
        }
        break;
    }
}

static int reserve_set(int slot)
{
    char prompt[64];
    char path[PATH_MAX];
    char msg[PATH_MAX + 64];
    int rc;

    if (slot < 0 || slot >= nanox_slot_capacity()) return FALSE;

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

    if (slot < 0 || slot >= nanox_slot_capacity()) return FALSE;

    if (!file_reserve[slot][0]) {
        nanox_set_lamp(NANOX_LAMP_WARN);
        snprintf(msg, sizeof(msg), "%s is empty (load files with nx ... or reserve it first)", slot_name(slot));
        minibuf_show(msg);
        return FALSE;
    }
    rc = getfile(file_reserve[slot], TRUE);
    if (rc == TRUE) {
        snprintf(msg, sizeof(msg), "Jump %s -> %s", slot_name(slot), file_reserve[slot]);
        minibuf_show(msg);
        nanox_set_lamp(NANOX_LAMP_OFF);
        last_slot_index = slot;
    } else {
        nanox_set_lamp(NANOX_LAMP_ERROR);
    }
    return rc;
}

int nanox_open_startup_slot(void)
{
    seed_startup_slots();
    if (!file_reserve[0][0])
        return FALSE;
    return reserve_jump(0);
}

int reserve_jump_numeric_mode(int f, int n)
{
    char buf[16];
    char prompt[32];
    int slot;
    int max_slots = nanox_slot_capacity();

    if (!nanox_cfg.no_function_slot)
        return FALSE;
    snprintf(prompt, sizeof(prompt), "Open slot (1-%d): ", max_slots);
    if (minibuf_input(prompt, buf, sizeof(buf)) != TRUE)
        return FALSE;
    slot = atoi(buf);
    if (slot < 1 || slot > max_slots) {
        mlwrite("Slot must be between 1 and %d", max_slots);
        return FALSE;
    }
    return reserve_jump(slot - 1);
}

int reserve_set_1(int f, int n) { return reserve_set(0); }
int reserve_set_2(int f, int n) { return reserve_set(1); }
int reserve_set_3(int f, int n) { return reserve_set(2); }
int reserve_set_4(int f, int n) { return reserve_set(3); }

int reserve_jump_1(int f, int n) { return reserve_jump(0); }
int reserve_jump_2(int f, int n) { return reserve_jump(1); }
int reserve_jump_3(int f, int n) { return reserve_jump(2); }
int reserve_jump_4(int f, int n) { return reserve_jump(3); }
int reserve_jump_fallback_1(int f, int n) { return nanox_cfg.no_function_slot ? reserve_jump_numeric_mode(f, n) : reserve_jump(0); }
int reserve_jump_fallback_2(int f, int n) { return nanox_cfg.no_function_slot ? reserve_jump_numeric_mode(f, n) : reserve_jump(1); }
int reserve_jump_fallback_3(int f, int n) { return nanox_cfg.no_function_slot ? reserve_jump_numeric_mode(f, n) : reserve_jump(2); }
int reserve_jump_fallback_4(int f, int n) { return nanox_cfg.no_function_slot ? reserve_jump_numeric_mode(f, n) : reserve_jump(3); }

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
    if (startup_slot_queue) {
        for (size_t i = 0; i < startup_slot_queue_count; ++i)
            free(startup_slot_queue[i]);
        free(startup_slot_queue);
        startup_slot_queue = NULL;
        startup_slot_queue_count = 0;
        startup_slot_queue_cap = 0;
        startup_slot_queue_next = 0;
    }
}

#include <dirent.h>
#include <sys/stat.h>

bool file_tree_active = false;
int file_tree_width = 25;
char file_tree_workspace_root[PATH_MAX] = "";
bool file_tree_interactive = false;

typedef struct FileNode {
    char name[256];
    char path[PATH_MAX];
    bool is_dir;
    bool is_open;
    int depth;
    struct FileNode *parent;
} FileNode;

#define MAX_TREE_NODES 1024
static FileNode file_tree_nodes[MAX_TREE_NODES];
static int file_tree_node_count = 0;
static int file_tree_scroll = 0;
static int file_tree_selected = 0;

static void detect_workspace_root(char *root_out, size_t sz)
{
    char dir[PATH_MAX];
    if (file_tree_workspace_root[0] != '\0') {
        mystrscpy(root_out, file_tree_workspace_root, sz);
        return;
    }
    if (curbp->b_fname && curbp->b_fname[0]) {
        char abs_path[PATH_MAX];
        if (realpath(curbp->b_fname, abs_path)) {
            char *last_slash = strrchr(abs_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                strcpy(dir, abs_path);
            } else {
                if (getcwd(dir, sizeof(dir)) == NULL) dir[0] = '\0';
            }
        } else {
            char *last_slash = strrchr(curbp->b_fname, '/');
            if (last_slash) {
                size_t len = last_slash - curbp->b_fname;
                if (len >= sizeof(dir)) len = sizeof(dir) - 1;
                memcpy(dir, curbp->b_fname, len);
                dir[len] = '\0';
            } else {
                if (getcwd(dir, sizeof(dir)) == NULL) dir[0] = '\0';
            }
        }
    } else {
        if (getcwd(dir, sizeof(dir)) == NULL) dir[0] = '\0';
    }

    char last_dir[PATH_MAX] = "";
    char check_path[PATH_MAX + 32];
    char workspace[PATH_MAX] = "";
    strcpy(workspace, dir);

    while (strcmp(dir, last_dir) != 0) {
        strcpy(last_dir, dir);

        snprintf(check_path, sizeof(check_path), "%s/.git", dir);
        if (access(check_path, F_OK) == 0) {
            strcpy(workspace, dir);
            break;
        }
        snprintf(check_path, sizeof(check_path), "%s/Cargo.toml", dir);
        if (access(check_path, F_OK) == 0) {
            strcpy(workspace, dir);
            break;
        }
        snprintf(check_path, sizeof(check_path), "%s/Makefile", dir);
        if (access(check_path, F_OK) == 0) {
            strcpy(workspace, dir);
            break;
        }

        char *last_slash = strrchr(dir, '/');
        if (last_slash) {
            if (last_slash == dir) {
                strcpy(dir, "/");
            } else {
                *last_slash = '\0';
            }
        } else {
            break;
        }
    }
    mystrscpy(root_out, workspace, sz);
    mystrscpy(file_tree_workspace_root, workspace, sizeof(file_tree_workspace_root));
}

static void file_tree_add_node(const char *name, const char *path, bool is_dir, int depth, FileNode *parent, int insert_pos)
{
    if (file_tree_node_count >= MAX_TREE_NODES)
        return;
    for (int i = file_tree_node_count; i > insert_pos; i--) {
        file_tree_nodes[i] = file_tree_nodes[i - 1];
    }
    FileNode *node = &file_tree_nodes[insert_pos];
    mystrscpy(node->name, name, sizeof(node->name));
    mystrscpy(node->path, path, sizeof(node->path));
    node->is_dir = is_dir;
    node->is_open = false;
    node->depth = depth;
    node->parent = parent;
    file_tree_node_count++;
}

static int compare_nodes(const void *a, const void *b) {
    const FileNode *na = (const FileNode *)a;
    const FileNode *nb = (const FileNode *)b;
    if (na->is_dir != nb->is_dir) {
        return na->is_dir ? -1 : 1;
    }
    return strcasecmp(na->name, nb->name);
}

void file_tree_expand_dir(int index)
{
    FileNode *parent = &file_tree_nodes[index];
    if (!parent->is_dir || parent->is_open)
        return;

    DIR *dir = opendir(parent->path);
    if (!dir)
        return;

    parent->is_open = true;
    FileNode children[256];
    int child_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (entry->d_name[0] == '.')
            continue;

        char child_path[PATH_MAX];
        snprintf(child_path, sizeof(child_path), "%s/%s", parent->path, entry->d_name);

        struct stat st;
        bool is_directory = false;
        if (stat(child_path, &st) == 0) {
            is_directory = S_ISDIR(st.st_mode);
        }

        if (child_count < 256) {
            mystrscpy(children[child_count].name, entry->d_name, sizeof(children[child_count].name));
            mystrscpy(children[child_count].path, child_path, sizeof(children[child_count].path));
            children[child_count].is_dir = is_directory;
            children[child_count].is_open = false;
            children[child_count].depth = parent->depth + 1;
            children[child_count].parent = parent;
            child_count++;
        }
    }
    closedir(dir);

    qsort(children, child_count, sizeof(FileNode), compare_nodes);

    for (int i = 0; i < child_count; i++) {
        file_tree_add_node(children[i].name, children[i].path, children[i].is_dir, children[i].depth, parent, index + 1 + i);
    }
}

void file_tree_collapse_dir(int index)
{
    FileNode *parent = &file_tree_nodes[index];
    if (!parent->is_dir || !parent->is_open)
        return;

    parent->is_open = false;
    int next = index + 1;
    int remove_count = 0;
    while (next < file_tree_node_count && file_tree_nodes[next].depth > parent->depth) {
        remove_count++;
        next++;
    }

    if (remove_count > 0) {
        for (int i = index + 1; i < file_tree_node_count - remove_count; i++) {
            file_tree_nodes[i] = file_tree_nodes[i + remove_count];
        }
        file_tree_node_count -= remove_count;
    }
}

void file_tree_init_workspace(void)
{
    char root[PATH_MAX];
    detect_workspace_root(root, sizeof(root));

    file_tree_node_count = 0;
    file_tree_selected = 0;
    file_tree_scroll = 0;

    DIR *dir = opendir(root);
    if (!dir)
        return;

    FileNode children[256];
    int child_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (entry->d_name[0] == '.')
            continue;

        char child_path[PATH_MAX];
        snprintf(child_path, sizeof(child_path), "%s/%s", root, entry->d_name);

        struct stat st;
        bool is_directory = false;
        if (stat(child_path, &st) == 0) {
            is_directory = S_ISDIR(st.st_mode);
        }

        if (child_count < 256) {
            mystrscpy(children[child_count].name, entry->d_name, sizeof(children[child_count].name));
            mystrscpy(children[child_count].path, child_path, sizeof(children[child_count].path));
            children[child_count].is_dir = is_directory;
            children[child_count].is_open = false;
            children[child_count].depth = 0;
            children[child_count].parent = NULL;
            child_count++;
        }
    }
    closedir(dir);

    qsort(children, child_count, sizeof(FileNode), compare_nodes);

    for (int i = 0; i < child_count; i++) {
        file_tree_add_node(children[i].name, children[i].path, children[i].is_dir, children[i].depth, NULL, i);
    }
}

void file_tree_draw(void)
{
    if (!file_tree_active)
        return;

    int visible_rows = term->t_nrow - 2;
    int count = file_tree_node_count;

    HighlightStyle normal = colorscheme_get(HL_NORMAL);
    HighlightStyle comment = colorscheme_get(HL_COMMENT);
    HighlightStyle keyword = colorscheme_get(HL_KEYWORD);

    for (int r = 0; r < visible_rows && r < term->t_nrow; r++) {
        struct video *vp = vscreen[r];
        for (int c = 0; c < file_tree_width; c++) {
            vp->v_text[c].ch = ' ';
            vp->v_text[c].fg = normal.fg;
            vp->v_text[c].bg = normal.bg;
            vp->v_text[c].bold = false;
            vp->v_text[c].underline = false;
            vp->v_text[c].italic = false;
        }

        int idx = file_tree_scroll + r;
        if (idx < count) {
            FileNode *node = &file_tree_nodes[idx];
            int indent = node->depth * 2;
            int col = 0;

            while (col < indent && col < file_tree_width - 2) {
                vp->v_text[col].ch = ' ';
                col++;
            }

            const char *indicator = "";
            if (node->is_dir) {
                indicator = node->is_open ? "▼ " : "▶ ";
            } else {
                indicator = "  ";
            }

            int i = 0;
            while (indicator[i] && col < file_tree_width - 2) {
                unicode_t uc;
                int bytes = utf8_to_unicode((unsigned char *)indicator, i, strlen(indicator), &uc);
                if (bytes <= 0) break;
                vp->v_text[col].ch = uc;
                if (node->is_dir) {
                    vp->v_text[col].fg = keyword.fg;
                } else {
                    vp->v_text[col].fg = comment.fg;
                }
                col++;
                i += bytes;
            }

            if (node->is_dir) {
                const char *ficon = "📁 ";
                int fi = 0;
                while (ficon[fi] && col < file_tree_width - 2) {
                    unicode_t uc;
                    int bytes = utf8_to_unicode((unsigned char *)ficon, fi, strlen(ficon), &uc);
                    if (bytes <= 0) break;
                    vp->v_text[col].ch = uc;
                    vp->v_text[col].fg = keyword.fg;
                    col++;
                    fi += bytes;
                }
            } else {
                const char *ficon = "📄 ";
                int fi = 0;
                while (ficon[fi] && col < file_tree_width - 2) {
                    unicode_t uc;
                    int bytes = utf8_to_unicode((unsigned char *)ficon, fi, strlen(ficon), &uc);
                    if (bytes <= 0) break;
                    vp->v_text[col].ch = uc;
                    vp->v_text[col].fg = comment.fg;
                    col++;
                    fi += bytes;
                }
            }

            i = 0;
            int name_len = strlen(node->name);
            while (i < name_len && col < file_tree_width - 2) {
                unicode_t uc;
                int bytes = utf8_to_unicode((unsigned char *)node->name, i, name_len, &uc);
                if (bytes <= 0) break;
                vp->v_text[col].ch = uc;
                if (idx == file_tree_selected) {
                    vp->v_text[col].bg = keyword.bg != -1 ? keyword.bg : normal.fg;
                    vp->v_text[col].fg = keyword.bg != -1 ? keyword.fg : normal.bg;
                    vp->v_text[col].bold = true;
                } else {
                    vp->v_text[col].fg = node->is_dir ? keyword.fg : normal.fg;
                }
                col++;
                i += bytes;
            }
        }

        vp->v_text[file_tree_width - 1].ch = 0x2502;
        vp->v_text[file_tree_width - 1].fg = comment.fg;
        vp->v_text[file_tree_width - 1].bg = normal.bg;
        vp->v_flag |= VFCHG;
    }
}

void command_mode_run_file_tree(void) {
    if (!file_tree_active) {
        mlwrite("File Tree is not open. Run openFileTree first.");
        return;
    }

    file_tree_interactive = true;
    sgarbf = TRUE;
    update(TRUE);

    while (file_tree_interactive) {
        mlwrite("File Tree Mode (Esc: exit, Arrows: select, Enter: open/toggle, Shift+BS: go up)");
        int c = getcmd();
        
        if (c == 0x1B) {
            file_tree_interactive = false;
            break;
        }

        switch (c) {
        case (SPEC | 'A'):
            if (file_tree_selected > 0) {
                file_tree_selected--;
                if (file_tree_selected < file_tree_scroll)
                    file_tree_scroll = file_tree_selected;
            }
            break;

        case (SPEC | 'B'):
            if (file_tree_selected < file_tree_node_count - 1) {
                file_tree_selected++;
                int visible_rows = term->t_nrow - 3;
                if (file_tree_selected >= file_tree_scroll + visible_rows)
                    file_tree_scroll = file_tree_selected - visible_rows + 1;
            }
            break;

        case '\n':
        case '\r':
        case (CONTROL | 'M'):
            if (file_tree_selected >= 0 && file_tree_selected < file_tree_node_count) {
                FileNode *node = &file_tree_nodes[file_tree_selected];
                if (node->is_dir) {
                    if (node->is_open) {
                        file_tree_collapse_dir(file_tree_selected);
                    } else {
                        file_tree_expand_dir(file_tree_selected);
                    }
                } else {
                    struct buffer *bp;
                    extern struct buffer *bheadp;
                    bp = bheadp;
                    bool found_buf = false;
                    while (bp != NULL) {
                        if (strcmp(bp->b_fname, node->path) == 0) {
                            swbuffer(bp);
                            found_buf = true;
                            break;
                        }
                        bp = bp->b_bufp;
                    }
                    if (!found_buf) {
                        getfile(node->path, TRUE);
                    }
                    file_tree_interactive = false;
                }
            }
            break;

        case (SHIFT | 0x08):
        case (SHIFT | 0x7f):
        case (SHIFT | (CONTROL | 'H')):
        case (SPEC | '\t'):
        case (SHIFT | 0x09):
            if (file_tree_selected >= 0 && file_tree_selected < file_tree_node_count) {
                FileNode *node = &file_tree_nodes[file_tree_selected];
                if (node->parent != NULL) {
                    for (int i = 0; i < file_tree_node_count; i++) {
                        if (&file_tree_nodes[i] == node->parent) {
                            file_tree_selected = i;
                            file_tree_collapse_dir(i);
                            if (file_tree_selected < file_tree_scroll)
                                file_tree_scroll = file_tree_selected;
                            break;
                        }
                    }
                } else {
                    char cur_root[PATH_MAX];
                    detect_workspace_root(cur_root, sizeof(cur_root));
                    char parent_root[PATH_MAX];
                    char *last_slash = strrchr(cur_root, '/');
                    if (last_slash) {
                        if (last_slash == cur_root) {
                            strcpy(parent_root, "/");
                        } else {
                            *last_slash = '\0';
                            strcpy(parent_root, cur_root);
                        }
                        mystrscpy(file_tree_workspace_root, parent_root, sizeof(file_tree_workspace_root));
                        file_tree_init_workspace();
                    }
                }
            }
            break;
        }

        sgarbf = TRUE;
        update(TRUE);
    }

    mlwrite("Exited File Tree Mode.");
}

typedef struct {
    const char *name;
    const char *lines[30];
    int line_count;
} HelpCategory;

static HelpCategory help_categories[] = {
    {
        "1. Basic & Editing",
        {
            "Basic Editor Commands:",
            "",
            "  Ctrl+S / F2     - Save current file",
            "  Ctrl+O / F3     - Open/Find file",
            "  Ctrl+Q / F4     - Quit nanox",
            "  Ctrl+F / F5     - Find / Search engine",
            "  Ctrl+V          - Open Command Mode",
            "  Ctrl+Space      - Trigger Autocomplete",
            "  Paste preview   - Press p to insert, Esc/Ctrl+G/BS to cancel",
            "",
            "Use F1 / Ctrl+H at any time to open this menu.",
            "Use Command Mode (help or h) for the full reference manual."
        },
        12
    },
    {
        "2. LSP & Autocomplete",
        {
            "LSP & Keyword Autocompletion:",
            "",
            "  - Automatically queries active Language Server Protocols.",
            "  - Sorts candidates using VSCode/Vim-style semantic priority:",
            "    * Exact matches & prefix matches are boosted to the top.",
            "    * Kinds (Snippets, Keywords, Functions) get a higher score.",
            "    * Selection history (MRU) tracks usage frequency for smart ordering.",
            "  - Integrates language-specific keywords (Fortran, Rust, Ada, etc.).",
            "",
            "  * NOTE: Seamlessly integrates with the Workspace File Tree",
            "    (type openFileTree in Command Mode to view files)."
        },
        11
    },
    {
        "3. Workspace File Tree",
        {
            "VSCode-Style File Tree Viewer:",
            "",
            "  - openFileTree  - Open visual left panel tree view",
            "  - closeFileTree - Close left panel tree view",
            "  - runFileTree   - Activate interactive navigation mode",
            "    * Up/Down Arrow - Select file/directory",
            "    * Enter         - Expand/collapse directory or open file",
            "    * Shift+BackSp  - Go up to parent directory/level",
            "",
            "  * NOTE: Combines workspace-wide navigation with smart LSP",
            "    completion priority sorting to achieve a full IDE experience."
        },
        11
    },
    {
        "4. Fast Build System",
        {
            "Command Mode Fast Build:",
            "",
            "  - Type build in Command Mode.",
            "  - Searches upward for project build configurations:",
            "    * Cargo.toml   -> runs 'cargo build'",
            "    * Makefile     -> runs 'make'",
            "    * CMakeLists.txt -> runs 'cmake --build build' or cmake -B",
            "  - Fallback: Compiles single source file directly via",
            "    gcc (C), g++ (C++), rustc (Rust), go (Go), or gfortran (Fortran)."
        },
        9
    },
    {
        "5. Multi-Cursor & Viblock",
        {
            "Advanced Editing Features:",
            "",
            "  - Multi-Cursor Commands:",
            "    * cursor create <n> - Spawn <n> cursors",
            "    * cursor select <n> - Switch cursor focus",
            "    * cursor single     - Return to single cursor",
            "  - Viblock (Visual Block Select) Commands:",
            "    * viblock-edit      - Start block editing",
            "    * viblock-replace   - Start block replace"
        },
        9
    },
    {
        "6. AI Copilot",
        {
            "Ollama AI Copilot completions:",
            "",
            "  - Set options under [ai] in ~/.config/nanox/config:",
            "    * enabled=true      - Enable Copilot",
            "    * model=name        - Ollama model (e.g. qwen2.5-coder:1.5b)",
            "    * endpoint=url      - Ollama API url (http://localhost:11434/api/generate)",
            "    * temperature=0.2   - Generation temperature",
            "  - Usage:",
            "    * Ctrl+Alt+A or ai - Request completion recommendation",
            "    * Press 'y'         - Accept & keep the suggestion",
            "    * Press 'n'/other   - Reject & revert suggestion"
        },
        11
    }
};

#define HELP_CAT_COUNT (sizeof(help_categories) / sizeof(help_categories[0]))
static int help_selected_cat = 0;
bool interactive_help_active = false;

void draw_interactive_help(void)
{
    if (!term || !vscreen || term->t_nrow <= 0 || term->t_ncol <= 0)
        return;

    HighlightStyle normal = colorscheme_get(HL_NORMAL);
    HighlightStyle comment = colorscheme_get(HL_COMMENT);
    HighlightStyle keyword = colorscheme_get(HL_KEYWORD);

    int max_r = term->t_nrow - 2;
    if (max_r < 0)
        return;

    for (int r = 0; r <= max_r && r < term->t_nrow; r++) {
        struct video *vp = vscreen[r];
        if (!vp)
            return;
        for (int c = 0; c < term->t_ncol; c++) {
            vp->v_text[c].ch = ' ';
            vp->v_text[c].fg = normal.fg;
            vp->v_text[c].bg = normal.bg;
            vp->v_text[c].bold = false;
            vp->v_text[c].underline = false;
            vp->v_text[c].italic = false;
        }
        vp->v_flag |= VFCHG;
    }

    struct video *hdr = vscreen[0];
    if (!hdr)
        return;
    const char *header = " [ Nanox Interactive Help System ]";
    int col = 0;
    while (header[col] && col < term->t_ncol) {
        hdr->v_text[col].ch = header[col];
        hdr->v_text[col].bold = true;
        hdr->v_text[col].fg = keyword.fg;
        col++;
    }

    int separator_col = 26;
    if (separator_col >= term->t_ncol)
        separator_col = term->t_ncol / 3;
    if (separator_col < 8)
        separator_col = 8;
    if (separator_col >= term->t_ncol)
        separator_col = term->t_ncol - 1;

    for (int r = 2; r < max_r && r < term->t_nrow; r++) {
        if (!vscreen[r])
            return;
        vscreen[r]->v_text[separator_col].ch = 0x2502;
        vscreen[r]->v_text[separator_col].fg = comment.fg;
    }

    for (int i = 0; i < HELP_CAT_COUNT; i++) {
        int row_idx = 2 + i * 2;
        if (row_idx >= max_r) break;
        struct video *vp = vscreen[row_idx];
        if (!vp)
            return;
        const char *name = help_categories[i].name;
        int c = 2;
        while (name[c - 2] && c < separator_col - 1) {
            vp->v_text[c].ch = name[c - 2];
            if (i == help_selected_cat) {
                vp->v_text[c].bg = keyword.bg != -1 ? keyword.bg : normal.fg;
                vp->v_text[c].fg = keyword.bg != -1 ? keyword.fg : normal.bg;
                vp->v_text[c].bold = true;
            } else {
                vp->v_text[c].fg = normal.fg;
            }
            c++;
        }
    }

    HelpCategory *cat = &help_categories[help_selected_cat];
    for (int l = 0; l < cat->line_count && l < max_r - 2; l++) {
        int row_idx = 2 + l;
        if (row_idx >= max_r) break;
        struct video *vp = vscreen[row_idx];
        if (!vp)
            return;
        const char *line = cat->lines[l];
        if (!line) break;
        int c = separator_col + 2;
        int idx = 0;
        int len = (int)strlen(line);
        while (line[idx] && c < term->t_ncol - 2) {
            unicode_t uc;
            int bytes = utf8_to_unicode((unsigned char *)line, idx, len, &uc);
            if (bytes <= 0 || idx + bytes > len) {
                uc = (unsigned char)line[idx];
                bytes = 1;
            }
            vp->v_text[c].ch = uc;
            vp->v_text[c].fg = (l == 0) ? keyword.fg : normal.fg;
            vp->v_text[c].bold = (l == 0);
            c++;
            idx += bytes;
        }
    }

    struct video *ftr = vscreen[max_r];
    if (!ftr)
        return;
    for (int c = 0; c < term->t_ncol; c++) {
        ftr->v_text[c].ch = '-';
        ftr->v_text[c].fg = comment.fg;
    }
    const char *footer = " [ Up/Down: Navigate | Esc/F1: Exit Help ]";
    col = 2;
    while (footer[col - 2] && col < term->t_ncol - 2) {
        ftr->v_text[col].ch = footer[col - 2];
        ftr->v_text[col].bold = true;
        ftr->v_text[col].fg = comment.fg;
        col++;
    }
}

int nanox_help_command(int f, int n)
{
    interactive_help_active = true;
    help_selected_cat = 0;
    sgarbf = TRUE;
    update(TRUE);

    while (interactive_help_active) {
        mlwrite("Interactive Help Menu (Esc/F1: Exit, Up/Down: Select)");
        int c = getcmd();

        int base_key = c;
        if (c & SPEC) {
            base_key = SPEC | (c & 0xFF);
        }

        switch (base_key) {
        case CONTROL | 'G':
        case CONTROL | '[':
        case 27:
        case SPEC | 'P':
        case 0x7F:
        case CONTROL | 'H':
            interactive_help_active = false;
            break;
        case SPEC | 'A':
            help_selected_cat = (help_selected_cat - 1 + (int)HELP_CAT_COUNT) % (int)HELP_CAT_COUNT;
            break;
        case SPEC | 'B':
            help_selected_cat = (help_selected_cat + 1) % (int)HELP_CAT_COUNT;
            break;
        default:
            break;
        }

        sgarbf = TRUE;
        update(TRUE);
    }

    mlwrite("");
    sgarbf = TRUE;
    update(TRUE);
    return TRUE;
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

    /* Check for 'p' or 'P' key - insert paste */
    if (c == 'p' || c == 'P') {
        /* Insert the paste slot content */
        paste_slot_insert();
        action_taken = TRUE;
    }
    /* ESC key, Ctrl+G, or Backspace - cancel */
    else if (c == (CONTROL | '[') || (c & 0xFF) == 27 || c == (CONTROL | 'G') || c == 0x7F) {
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
    mlwrite("Press 'p' to paste, ESC/Ctrl+G/BS to cancel");
    return TRUE;
}

int check_paste_slot_active(void)
{
    extern int paste_slot_is_active(void);
    return paste_slot_is_active();
}
