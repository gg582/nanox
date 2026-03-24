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
static void command_mode_prompt(void);
static void execute_command(const char *input);
static void command_mode_trim(char *text);
static int command_mode_parse_line_range(const char *text, int *start_line, int *end_line);
static int command_mode_apply_indent_range(int start_line, int end_line, int indent_direction);
static int command_mode_handle_range_command(const char *input, const char *name, int indent_direction);
static int command_mode_handle_lint_command(const char *input);
static int command_mode_handle_set_nr_command(const char *input);
static int command_mode_handle_flip_command(const char *input);
static int command_mode_apply_indent_lint(void);
static int command_mode_total_lines(void);
static struct line *command_mode_line_at_number(int number);

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
    block_active = 0;
    block_replace = 0;
    block_anchor_line = NULL;
    block_anchor_offset = 0;
}

/* Activate F1 command mode */
void command_mode_activate(void) {
    command_mode_prompt();
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
static void command_mode_trim(char *text)
{
    if (!text)
        return;

    char *start = text;
    while (*start && isspace((unsigned char)*start))
        start++;

    if (start != text)
        memmove(text, start, strlen(start) + 1);

    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1]))
        len--;
    text[len] = '\0';
}

static void command_mode_prompt(void)
{
    char input[CMD_BUF_SIZE];
    int status = minibuf_input("Command Mode: ", input, sizeof(input));
    if (status == TRUE)
        execute_command(input);
    nanox_request_underbar_redraw();
}

static int command_mode_handle_range_command(const char *input, const char *name, int indent_direction)
{
    size_t cmd_len = strlen(name);
    if (strncasecmp(input, name, cmd_len) != 0)
        return FALSE;

    const char *range = input + cmd_len;
    if (*range && !isspace((unsigned char)*range))
        return FALSE;

    while (*range && isspace((unsigned char)*range))
        range++;

    if (*range == '\0') {
        mlwrite("[%s syntax: %s start-end]", name, name);
        return TRUE;
    }

    int start_line, end_line;
    if (!command_mode_parse_line_range(range, &start_line, &end_line)) {
        mlwrite("[%s range must be start-end]", name);
        return TRUE;
    }

    command_mode_apply_indent_range(start_line, end_line, indent_direction);
    return TRUE;
}

static int command_mode_handle_lint_command(const char *input)
{
    char buffer[CMD_BUF_SIZE];
    mystrscpy(buffer, input, sizeof(buffer));
    command_mode_trim(buffer);

    if (!(strcasecmp(buffer, "lint") == 0 || strcasecmp(buffer, "tidy") == 0))
        return FALSE;

    command_mode_apply_indent_lint();
    return TRUE;
}

static void execute_command(const char *input) {
    if (input == NULL || *input == '\0') {
        mlwrite("Empty command");
        return;
    }

    char buffer[CMD_BUF_SIZE];
    mystrscpy(buffer, input, sizeof(buffer));
    command_mode_trim(buffer);

    if (buffer[0] == '\0') {
        mlwrite("Empty command");
        return;
    }

    if (command_mode_handle_range_command(buffer, "indent", 1))
        return;
    if (command_mode_handle_range_command(buffer, "outdent", -1))
        return;
    if (command_mode_handle_lint_command(buffer))
        return;
    if (command_mode_handle_set_nr_command(buffer))
        return;
    if (command_mode_handle_flip_command(buffer))
        return;
    
    /* Check if it's a number (goto line) */
    int is_number = 1;
    for (char *p = buffer; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            is_number = 0;
            break;
        }
    }
    
    if (is_number) {
        int line_num = atoi(buffer);
        execute_goto_line(line_num);
    }
    /* Check for Help command (case insensitive) */
    else if (strcasecmp(buffer, "help") == 0 || strcasecmp(buffer, "h") == 0) {
        execute_help();
    }
    else if (strcasecmp(buffer, "viblock-edit") == 0 || strcasecmp(buffer, "viblock edit") == 0) {
        start_block_mode(FALSE);
    }
    else if (strcasecmp(buffer, "viblock-replace") == 0 || strcasecmp(buffer, "viblock replace") == 0) {
        start_block_mode(TRUE);
    }
    else {
        mlwrite("Unknown command: %s", buffer);
    }
}

