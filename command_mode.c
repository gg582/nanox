/* command_mode.c - F1 command mode implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "command_mode.h"
#include "utf8.h"
#include "util.h"
#include "colorscheme.h"

#define CMD_BUF_SIZE 256
#define REPLACE_PREVIEW 48

static int cmd_active = 0;
static char cmd_buffer[CMD_BUF_SIZE];
static int cmd_pos = 0;
static int block_active = 0;
static int block_replace = 0;
static struct line *block_anchor_line = NULL;
static int block_anchor_offset = 0;

static int parse_sed_expression(const char *expr, char *pattern, size_t pat_sz,
    char *replacement, size_t rep_sz, int *is_global, int *is_caseless);
static int apply_regex_to_line(struct line *lp, pcre2_code *code, pcre2_match_data *match_data,
    const char *replacement, size_t repl_len, int is_global, int *total_count);
static size_t utf8_advance(const char *text, size_t len, size_t offset);
static void build_preview(const char *text, size_t len, char *dest, size_t dest_sz);
static char *splice_text(char *text, size_t text_len, size_t start, size_t end,
    const char *replacement, size_t repl_len, size_t *new_len);
static int line_index_from_top(struct line *target);
static void restore_cursor_to_index(int index, int offset);
static int block_apply_text(const char *text, int replace_mode);
static int block_visual_column(struct line *lp, int offset);
static void block_bounds(int *top, int *bottom, int *left, int *right);
static int line_offset_for_column(struct line *lp, int target_col, int *actual_col);
static void render_block_status(void);

static void command_mode_write_segment(const char *text, const HighlightStyle *style, int *col)
{
    if (!text || !*text || !style || !col || !term)
        return;

    TTsetcolors(style->fg, style->bg);
    TTsetattrs(style->bold, style->underline, style->italic);

    const unsigned char *bytes = (const unsigned char *)text;
    int len = (int)strlen(text);
    int idx = 0;

    while (idx < len && *col < term->t_ncol) {
        unicode_t uc;
        int consumed = utf8_to_unicode((unsigned char *)bytes, idx, len, &uc);
        if (consumed <= 0)
            break;
        int width = mystrnlen_raw_w(uc);
        if (*col + width > term->t_ncol)
            break;
        TTputc(uc);
        *col += width;
        idx += consumed;
    }
}

static void command_mode_draw_status(const char *status, const char *input, bool show_cursor)
{
    if (!discmd || !term)
        return;

    HighlightStyle normal = colorscheme_get(HL_NORMAL);
    HighlightStyle label = colorscheme_get(HL_NOTICE);
    HighlightStyle status_style = colorscheme_get(HL_HEADER);
    HighlightStyle input_style = colorscheme_get(HL_FUNCTION);

    movecursor(term->t_nrow, 0);
    int col = 0;

    command_mode_write_segment("F1 ", &label, &col);
    command_mode_write_segment("Command ", &label, &col);

    if (status && *status)
        command_mode_write_segment(status, &status_style, &col);

    if (input && *input) {
        if (status && *status)
            command_mode_write_segment(" ", &status_style, &col);
        command_mode_write_segment(input, &input_style, &col);
    }

    if (show_cursor)
        command_mode_write_segment("_", &input_style, &col);

    TTsetcolors(normal.fg, normal.bg);
    TTsetattrs(normal.bold, normal.underline, normal.italic);
    while (col < term->t_ncol) {
        TTputc(' ');
        col++;
    }

    TTsetcolors(-1, -1);
    TTsetattrs(0, 0, 0);
    TTflush();
    mpresf = TRUE;
}

/* Initialize command mode system */
void command_mode_init(void) {
    cmd_active = 0;
    cmd_buffer[0] = '\0';
    cmd_pos = 0;
    block_active = 0;
    block_replace = 0;
    block_anchor_line = NULL;
    block_anchor_offset = 0;
}

/* Check if command mode is active */
int command_mode_is_active(void) {
    return cmd_active;
}

