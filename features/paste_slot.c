/*
 * paste_slot.c - Paste preview/edit window implementation
 * 
 * This module implements a paste slot window that allows users to preview
 * and edit pasted content before inserting it into the document.
 * 
 * UTF-8 compatible implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "highlight.h"
#include "utf8.h"
#include "util.h"
#include "video.h"

extern void vtputc(int c);

/* Paste slot buffer */
static char *paste_slot_buffer = NULL;
static int paste_slot_size = 0;
static int paste_slot_capacity = 0;
static int paste_slot_active = 0;

static int paste_slot_decode(const char *content, int byte_pos, int content_len,
                             unicode_t *uc)
{
    int bytes;

    if (!content || !uc || byte_pos < 0 || byte_pos >= content_len)
        return 0;

    bytes = utf8_to_unicode((unsigned char *)content, byte_pos, content_len, uc);
    if (bytes <= 0 || byte_pos + bytes > content_len) {
        *uc = (unsigned char)content[byte_pos];
        return 1;
    }

    return bytes;
}

static void paste_slot_set_cell(struct video *vp, int col, unicode_t ch,
                                HighlightStyle style)
{
    vp->v_text[col].ch = ch;
    vp->v_text[col].fg = style.fg;
    vp->v_text[col].bg = style.bg;
    vp->v_text[col].bold = style.bold;
    vp->v_text[col].underline = style.underline;
    vp->v_text[col].italic = style.italic;
}

static void paste_slot_puts(struct video *vp, int *col, int max_col,
                            const char *text, HighlightStyle style)
{
    int idx = 0;
    int len;

    if (!vp || !col || !text || max_col <= 0)
        return;

    len = (int)strlen(text);
    while (idx < len && *col < max_col) {
        unicode_t uc;
        int bytes = paste_slot_decode(text, idx, len, &uc);
        if (bytes <= 0)
            break;
        paste_slot_set_cell(vp, *col, uc, style);
        (*col)++;
        idx += bytes;
    }
}

static int paste_slot_display_colored(void)
{
    extern int updupd(int force);
    extern struct video **vscreen;

    if (!term || !vscreen || !highlight_is_enabled())
        return 0;
    if (term->t_nrow < 4 || term->t_ncol < 8)
        return 0;

    HighlightStyle normal = colorscheme_get(HL_NORMAL);
    HighlightStyle border = colorscheme_get(HL_COMMENT);
    HighlightStyle green = normal;
    green.fg = 2;
    green.bold = true;

    int rows = term->t_nrow;
    int cols = term->t_ncol;

    for (int r = 0; r < rows; r++) {
        struct video *vp = vscreen[r];
        if (!vp)
            return 0;
        for (int c = 0; c < cols; c++)
            paste_slot_set_cell(vp, c, ' ', normal);
        vp->v_flag |= VFCHG | VFCOL;
    }

    for (int c = 0; c < cols; c++) {
        paste_slot_set_cell(vscreen[0], c, '-', border);
        paste_slot_set_cell(vscreen[rows - 2], c, '-', border);
    }

    int col = 2;
    paste_slot_puts(vscreen[0], &col, cols - 1, "Paste Preview", green);

    const char *content = paste_slot_buffer ? paste_slot_buffer : "";
    int content_len = paste_slot_size;
    int byte_pos = 0;
    int row = 2;

    while (byte_pos < content_len && row < rows - 2) {
        col = 2;
        while (byte_pos < content_len && col < cols - 2) {
            unsigned char c = (unsigned char)content[byte_pos];

            if (c == '\n' || c == '\r') {
                if (c == '\r' && byte_pos + 1 < content_len &&
                    content[byte_pos + 1] == '\n')
                    byte_pos++;
                byte_pos++;
                break;
            }

            unicode_t uc;
            int bytes = paste_slot_decode(content, byte_pos, content_len, &uc);
            int width = mystrnlen_raw_w(uc);
            if (bytes <= 0)
                break;
            if (width <= 0)
                width = 1;
            if (col + width > cols - 2)
                break;

            paste_slot_set_cell(vscreen[row], col, uc, green);
            col += width;
            byte_pos += bytes;
        }
        row++;
    }

    col = 2;
    paste_slot_puts(vscreen[rows - 1], &col, cols - 1,
                    "Press 'p' to paste, ESC/Ctrl+G/BS to cancel", green);

    updupd(TRUE);
    TTflush();
    return 1;
}

/* Initialize paste slot buffer */
void paste_slot_init(void)
{
    if (paste_slot_buffer == NULL) {
        paste_slot_capacity = 16384;  /* Start with 16KB */
        paste_slot_buffer = malloc(paste_slot_capacity);
        if (paste_slot_buffer) {
            paste_slot_size = 0;
            paste_slot_buffer[0] = '\0';
        }
    }
}

/* Add character to paste slot buffer */
int paste_slot_add_char(char c)
{
    if (paste_slot_buffer == NULL)
        paste_slot_init();
    
    if (paste_slot_buffer == NULL)
        return 0;
    
    /* Expand if needed */
    if (paste_slot_size < 0 || paste_slot_capacity <= 0)
        return 0;

    if (paste_slot_size + 1 >= paste_slot_capacity) {
        if (paste_slot_capacity > INT_MAX / 2)
            return 0;
        int new_capacity = paste_slot_capacity * 2;
        char *new_buffer = realloc(paste_slot_buffer, new_capacity);
        if (new_buffer == NULL)
            return 0;
        paste_slot_buffer = new_buffer;
        paste_slot_capacity = new_capacity;
    }
    
    paste_slot_buffer[paste_slot_size++] = c;
    paste_slot_buffer[paste_slot_size] = '\0';
    return 1;
}