/* Cleanup command mode */
void command_mode_cleanup(void) {
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

static void restore_saved_cursor(int index, int offset)
{
    if (index < 0) {
        curwp->w_flag |= WFMOVE;
        return;
    }
    restore_cursor_to_index(index, offset);
}

static int command_mode_total_lines(void)
{
    int total = 0;
    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        total++;
        lp = lforw(lp);
    }
    return total;
}

static struct line *command_mode_line_at_number(int number)
{
    struct line *lp = lforw(curbp->b_linep);
    int idx = 1;
    while (lp != curbp->b_linep && idx < number) {
        lp = lforw(lp);
        idx++;
    }
    if (lp == curbp->b_linep)
        return NULL;
    return lp;
}

static int command_mode_parse_line_range(const char *text, int *start_line, int *end_line)
{
    if (!text || !start_line || !end_line)
        return FALSE;

    char buffer[CMD_BUF_SIZE];
    mystrscpy(buffer, text, sizeof(buffer));

    char *dash = strchr(buffer, '-');
    if (!dash)
        return FALSE;

    *dash = '\0';
    char *left = buffer;
    char *right = dash + 1;

    command_mode_trim(left);
    command_mode_trim(right);

    if (*left == '\0' || *right == '\0')
        return FALSE;

    char *endptr;
    long start = strtol(left, &endptr, 10);
    if (*endptr != '\0')
        return FALSE;
    long end = strtol(right, &endptr, 10);
    if (*endptr != '\0')
        return FALSE;

    if (start > end) {
        long tmp = start;
        start = end;
        end = tmp;
    }

    if (start < 1)
        start = 1;
    if (end < 1)
        end = 1;

    *start_line = (int)start;
    *end_line = (int)end;
    return TRUE;
}

static int command_mode_apply_indent_range(int start_line, int end_line, int indent_direction)
{
    int total = command_mode_total_lines();
    if (total <= 0) {
        mlwrite("Buffer is empty");
        return FALSE;
    }

    if (start_line < 1)
        start_line = 1;
    if (end_line < 1)
        end_line = 1;
    if (start_line > total)
        start_line = total;
    if (end_line > total)
        end_line = total;
    if (start_line > end_line) {
        int tmp = start_line;
        start_line = end_line;
        end_line = tmp;
    }

    struct line *start_lp = command_mode_line_at_number(start_line);
    struct line *end_lp = command_mode_line_at_number(end_line);
    if (!start_lp || !end_lp) {
        mlwrite("Invalid line range");
        return FALSE;
    }

    struct line *saved_start = indent_start_lp;
    struct line *saved_end = indent_end_lp;
    int saved_type = indent_range_type;
    int saved_active = indent_selection_active;

    indent_start_lp = start_lp;
    indent_end_lp = end_lp;
    indent_range_type = indent_direction;
    indent_selection_active = FALSE;

    int status = indent_apply_range(FALSE, 1);

    indent_start_lp = saved_start;
    indent_end_lp = saved_end;
    indent_range_type = saved_type;
    indent_selection_active = saved_active;

    return status;
}

static int command_mode_get_indent(const struct line *lp)
{
    int col = 0;
    int i;
    int len = llength((struct line *)lp);
    for (i = 0; i < len; ++i) {
        int c = lgetc((struct line *)lp, i);
        if (c != ' ' && c != '\t')
            break;
        if (c == '\t')
            col = nextab(col);
        else
            col++;
    }
    return col;
}

static int command_mode_is_blank_line(const struct line *lp)
{
    int i;
    int len = llength((struct line *)lp);
    for (i = 0; i < len; ++i) {
        int c = lgetc((struct line *)lp, i);
        if (c != ' ' && c != '\t')
            return FALSE;
    }
    return TRUE;
}