/* Activate F1 command mode */
void command_mode_activate(void) {
    cmd_active = 1;
    cmd_buffer[0] = '\0';
    cmd_pos = 0;
    
    /* Mark current window for full redisplay to synchronize display state */
    curwp->w_flag |= WFHARD | WFMODE;
    
    /* Force complete screen reset and redraw */
    sgarbf = TRUE;  /* Mark screen as garbage - forces atomic update */
    update(TRUE);   /* Full screen redraw - ensures buffer doesn't preempt message */
    
    /* Show command prompt in status bar with themed colors */
    command_mode_draw_status("[number] goto line | help | viblock-edit | viblock-replace", NULL, false);
    
    /* DO NOT call update() here - let the main loop handle it to prevent preemption */
}

/* Execute goto line command */
static void execute_goto_line(int line_num) {
    int total_lines = 0;
    struct line *lp;
    
    /* Count total lines in buffer */
    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        total_lines++;
    }
    
    /* Clamp to valid range */
    if (line_num < 1) line_num = 1;
    if (line_num > total_lines) line_num = total_lines;
    
    /* Go to the line */
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;
    
    for (int i = 1; i < line_num && lforw(curwp->w_dotp) != curbp->b_linep; i++) {
        curwp->w_dotp = lforw(curwp->w_dotp);
    }
    
    curwp->w_flag |= WFMOVE;
    mlwrite("Line %d of %d", line_num, total_lines);
}

/* Execute help command */
static void execute_help(void) {
    /* Force complete screen reset before showing help */
    sgarbf = TRUE;
    update(TRUE);
    
    /* Call existing help function */
    nanox_help_command(FALSE, 1);
}

static void start_block_mode(int replace_mode)
{
    block_active = 1;
    block_replace = replace_mode;
    block_anchor_line = curwp->w_dotp;
    block_anchor_offset = curwp->w_doto;
    curwp->w_flag |= WFHARD | WFMODE;
    render_block_status();
}

/* Parse and execute command */
static void execute_command(void) {
    if (cmd_pos == 0) {
        mlwrite("Empty command");
        return;
    }
    
    cmd_buffer[cmd_pos] = '\0';
    
    /* Check if it's a number (goto line) */
    int is_number = 1;
    for (int i = 0; i < cmd_pos; i++) {
        if (!isdigit((unsigned char)cmd_buffer[i])) {
            is_number = 0;
            break;
        }
    }
    
    if (is_number) {
        int line_num = atoi(cmd_buffer);
        execute_goto_line(line_num);
    }
    /* Check for Help command (case insensitive) */
    else if (strcasecmp(cmd_buffer, "help") == 0 || strcasecmp(cmd_buffer, "h") == 0) {
        execute_help();
    }
    else if (strcasecmp(cmd_buffer, "viblock-edit") == 0 || strcasecmp(cmd_buffer, "viblock edit") == 0) {
        start_block_mode(FALSE);
    }
    else if (strcasecmp(cmd_buffer, "viblock-replace") == 0 || strcasecmp(cmd_buffer, "viblock replace") == 0) {
        start_block_mode(TRUE);
    }
    else {
        mlwrite("Unknown command: %s", cmd_buffer);
    }
}

/* Handle key input in command mode */
int command_mode_handle_key(int c) {
    if (!cmd_active) return 0;
    
    switch (c) {
        case 0x0D: /* Enter */
        case 0x0A: /* LF */
        case CONTROL | 'M':
            execute_command();
            cmd_active = 0;
            return 1;
            
        case 0x1B: /* ESC */
            mlwrite("Command cancelled");
            cmd_active = 0;
            return 1;
            
        case 0x7F: /* Backspace/DEL */
        case 0x08: /* Ctrl+H */
            if (cmd_pos > 0) {
                cmd_pos--;
                cmd_buffer[cmd_pos] = '\0';
                command_mode_render();
            }
            return 1;
            
        default:
            /* Accept printable characters */
            if (c >= 0x20 && c < 0x7F && cmd_pos < CMD_BUF_SIZE - 1) {
                cmd_buffer[cmd_pos++] = (char)c;
                cmd_buffer[cmd_pos] = '\0';
                command_mode_render();
            }
            return 1;
    }
}

