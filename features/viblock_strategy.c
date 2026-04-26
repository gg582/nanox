#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "estruct.h"
#include "edef.h"
#include "line.h"
#include "efunc.h"
#include "viblock_strategy.h"
#include "util.h"

extern int line_index_from_top(struct line *target);
extern int line_offset_for_column(struct line *lp, int target_col, int *actual_col);
extern void restore_saved_cursor(int index, int offset);
extern void command_mode_draw_status(const char *status, const char *hint, int is_error);
extern int command_mode_apply_numbering_range(int start_line, int end_line, int reverse);
extern int command_mode_swap_ranges(int start1, int end1, int start2, int end2);

static BlockMode block_mode = BLOCK_MODE_NONE;
static int block_set_nr_reverse = 0;
static struct line *block_anchor_line = NULL;
static int block_anchor_offset = 0;

static int block_visual_column(struct line *lp, int offset) {
    int col = 0, idx = 0;
    int len = lp ? llength(lp) : 0;
    while (lp && idx < offset && idx < len) {
        unicode_t c;
        int bytes = utf8_to_unicode((unsigned char *)ltext(lp), idx, len, &c);
        if (bytes <= 0) break;
        col = next_column(col, c, tab_width);
        idx += bytes;
    }
    return col;
}

static void block_bounds(int *top, int *bottom, int *left, int *right) {
    int anchor = line_index_from_top(block_anchor_line);
    int cursor = line_index_from_top(curwp->w_dotp);
    int anchor_col = block_visual_column(block_anchor_line, block_anchor_offset);
    int cursor_col = block_visual_column(curwp->w_dotp, curwp->w_doto);
    if (top) *top = (anchor < cursor) ? anchor : cursor;
    if (bottom) *bottom = (anchor > cursor) ? anchor : cursor;
    if (left) *left = (anchor_col < cursor_col) ? anchor_col : cursor_col;
    if (right) *right = (anchor_col > cursor_col) ? anchor_col : cursor_col;
}

static int block_apply_text(const char *text, int replace_mode) {
    int top, bottom, left, right;
    int original_index = line_index_from_top(curwp->w_dotp);
    int original_offset = curwp->w_doto;
    struct line *lp;
    int idx = 0;

    block_bounds(&top, &bottom, &left, &right);
    lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep && idx <= bottom) {
        struct line *next = lforw(lp);
        if (idx >= top) {
            int actual_col = 0, start_offset;
            curwp->w_dotp = lp; curwp->w_doto = 0;
            start_offset = line_offset_for_column(lp, left, &actual_col);
            curwp->w_doto = start_offset;
            while (actual_col < left) {
                if (linsert(1, ' ') != TRUE) return FALSE;
                actual_col++;
            }
            start_offset = curwp->w_doto;
            if (replace_mode) {
                int actual_end_col = 0, end_offset;
                end_offset = line_offset_for_column(lp, right, &actual_end_col);
                curwp->w_doto = end_offset;
                while (actual_end_col < right) {
                    if (linsert(1, ' ') != TRUE) return FALSE;
                    actual_end_col++;
                }
                end_offset = curwp->w_doto;
                curwp->w_doto = start_offset;
                if (ldelete((long)(end_offset - start_offset), FALSE) != TRUE) return FALSE;
            }
            curwp->w_doto = start_offset;
            if (text && *text) {
                if (linsert_block(text, (int)strlen(text)) != TRUE) return FALSE;
            }
        }
        lp = next;
        idx++;
    }
    restore_saved_cursor(original_index, original_offset);
    curwp->w_flag |= WFHARD | WFMODE;
    return TRUE;
}

/* Strategy implementations */
static int strategy_edit_apply(const char *input) { return block_apply_text(input, 0); }
static void strategy_edit_render(int top, int bottom, int left, int right, int reverse) {
    char status[96];
    (void)reverse;
    snprintf(status, sizeof(status), "viblock-edit lines %d-%d cols %d-%d", top + 1, bottom + 1, left + 1, right + 1);
    command_mode_draw_status(status, "[move cursor, Enter apply, Esc cancel]", 0);
}

static int strategy_replace_apply(const char *input) { return block_apply_text(input, 1); }
static void strategy_replace_render(int top, int bottom, int left, int right, int reverse) {
    char status[96];
    (void)reverse;
    snprintf(status, sizeof(status), "viblock-replace lines %d-%d cols %d-%d", top + 1, bottom + 1, left + 1, right + 1);
    command_mode_draw_status(status, "[move cursor, Enter apply, Esc cancel]", 0);
}