static int command_mode_detect_indent_step(void)
{
    struct line *lp;
    int freq[9];
    int prev_indent = -1;
    int i;
    int best = tab_width;
    int best_count = 0;

    for (i = 0; i < 9; i++)
        freq[i] = 0;

    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        if (llength(lp) > 0 && lgetc(lp, 0) == '\t')
            return (tab_width > 0) ? tab_width : 1;
    }

    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        if (!command_mode_is_blank_line(lp)) {
            int ind = command_mode_get_indent(lp);
            if (prev_indent >= 0 && ind > prev_indent) {
                int delta = ind - prev_indent;
                if (delta >= 1 && delta <= 8)
                    freq[delta]++;
            }
            prev_indent = ind;
        }
    }

    for (i = 1; i <= 8; i++) {
        if (freq[i] > best_count) {
            best_count = freq[i];
            best = i;
        }
    }
    if (best <= 0)
        best = 1;
    return best;
}

static int command_mode_line_starts_with_closing_block(const struct line *lp)
{
    int i = 0;
    int len = llength((struct line *)lp);
    char word[32];
    int wlen = 0;
    char *ext = strrchr(curbp->b_fname, '.');

    while (i < len) {
        int c = lgetc((struct line *)lp, i);
        if (c != ' ' && c != '\t')
            break;
        i++;
    }
    if (i >= len)
        return FALSE;

    {
        int c = lgetc((struct line *)lp, i);
        if (c == '}' || c == ')' || c == ']')
            return TRUE;
    }

    while (i < len && wlen < (int)sizeof(word) - 1) {
        int c = lgetc((struct line *)lp, i);
        if (c == ' ' || c == '\t' || c == '\n' ||
            c == '(' || c == '{' || c == '[' || c == ')' || c == '}' || c == ']')
            break;
        word[wlen++] = (char)c;
        i++;
    }
    word[wlen] = '\0';
    if (wlen == 0 || ext == NULL)
        return FALSE;

    if (strcasecmp(ext, ".sh") == 0 || strcasecmp(ext, ".bash") == 0) {
        if (strcmp(word, "fi") == 0 || strcmp(word, "done") == 0 ||
            strcmp(word, "esac") == 0 || strcmp(word, "else") == 0 ||
            strcmp(word, "elif") == 0)
            return TRUE;
    } else if (strcasecmp(ext, ".py") == 0) {
        if (strcmp(word, "else") == 0 || strcmp(word, "elif") == 0 ||
            strcmp(word, "except") == 0 || strcmp(word, "finally") == 0)
            return TRUE;
    } else if (strcasecmp(ext, ".lua") == 0) {
        if (strcmp(word, "end") == 0 || strcmp(word, "else") == 0 ||
            strcmp(word, "elseif") == 0 || strcmp(word, "until") == 0)
            return TRUE;
    }

    return FALSE;
}

static int command_mode_line_ends_with_open_block(const struct line *lp)
{
    int i;
    int len = llength((struct line *)lp);

    for (i = len - 1; i >= 0; --i) {
        int c = lgetc((struct line *)lp, i);
        if (c == ' ' || c == '\t')
            continue;
        return (c == '{' || c == '(' || c == '[') ? TRUE : FALSE;
    }
    return FALSE;
}