/* Render command mode UI (status bar) */
void command_mode_render(void) {
    if (!cmd_active) return;
    
    /* Mark current window for hard redraw to prevent buffer rendering from preempting message */
    curwp->w_flag |= WFHARD;
    
    const char *status = (cmd_pos == 0) ? "[number] goto line | help | viblock-edit | viblock-replace" : "Command";
    command_mode_draw_status(status, cmd_buffer, true);
    
    /* Force screen garbage flag for atomic display update */
    sgarbf = TRUE;
    
    /* DO NOT call update() here - message will be shown by main loop's update cycle
     * This prevents the message from being buried by a second buffer redraw */
}

/* Cleanup command mode */
void command_mode_cleanup(void) {
    cmd_active = 0;
    cmd_buffer[0] = '\0';
    cmd_pos = 0;
    block_active = 0;
    block_replace = 0;
    block_anchor_line = NULL;
    block_anchor_offset = 0;
}

/* Command mode activation wrapper for key binding */
int command_mode_activate_command(int f, int n) {
    command_mode_activate();
    return TRUE;
}

/* --- Sed-style replace command implementation --- */

static int read_sed_chunk(const char **pp, char delim, char *dest, size_t dest_sz, const char *label)
{
    size_t idx = 0;
    const char *p = *pp;

    if (dest_sz == 0)
        return FALSE;

    while (*p && *p != delim) {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            if (*p == '\0') {
                mlwrite("Unterminated %s", label);
                return FALSE;
            }
            c = (unsigned char)*p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '\\': c = '\\'; break;
            default:
                break;
            }
        }
        if (idx + 1 >= dest_sz) {
            mlwrite("%s too long", label);
            return FALSE;
        }
        dest[idx++] = (char)c;
    }

    if (*p != delim) {
        mlwrite("Unterminated %s", label);
        return FALSE;
    }

    dest[idx] = '\0';
    *pp = p + 1;
    return TRUE;
}

static int parse_sed_expression(const char *expr, char *pattern, size_t pat_sz,
    char *replacement, size_t rep_sz, int *is_global, int *is_caseless)
{
    const char *p = expr;
    int global = FALSE;
    int caseless = FALSE;

    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != 's' && *p != 'S') {
        mlwrite("Expression must begin with s/");
        return FALSE;
    }

    p++;
    char delim = *p++;
    if (delim == '\0') {
        mlwrite("Missing delimiter");
        return FALSE;
    }

    if (!read_sed_chunk(&p, delim, pattern, pat_sz, "pattern"))
        return FALSE;
    if (pattern[0] == '\0') {
        mlwrite("Empty pattern");
        return FALSE;
    }
    if (!read_sed_chunk(&p, delim, replacement, rep_sz, "replacement"))
        return FALSE;

    while (*p) {
        unsigned char c = (unsigned char)*p++;
        if (isspace(c))
            continue;
        if (c == 'g' || c == 'G')
            global = TRUE;
        else if (c == 'i' || c == 'I')
            caseless = TRUE;
        else {
            mlwrite("Unknown flag '%c'", c);
            return FALSE;
        }
    }

    if (strchr(pattern, '\n') != NULL) {
        mlwrite("Multi-line patterns are not supported");
        return FALSE;
    }
    if (strchr(replacement, '\n') != NULL) {
        mlwrite("Multi-line replacements are not supported");
        return FALSE;
    }

    *is_global = global;
    *is_caseless = caseless;
    return TRUE;
}

static size_t utf8_advance(const char *text, size_t len, size_t offset)
{
    if (offset >= len)
        return len;

    size_t next = offset + 1;
    while (next < len && ((unsigned char)text[next] & 0xC0) == 0x80)
        next++;
    return next;
}

static void build_preview(const char *text, size_t len, char *dest, size_t dest_sz)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    size_t i = 0;

    if (dest_sz == 0)
        return;

    while (i < len && di < dest_sz - 1) {
        unsigned char c = (unsigned char)text[i++];
        if (c == '\n') {
            if (di + 2 >= dest_sz)
                break;
            dest[di++] = '\\';
            dest[di++] = 'n';
        } else if (c == '\t') {
            if (di + 2 >= dest_sz)
                break;
            dest[di++] = '\\';
            dest[di++] = 't';
        } else if (c < 0x20 || c == 0x7F) {
            if (di + 4 >= dest_sz)
                break;
            dest[di++] = '\\';
            dest[di++] = 'x';
            dest[di++] = hex[(c >> 4) & 0xF];
            dest[di++] = hex[c & 0xF];
        } else {
            dest[di++] = (char)c;
        }
    }

    if (i < len && di + 4 < dest_sz) {
        dest[di++] = '.';
        dest[di++] = '.';
        dest[di++] = '.';
    }

    dest[di] = '\0';
}