static int strategy_setnr_apply(const char *input) {
    int top, bottom;
    (void)input;
    block_bounds(&top, &bottom, NULL, NULL);
    return command_mode_apply_numbering_range(top + 1, bottom + 1, block_set_nr_reverse);
}
static void strategy_setnr_render(int top, int bottom, int left, int right, int reverse) {
    char status[96];
    snprintf(status, sizeof(status), "viblock-set-nr (%s) lines %d-%d cols %d-%d", reverse ? "rev" : "fwd", top + 1, bottom + 1, left + 1, right + 1);
    command_mode_draw_status(status, "[move cursor, Enter apply, Esc cancel]", 0);
}

static ViblockStrategy edit_strategy    = { "viblock-edit",    strategy_edit_apply,    strategy_edit_render };
static ViblockStrategy replace_strategy = { "viblock-replace", strategy_replace_apply, strategy_replace_render };
static ViblockStrategy setnr_strategy   = { "viblock-set-nr",  strategy_setnr_apply,   strategy_setnr_render };

static ViblockStrategy *current_strategy = NULL;

void viblock_start(BlockMode mode, int reverse_flag) {
    block_mode = mode;
    block_anchor_line = curwp->w_dotp;
    block_anchor_offset = curwp->w_doto;
    block_set_nr_reverse = (mode == BLOCK_MODE_SET_NR) ? reverse_flag : 0;
    if (mode == BLOCK_MODE_EDIT) current_strategy = &edit_strategy;
    else if (mode == BLOCK_MODE_REPLACE) current_strategy = &replace_strategy;
    else if (mode == BLOCK_MODE_SET_NR) current_strategy = &setnr_strategy;
    else current_strategy = NULL;
    curwp->w_flag |= WFHARD | WFMODE;
    viblock_render_status();
}

void viblock_reset(void) {
    block_mode = BLOCK_MODE_NONE;
    current_strategy = NULL;
    block_set_nr_reverse = 0;
    block_anchor_line = NULL;
    block_anchor_offset = 0;
}

void viblock_render_status(void) {
    int top, bottom, left, right;
    if (!current_strategy) return;
    block_bounds(&top, &bottom, &left, &right);
    current_strategy->render_status(top, bottom, left, right, block_set_nr_reverse);
}

int viblock_is_active(void) {
    return block_mode != BLOCK_MODE_NONE;
}

int viblock_selection_contains(struct line *lp, int col_start, int col_end) {
    int top, bottom, left, right, line_idx;
    if (block_mode == BLOCK_MODE_NONE || !lp) return FALSE;
    block_bounds(&top, &bottom, &left, &right);
    line_idx = line_index_from_top(lp);
    if (line_idx < top || line_idx > bottom) return FALSE;
    return col_start < right && col_end > left;
}

int viblock_apply_flip(int start1, int end1, int start2, int end2) {
    return command_mode_swap_ranges(start1, end1, start2, end2);
}

static int block_motion_allowed(fn_t func) {
    return func == backchar || func == forwchar || func == backline || func == forwline ||
           func == gotobol || func == gotoeol || func == backpage || func == forwpage;
}

int viblock_handle_key(int c, int f, int n) {
    char text[NSTRING];
    fn_t func;
    int status;

    if (!current_strategy) return FALSE;

    switch (c) {
    case 0x0D: case 0x0A: case CONTROL | 'M':
        if (block_mode == BLOCK_MODE_SET_NR) {
            current_strategy->apply(NULL);
            viblock_reset();
            return TRUE;
        }
        status = minibuf_input("viblock input: ", text, sizeof(text));
        if (status == TRUE) {
            if (!current_strategy->apply(text)) return FALSE;
            mlwrite("%s applied", current_strategy->name);
        } else {
            mlwrite("%s cancelled", current_strategy->name);
        }
        viblock_reset();
        return TRUE;
    case 0x1B: case CONTROL | 'G':
        mlwrite("%s cancelled", current_strategy->name);
        viblock_reset();
        curwp->w_flag |= WFHARD | WFMODE;
        return TRUE;
    default:
        func = getbind(c);
        if (!block_motion_allowed(func)) {
            viblock_render_status();
            return TRUE;
        }
        execute(c, f, n);
        curwp->w_flag |= WFHARD | WFMODE;
        viblock_render_status();
        return TRUE;
    }
}