static int command_mode_set_indent_on_line(struct line *lp, int target)
{
    int ch;

    if (target < 0)
        target = 0;
    curwp->w_dotp = lp;
    curwp->w_doto = 0;

    while (curwp->w_doto < llength(curwp->w_dotp)) {
        ch = lgetc(curwp->w_dotp, curwp->w_doto);
        if (ch != ' ' && ch != '\t')
            break;
        if (ldelchar(1, FALSE) != TRUE)
            return FALSE;
    }

    if (target > 0) {
        if (nanox_cfg.soft_tab) {
            int i;
            for (i = 0; i < target; ++i) {
                if (linsert(1, ' ') != TRUE)
                    return FALSE;
            }
        } else {
            int step = tab_width;
            int tabs;
            int spaces;
            if (step <= 0)
                step = 1;
            tabs = target / step;
            spaces = target % step;
            while (tabs--) {
                if (linsert(1, '\t') != TRUE)
                    return FALSE;
            }
            while (spaces--) {
                if (linsert(1, ' ') != TRUE)
                    return FALSE;
            }
        }
    }
    return TRUE;
}

static int command_mode_apply_indent_lint(void)
{
    int total = command_mode_total_lines();
    int step;
    int changed = 0;
    int original_index = line_index_from_top(curwp->w_dotp);
    int original_offset = curwp->w_doto;
    struct line *lp;
    struct line *prev_code = NULL;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    if (total <= 0) {
        mlwrite("Buffer is empty");
        return FALSE;
    }

    step = command_mode_detect_indent_step();
    if (step <= 0)
        step = 1;

    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        int target;

        if (command_mode_is_blank_line(lp))
            continue;

        target = prev_code ? command_mode_get_indent(prev_code) : 0;
        if (prev_code && command_mode_line_ends_with_open_block(prev_code))
            target += step;
        if (command_mode_line_starts_with_closing_block(lp))
            target -= step;
        if (target < 0)
            target = 0;

        if (command_mode_get_indent(lp) != target) {
            if (command_mode_set_indent_on_line(lp, target) != TRUE) {
                restore_saved_cursor(original_index, original_offset);
                return FALSE;
            }
            changed++;
        }

        prev_code = lp;
    }

    restore_saved_cursor(original_index, original_offset);
    if (changed > 0)
        lchange(WFHARD);
    mlwrite("lint tidy applied (%d line%s, step %d)", changed, (changed == 1) ? "" : "s", step);
    return TRUE;
}

static int command_mode_parse_numbering_prefix(const unsigned char *text, int len,
    int *indent_end, int *content_start, char *suffix, size_t suffix_sz)
{
    int i = 0;
    int j;
    int k;
    size_t si = 0;
    char delim;

    while (i < len && (text[i] == ' ' || text[i] == '\t'))
        i++;
    if (indent_end)
        *indent_end = i;

    j = i;
    while (j < len && isdigit(text[j]))
        j++;
    if (j == i || j >= len)
        return FALSE;

    delim = (char)text[j];
    if (!(delim == '.' || delim == ':' || delim == ')'))
        return FALSE;

    k = j + 1;
    while (k < len && (text[k] == ' ' || text[k] == '\t'))
        k++;
    if (content_start)
        *content_start = k;

    if (suffix && suffix_sz > 0) {
        suffix[si++] = delim;
        if (k == j + 1) {
            if (si < suffix_sz - 1)
                suffix[si++] = ' ';
        } else {
            int t;
            for (t = j + 1; t < k && si < suffix_sz - 1; ++t)
                suffix[si++] = (char)text[t];
        }
        suffix[si] = '\0';
    }
    return TRUE;
}

static int command_mode_line_number_of(struct line *target)
{
    int line = 1;
    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        if (lp == target)
            return line;
        lp = lforw(lp);
        line++;
    }
    return -1;
}