static char *splice_text(char *text, size_t text_len, size_t start, size_t end,
    const char *replacement, size_t repl_len, size_t *new_len)
{
    size_t tail = text_len - end;
    size_t next_len = text_len - (end - start) + repl_len;
    char *result = malloc(next_len + 1);

    if (result == NULL)
        return NULL;

    if (start > 0)
        memcpy(result, text, start);
    if (repl_len)
        memcpy(result + start, replacement, repl_len);
    if (tail)
        memcpy(result + start + repl_len, text + end, tail);

    result[next_len] = '\0';
    free(text);
    *new_len = next_len;
    return result;
}

static int line_index_from_top(struct line *target)
{
    struct line *lp = lforw(curbp->b_linep);
    int idx = 0;

    while (lp != curbp->b_linep) {
        if (lp == target)
            return idx;
        lp = lforw(lp);
        idx++;
    }
    return -1;
}

static void restore_cursor_to_index(int index, int offset)
{
    struct line *lp = lforw(curbp->b_linep);
    int idx = 0;

    while (lp != curbp->b_linep && idx < index) {
        lp = lforw(lp);
        idx++;
    }

    if (lp == curbp->b_linep) {
        struct line *last = lback(lp);
        if (last != curbp->b_linep)
            lp = last;
    }

    curwp->w_dotp = lp;
    if (lp != curbp->b_linep) {
        if (offset > lp->l_used)
            offset = lp->l_used;
        curwp->w_doto = offset;
    } else {
        curwp->w_doto = 0;
    }
    curwp->w_flag |= WFMOVE;
}

static int block_visual_column(struct line *lp, int offset)
{
    int col = 0;
    int idx = 0;
    int len = lp ? llength(lp) : 0;

    while (lp && idx < offset && idx < len) {
        unicode_t c;
        int bytes = utf8_to_unicode((unsigned char *)lp->l_text, idx, len, &c);
        if (bytes <= 0)
            break;
        col = next_column(col, c, tab_width);
        idx += bytes;
    }
    return col;
}

static void block_bounds(int *top, int *bottom, int *left, int *right)
{
    int anchor = line_index_from_top(block_anchor_line);
    int cursor = line_index_from_top(curwp->w_dotp);
    int anchor_col = block_visual_column(block_anchor_line, block_anchor_offset);
    int cursor_col = block_visual_column(curwp->w_dotp, curwp->w_doto);

    if (top)
        *top = (anchor < cursor) ? anchor : cursor;
    if (bottom)
        *bottom = (anchor > cursor) ? anchor : cursor;
    if (left)
        *left = (anchor_col < cursor_col) ? anchor_col : cursor_col;
    if (right)
        *right = (anchor_col > cursor_col) ? anchor_col : cursor_col;
}

static int line_offset_for_column(struct line *lp, int target_col, int *actual_col)
{
    int len = llength(lp);
    int idx = 0;
    int col = 0;

    while (idx < len) {
        unicode_t c;
        int bytes = utf8_to_unicode((unsigned char *)lp->l_text, idx, len, &c);
        int next_col;

        if (bytes <= 0)
            break;
        next_col = next_column(col, c, tab_width);
        if (next_col > target_col)
            break;
        idx += bytes;
        col = next_col;
    }

    if (actual_col)
        *actual_col = col;
    return idx;
}

static void render_block_status(void)
{
    char status[96];
    int top, bottom, left, right;

    if (!block_active)
        return;

    block_bounds(&top, &bottom, &left, &right);
    snprintf(status, sizeof(status), "%s lines %d-%d cols %d-%d",
        block_replace ? "viblock-replace" : "viblock-edit",
        top + 1, bottom + 1, left + 1, right + 1);
    command_mode_draw_status(status, "[move cursor, Enter apply, Esc cancel]", false);
}

int command_mode_block_is_active(void)
{
    return block_active;
}