/* Clear paste slot buffer */
void paste_slot_clear(void)
{
    if (paste_slot_buffer) {
        paste_slot_size = 0;
        paste_slot_buffer[0] = '\0';
    }
}

/* Free paste slot buffer */
void paste_slot_free(void)
{
    if (paste_slot_buffer) {
        free(paste_slot_buffer);
        paste_slot_buffer = NULL;
        paste_slot_size = 0;
        paste_slot_capacity = 0;
    }
}

/* Get paste slot content */
char *paste_slot_get_content(void)
{
    return paste_slot_buffer;
}

/* Get paste slot size */
int paste_slot_get_size(void)
{
    return paste_slot_size;
}

/* Set paste slot active state */
void paste_slot_set_active(int active)
{
    paste_slot_active = active;
}

/* Check if paste slot is active */
int paste_slot_is_active(void)
{
    return paste_slot_active;
}

/* Display paste slot window - UTF-8 compatible */
void paste_slot_display(void)
{
    extern void mlwrite(const char *fmt, ...);
    extern void vtmove(int row, int col);
    extern int updupd(int force);
    extern struct video **vscreen;

    int col;
    char *content = paste_slot_buffer ? paste_slot_buffer : "";
    int content_len = paste_slot_size;
    int current_row = 0;
    int byte_pos = 0;
    char *title = " PASTE SLOT ";

    if (paste_slot_display_colored())
        return;

    if (!term || !vscreen || term->t_nrow < 4 || term->t_ncol < 8)
        return;

    if (content_len < 0)
        content_len = 0;

    /* Clear screen and set change flags for all rows */
    for (int i = 0; i < term->t_nrow; i++) {
        if (!vscreen[i])
            return;
        vscreen[i]->v_flag |= VFCHG;
        vtmove(i, 0);
        extern void vteeol(void);
        vteeol();
    }

    /* Draw Top Border */
    vtmove(0, 0);
    vtputc('+');
    for (col = 1; col < term->t_ncol - 1; col++)
        vtputc('-');
    vtputc('+');

    /* Draw Header Line with Title */
    vtmove(1, 0);
    vtputc('|');
    vtmove(1, 2);
    while (*title) {
        vtputc(*title++);
    }
    vtmove(1, term->t_ncol - 1);
    vtputc('|');

    /* Draw Middle Border */
    vtmove(2, 0);
    vtputc('+');
    for (col = 1; col < term->t_ncol - 1; col++)
        vtputc('-');
    vtputc('+');

    /* Display content with UTF-8 awareness */
    current_row = 3;
    while (byte_pos < content_len && current_row < term->t_nrow - 1) {
        vtmove(current_row, 0);
        vtputc('|');
        col = 1;

        while (byte_pos < content_len && col < term->t_ncol - 1) {
            unsigned char c = (unsigned char)content[byte_pos];

            if (c == '\n' || c == '\r') {
                if (c == '\r' && byte_pos + 1 < content_len && content[byte_pos + 1] == '\n')
                    byte_pos++;
                byte_pos++;
                break;
            } else {
                unicode_t uc;
                int bytes = paste_slot_decode(content, byte_pos, content_len, &uc);
                int width = mystrnlen_raw_w(uc);
                if (bytes <= 0)
                    break;
                if (width <= 0)
                    width = 1;

                /* Render decoded unicode character to virtual screen */
                vtputc(uc);

                byte_pos += bytes;
                col += width;
            }
        }

        /* Fill rest of the line with spaces and draw right border */
        while (col < term->t_ncol - 1) {
            vtputc(' ');
            col++;
        }
        vtmove(current_row, term->t_ncol - 1);
        vtputc('|');
        current_row++;
    }

    /* Fill remaining empty rows within the box */
    while (current_row < term->t_nrow - 1) {
        vtmove(current_row, 0);
        vtputc('|');
        for (col = 1; col < term->t_ncol - 1; col++)
            vtputc(' ');
        vtmove(current_row, term->t_ncol - 1);
        vtputc('|');
        current_row++;
    }

    /* Draw Bottom Border */
    vtmove(term->t_nrow - 1, 0);
    vtputc('+');
    for (col = 1; col < term->t_ncol - 1; col++)
        vtputc('-');
    vtputc('+');

    /* Push virtual screen to physical terminal */
    updupd(TRUE);

    /* Show instructions on the last line */
    mlwrite("Press 'p' to paste, ESC/Ctrl+G/BS to cancel");

    TTflush();
}

/* Insert paste slot content into document without auto-indent - UTF-8 compatible */
int paste_slot_insert(void)
{
    extern int linsert_block(const char *block, int len);
    
    if (paste_slot_buffer == NULL || paste_slot_size <= 0)
        return 1;

    mlwrite("Pasting...");
    
    return linsert_block(paste_slot_buffer, paste_slot_size);
}