static int command_mode_guess_numbering_suffix(int start_line, int end_line, char *suffix, size_t suffix_sz)
{
    struct line *lp = command_mode_line_at_number(start_line);
    int line = start_line;
    struct {
        char value[8];
        int count;
    } candidates[8];
    int candidate_count = 0;
    int best = -1;

    if (!lp || !suffix || suffix_sz == 0)
        return FALSE;

    while (lp != curbp->b_linep && line <= end_line) {
        char current[8];
        if (command_mode_parse_numbering_prefix(lp->l_text, llength(lp), NULL, NULL, current, sizeof(current))) {
            int i;
            int found = -1;
            for (i = 0; i < candidate_count; ++i) {
                if (strcmp(candidates[i].value, current) == 0) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                candidates[found].count++;
            } else if (candidate_count < 8) {
                mystrscpy(candidates[candidate_count].value, current, sizeof(candidates[candidate_count].value));
                candidates[candidate_count].count = 1;
                candidate_count++;
            }
        }
        lp = lforw(lp);
        line++;
    }

    if (candidate_count == 0) {
        mystrscpy(suffix, ". ", suffix_sz);
        return TRUE;
    }

    {
        int i;
        for (i = 0; i < candidate_count; ++i) {
            if (best < 0 || candidates[i].count > candidates[best].count)
                best = i;
        }
    }
    mystrscpy(suffix, candidates[best].value, suffix_sz);
    return TRUE;
}

static int command_mode_apply_numbering_range(int start_line, int end_line, int reverse)
{
    int total = command_mode_total_lines();
    int line;
    int changed = 0;
    int original_index = line_index_from_top(curwp->w_dotp);
    int original_offset = curwp->w_doto;
    struct line *lp;
    char suffix[8];

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    if (total <= 0) {
        mlwrite("Buffer is empty");
        return FALSE;
    }

    if (start_line < 1)
        start_line = 1;
    if (end_line < 1)
        end_line = 1;
    if (start_line > total)
        start_line = total;
    if (end_line > total)
        end_line = total;
    if (start_line > end_line) {
        int tmp = start_line;
        start_line = end_line;
        end_line = tmp;
    }

    if (!command_mode_guess_numbering_suffix(start_line, end_line, suffix, sizeof(suffix))) {
        mlwrite("Failed to detect numbering format");
        return FALSE;
    }

    lp = command_mode_line_at_number(start_line);
    if (!lp) {
        mlwrite("Invalid range");
        return FALSE;
    }

    line = start_line;
    while (lp != curbp->b_linep && line <= end_line) {
        int len = llength(lp);
        char *text = malloc((size_t)len + 1);
        int indent_end = 0;
        int content_start = 0;
        int new_number = reverse ? (end_line - (line - start_line)) : (start_line + (line - start_line));
        char number_buf[32];
        int number_len;
        int suffix_len = (int)strlen(suffix);
        int new_len;
        char *new_text;
        struct line *next = lforw(lp);

        if (!text) {
            restore_saved_cursor(original_index, original_offset);
            mlwrite("%%Out of memory");
            return FALSE;
        }
        memcpy(text, lp->l_text, (size_t)len);
        text[len] = '\0';

        if (!command_mode_parse_numbering_prefix((unsigned char *)text, len, &indent_end, &content_start, NULL, 0))
            content_start = indent_end;

        snprintf(number_buf, sizeof(number_buf), "%d", new_number);
        number_len = (int)strlen(number_buf);
        new_len = indent_end + number_len + suffix_len + (len - content_start);
        new_text = malloc((size_t)new_len + 1);
        if (!new_text) {
            free(text);
            restore_saved_cursor(original_index, original_offset);
            mlwrite("%%Out of memory");
            return FALSE;
        }

        if (indent_end > 0)
            memcpy(new_text, text, (size_t)indent_end);
        memcpy(new_text + indent_end, number_buf, (size_t)number_len);
        memcpy(new_text + indent_end + number_len, suffix, (size_t)suffix_len);
        if (len - content_start > 0)
            memcpy(new_text + indent_end + number_len + suffix_len, text + content_start, (size_t)(len - content_start));
        new_text[new_len] = '\0';

        curwp->w_dotp = lp;
        curwp->w_doto = 0;
        if (ldelete(llength(lp), FALSE) != TRUE ||
            (new_len > 0 && linsert_block(new_text, new_len) != TRUE)) {
            free(new_text);
            free(text);
            restore_saved_cursor(original_index, original_offset);
            return FALSE;
        }

        free(new_text);
        free(text);
        changed++;
        lp = next;
        line++;
    }

    restore_saved_cursor(original_index, original_offset);
    if (changed > 0)
        lchange(WFHARD);
    mlwrite("viblock-set-nr applied: lines %d-%d (%s)", start_line, end_line, reverse ? "reverse" : "forward");
    return TRUE;
}