int command_mode_block_selection_contains(struct line *lp, int col_start, int col_end)
{
    int top, bottom, left, right;
    int line_idx;

    if (!block_active || !lp)
        return FALSE;

    block_bounds(&top, &bottom, &left, &right);
    line_idx = line_index_from_top(lp);
    if (line_idx < top || line_idx > bottom)
        return FALSE;

    if (right == left)
        right++;
    return col_start < right && col_end > left;
}

static int block_motion_allowed(fn_t func)
{
    return func == backchar || func == forwchar ||
        func == backline || func == forwline ||
        func == gotobol || func == gotoeol ||
        func == gotobob || func == gotoeob ||
        func == backpage || func == forwpage;
}

static int block_apply_text(const char *text, int replace_mode)
{
    int top, bottom, left, right;
    int original_index = line_index_from_top(curwp->w_dotp);
    int original_offset = curwp->w_doto;
    struct line *lp = lforw(curbp->b_linep);
    int idx = 0;

    block_bounds(&top, &bottom, &left, &right);

    while (lp != curbp->b_linep) {
        struct line *next = lforw(lp);
        if (idx >= top && idx <= bottom) {
            int actual_col = 0;
            int start_offset;

            curwp->w_dotp = lp;
            curwp->w_doto = 0;
            start_offset = line_offset_for_column(lp, left, &actual_col);
            curwp->w_doto = start_offset;

            while (actual_col < left) {
                if (linsert(1, ' ') != TRUE)
                    return FALSE;
                actual_col++;
            }

            if (replace_mode) {
                int end_actual_col = 0;
                int end_offset;
                end_offset = line_offset_for_column(curwp->w_dotp, right, &end_actual_col);
                curwp->w_doto = end_offset;
                while (end_actual_col < right) {
                    if (linsert(1, ' ') != TRUE)
                        return FALSE;
                    end_actual_col++;
                }
                curwp->w_doto = start_offset;
                if (ldelete(line_offset_for_column(curwp->w_dotp, right, NULL) - start_offset, FALSE) != TRUE)
                    return FALSE;
            } else {
                curwp->w_doto = start_offset;
            }

            if (text && *text) {
                if (linsert_block((char *)text, (int)strlen(text)) != TRUE)
                    return FALSE;
            }
        }
        lp = next;
        idx++;
    }

    restore_cursor_to_index(original_index < 0 ? 0 : original_index, original_offset);
    curwp->w_flag |= WFHARD | WFMODE;
    return TRUE;
}

int command_mode_block_handle_key(int c, int f, int n)
{
    char text[NSTRING];
    fn_t func;
    int status;

    if (!block_active)
        return FALSE;

    switch (c) {
    case 0x0D:
    case 0x0A:
    case CONTROL | 'M':
        status = minibuf_input(block_replace ? "viblock replace: " : "viblock edit: ", text, sizeof(text));
        if (status == TRUE) {
            if (!block_apply_text(text, block_replace))
                return FALSE;
            mlwrite("%s applied", block_replace ? "viblock replace" : "viblock edit");
        } else {
            mlwrite("%s cancelled", block_replace ? "viblock replace" : "viblock edit");
        }
        block_active = 0;
        block_anchor_line = NULL;
        block_anchor_offset = 0;
        return TRUE;
    case 0x1B:
    case CONTROL | 'G':
        block_active = 0;
        block_anchor_line = NULL;
        block_anchor_offset = 0;
        curwp->w_flag |= WFHARD | WFMODE;
        mlwrite("%s cancelled", block_replace ? "viblock replace" : "viblock edit");
        return TRUE;
    default:
        func = getbind(c);
        if (!block_motion_allowed(func)) {
            render_block_status();
            return TRUE;
        }
        execute(c, f, n);
        curwp->w_flag |= WFHARD | WFMODE;
        render_block_status();
        return TRUE;
    }
}