static int command_mode_handle_set_nr_command(const char *input)
{
    const char *name1 = "viblock-set-nr";
    const char *name2 = "vibloc-set-nr";
    size_t cmd_len = 0;
    const char *args = NULL;
    char range[64];
    char opt1[32];
    int parsed;
    int consumed = 0;
    int start_line;
    int end_line;
    int reverse = FALSE;

    if (strncasecmp(input, name1, strlen(name1)) == 0) {
        cmd_len = strlen(name1);
    } else if (strncasecmp(input, name2, strlen(name2)) == 0) {
        cmd_len = strlen(name2);
    } else {
        return FALSE;
    }

    args = input + cmd_len;
    if (*args && !isspace((unsigned char)*args))
        return FALSE;
    while (*args && isspace((unsigned char)*args))
        args++;
    if (*args == '\0') {
        mlwrite("[viblock-set-nr syntax: viblock-set-nr start-end [rev]]");
        return TRUE;
    }

    range[0] = '\0';
    opt1[0] = '\0';
    parsed = sscanf(args, "%63s %31s %n", range, opt1, &consumed);
    if (parsed < 1 || parsed > 2) {
        mlwrite("[viblock-set-nr syntax: viblock-set-nr start-end [rev]]");
        return TRUE;
    }
    while (args[consumed] != '\0') {
        if (!isspace((unsigned char)args[consumed])) {
            mlwrite("[viblock-set-nr syntax: viblock-set-nr start-end [rev]]");
            return TRUE;
        }
        consumed++;
    }
    if (!command_mode_parse_line_range(range, &start_line, &end_line)) {
        mlwrite("[viblock-set-nr range must be start-end]");
        return TRUE;
    }
    if (parsed == 2) {
        if (strcasecmp(opt1, "rev") == 0 || strcasecmp(opt1, "reverse") == 0) {
            reverse = TRUE;
        } else {
            mlwrite("[viblock-set-nr option must be rev]");
            return TRUE;
        }
    }

    command_mode_apply_numbering_range(start_line, end_line, reverse);
    return TRUE;
}

static int command_mode_swap_ranges(int first_start, int first_end, int second_start, int second_end)
{
    struct line *a_start = command_mode_line_at_number(first_start);
    struct line *a_end = command_mode_line_at_number(first_end);
    struct line *b_start = command_mode_line_at_number(second_start);
    struct line *b_end = command_mode_line_at_number(second_end);
    struct line *pre_a;
    struct line *post_a;
    struct line *pre_b;
    struct line *post_b;
    int adjacent;
    int cursor_line = command_mode_line_number_of(curwp->w_dotp);
    int cursor_off = curwp->w_doto;

    if (!a_start || !a_end || !b_start || !b_end) {
        mlwrite("Invalid line range");
        return FALSE;
    }

    pre_a = lback(a_start);
    post_a = lforw(a_end);
    pre_b = lback(b_start);
    post_b = lforw(b_end);
    adjacent = (post_a == b_start);

    lforw(pre_a) = b_start;
    lback(b_start) = pre_a;

    if (adjacent) {
        lforw(b_end) = a_start;
        lback(a_start) = b_end;
    } else {
        lforw(b_end) = post_a;
        lback(post_a) = b_end;
        lforw(pre_b) = a_start;
        lback(a_start) = pre_b;
    }

    lforw(a_end) = post_b;
    lback(post_b) = a_end;

    if (cursor_line >= 1)
        restore_cursor_to_index(cursor_line - 1, cursor_off);
    lchange(WFHARD);
    curwp->w_flag |= WFMOVE | WFHARD | WFMODE;
    return TRUE;
}

static int command_mode_handle_flip_command(const char *input)
{
    const char *name = "viblock-flip";
    size_t cmd_len = strlen(name);
    const char *args = input + cmd_len;
    char range1[64];
    char range2[64];
    char extra[32];
    int parsed;
    int start1;
    int end1;
    int start2;
    int end2;
    int total;

    if (strncasecmp(input, name, cmd_len) != 0)
        return FALSE;
    if (*args && !isspace((unsigned char)*args))
        return FALSE;
    while (*args && isspace((unsigned char)*args))
        args++;
    if (*args == '\0') {
        mlwrite("[viblock-flip syntax: viblock-flip a-b c-d]");
        return TRUE;
    }

    range1[0] = '\0';
    range2[0] = '\0';
    extra[0] = '\0';
    parsed = sscanf(args, "%63s %63s %31s", range1, range2, extra);
    if (parsed != 2) {
        mlwrite("[viblock-flip syntax: viblock-flip a-b c-d]");
        return TRUE;
    }
    if (!command_mode_parse_line_range(range1, &start1, &end1) ||
        !command_mode_parse_line_range(range2, &start2, &end2)) {
        mlwrite("[viblock-flip range must be start-end]");
        return TRUE;
    }

    total = command_mode_total_lines();
    if (total <= 0) {
        mlwrite("Buffer is empty");
        return TRUE;
    }

    if (start1 < 1) start1 = 1;
    if (start2 < 1) start2 = 1;
    if (end1 > total) end1 = total;
    if (end2 > total) end2 = total;

    if (!(end1 < start2 || end2 < start1)) {
        mlwrite("[viblock-flip requires non-overlapping ranges]");
        return TRUE;
    }

    if (start2 < start1) {
        int swap_temp;
        swap_temp = start1; start1 = start2; start2 = swap_temp;
        swap_temp = end1; end1 = end2; end2 = swap_temp;
    }

    if (command_mode_swap_ranges(start1, end1, start2, end2) == TRUE)
        mlwrite("viblock-flip applied: %d-%d <-> %d-%d", start1, end1, start2, end2);
    return TRUE;
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
                if (linsert_block(text, (int)strlen(text)) != TRUE)
                    return FALSE;
            }
        }
        lp = next;
        idx++;
    }

    restore_saved_cursor(original_index, original_offset);
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

    if (curbp->b_mode & MDVIEW) {
        int ro = rdonly();
        nanox_request_underbar_redraw();
        return ro;
    }

    status = minibuf_input("sed replace: ", expr, sizeof(expr));
    if (status != TRUE) {
        nanox_request_underbar_redraw();
        return status;
    }

    if (!parse_sed_expression(expr, pattern, sizeof(pattern), replacement, sizeof(replacement), &is_global, &is_caseless)) {
        nanox_request_underbar_redraw();
        return FALSE;
    }

    if (is_caseless)
        options |= PCRE2_CASELESS;

    code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, options, &errornumber, &erroffset, NULL);
    if (code == NULL) {
        char errbuf[128];
        pcre2_get_error_message(errornumber, (PCRE2_UCHAR *)errbuf, sizeof(errbuf));
        mlwrite("Regex error at %d: %s", (int)erroffset, errbuf);
        nanox_request_underbar_redraw();
        return FALSE;
    }

    match_data = pcre2_match_data_create_from_pattern(code, NULL);
    if (match_data == NULL) {
        pcre2_code_free(code);
        mlwrite("%%Out of memory");
        nanox_request_underbar_redraw();
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
            restore_saved_cursor(original_index, original_offset);
            nanox_request_underbar_redraw();
            return FALSE;
        }
        lp = next;
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(code);

    restore_saved_cursor(original_index, original_offset);

    if (total == 0)
        mlwrite("No matches for pattern");
    else
        mlwrite("Replaced %d occurrence%s", total, total == 1 ? "" : "s");

    nanox_request_underbar_redraw();
    return TRUE;
}