static int apply_regex_to_line(struct line *lp, pcre2_code *code, pcre2_match_data *match_data,
    const char *replacement, size_t repl_len, int is_global, int *total_count)
{
    char *text = malloc(lp->l_used + 1);
    size_t text_len = lp->l_used;
    size_t search_offset = 0;
    int changed = FALSE;

    if (text == NULL) {
        mlwrite("%%Out of memory");
        return FALSE;
    }

    memcpy(text, lp->l_text, text_len);
    text[text_len] = '\0';

    while (search_offset <= text_len) {
        int rc = pcre2_match(code, (PCRE2_SPTR)text, text_len, search_offset, 0, match_data, NULL);
        if (rc < 0)
            break;

        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
        size_t match_start = ovector[0];
        size_t match_end = ovector[1];

        int zero_width = (match_start == match_end);

        int accept = is_global;
        if (!is_global) {
            char match_preview[REPLACE_PREVIEW];
            char repl_preview[REPLACE_PREVIEW];
            char prompt[REPLACE_PREVIEW * 2 + 32];

            build_preview(text + match_start, match_end - match_start, match_preview, sizeof(match_preview));
            build_preview(replacement, repl_len, repl_preview, sizeof(repl_preview));
            snprintf(prompt, sizeof(prompt), "Replace '%s' with '%s'", match_preview, repl_preview);
            accept = mlyesno(prompt);
        }

        size_t next_offset = match_end;

        if (accept == TRUE) {
            changed = TRUE;
            (*total_count)++;
            size_t new_len;
            char *new_text = splice_text(text, text_len, match_start, match_end, replacement, repl_len, &new_len);
            if (new_text == NULL) {
                mlwrite("%%Out of memory");
                free(text);
                return FALSE;
            }
            text = new_text;
            text_len = new_len;
            next_offset = match_start + repl_len;
        }

        if (zero_width && next_offset == match_start) {
            next_offset = utf8_advance(text, text_len, match_start);
            if (next_offset == match_start)
                next_offset++;
        }

        if (next_offset > text_len)
            break;

        search_offset = next_offset;
    }

    if (changed) {
        curwp->w_dotp = lp;
        curwp->w_doto = 0;
        ldelete(llength(lp), FALSE);
        if (text_len > 0)
            linsert_block(text, (int)text_len);
    }

    free(text);
    return TRUE;
}

int sed_replace_command(int f, int n)
{
    char expr[NSTRING];
    char pattern[NPAT];
    char replacement[NSTRING];
    int is_global = FALSE;
    int is_caseless = FALSE;
    int status;
    int total = 0;
    int original_index;
    int original_offset;
    pcre2_code *code;
    pcre2_match_data *match_data;
    int errornumber;
    PCRE2_SIZE erroffset;
    uint32_t options = PCRE2_UTF | PCRE2_UCP | PCRE2_MULTILINE;
    size_t repl_len;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    status = minibuf_input("sed replace: ", expr, sizeof(expr));
    if (status != TRUE)
        return status;

    if (!parse_sed_expression(expr, pattern, sizeof(pattern), replacement, sizeof(replacement), &is_global, &is_caseless))
        return FALSE;

    if (is_caseless)
        options |= PCRE2_CASELESS;

    code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, options, &errornumber, &erroffset, NULL);
    if (code == NULL) {
        char errbuf[128];
        pcre2_get_error_message(errornumber, (PCRE2_UCHAR *)errbuf, sizeof(errbuf));
        mlwrite("Regex error at %d: %s", (int)erroffset, errbuf);
        return FALSE;
    }

    match_data = pcre2_match_data_create_from_pattern(code, NULL);
    if (match_data == NULL) {
        pcre2_code_free(code);
        mlwrite("%%Out of memory");
        return FALSE;
    }

    original_index = line_index_from_top(curwp->w_dotp);
    original_offset = curwp->w_doto;
    repl_len = strlen(replacement);

    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        struct line *next = lforw(lp);
        if (!apply_regex_to_line(lp, code, match_data, replacement, repl_len, is_global, &total)) {
            pcre2_match_data_free(match_data);
            pcre2_code_free(code);
            restore_cursor_to_index(original_index < 0 ? 0 : original_index, original_offset);
            return FALSE;
        }
        lp = next;
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(code);

    restore_cursor_to_index(original_index < 0 ? 0 : original_index, original_offset);

    if (total == 0)
        mlwrite("No matches for pattern");
    else
        mlwrite("Replaced %d occurrence%s", total, total == 1 ? "" : "s");

    return TRUE;
}
