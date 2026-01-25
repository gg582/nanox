/*
 * paste_slot.c - Paste preview/edit window implementation
 * 
 * This module implements a paste slot window that allows users to preview
 * and edit pasted content before inserting it into the document.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

/* Paste slot buffer */
static char *paste_slot_buffer = NULL;
static int paste_slot_size = 0;
static int paste_slot_capacity = 0;
static int paste_slot_active = 0;

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
    if (paste_slot_size + 1 >= paste_slot_capacity) {
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

/* Display paste slot window */
void paste_slot_display(void)
{
    extern void mlwrite(const char *fmt, ...);
    int row, col;
    int max_rows = term.t_nrow - 4;  /* Leave room for border and status */
    int max_cols = term.t_ncol - 4;
    char *content = paste_slot_buffer;
    int content_len = paste_slot_size;
    int line_start = 0;
    int current_row = 0;
    
    /* Clear screen */
    movecursor(0, 0);
    TTeeol();
    
    /* Draw border */
    movecursor(0, 0);
    TTputc('+');
    for (col = 1; col < term.t_ncol - 1; col++)
        TTputc('-');
    TTputc('+');
    
    movecursor(1, 0);
    TTputc('|');
    movecursor(1, 1);
    mlwrite(" PASTE SLOT ");
    movecursor(1, term.t_ncol - 1);
    TTputc('|');
    
    movecursor(2, 0);
    TTputc('+');
    for (col = 1; col < term.t_ncol - 1; col++)
        TTputc('-');
    TTputc('+');
    
    /* Display content */
    current_row = 3;
    for (int i = 0; i < content_len && current_row < term.t_nrow - 1; i++) {
        if (i == line_start || i == 0) {
            movecursor(current_row, 0);
            TTputc('|');
            movecursor(current_row, 1);
        }
        
        char c = content[i];
        if (c == '\n' || c == '\r') {
            /* Fill rest of line */
            while (col < term.t_ncol - 1) {
                TTputc(' ');
                col++;
            }
            movecursor(current_row, term.t_ncol - 1);
            TTputc('|');
            current_row++;
            line_start = i + 1;
            if (c == '\r' && i + 1 < content_len && content[i + 1] == '\n')
                i++;  /* Skip \r\n */
        } else {
            TTputc(c);
        }
    }
    
    /* Fill remaining lines */
    while (current_row < term.t_nrow - 1) {
        movecursor(current_row, 0);
        TTputc('|');
        for (col = 1; col < term.t_ncol - 1; col++)
            TTputc(' ');
        TTputc('|');
        current_row++;
    }
    
    /* Bottom border */
    movecursor(term.t_nrow - 1, 0);
    TTputc('+');
    for (col = 1; col < term.t_ncol - 1; col++)
        TTputc('-');
    TTputc('+');
    
    /* Show instructions in status bar */
    mlwrite("Ctrl+Shift+P to paste, ESC to cancel");
    
    TTflush();
}

/* Insert paste slot content into document without auto-indent */
int paste_slot_insert(void)
{
    extern int linsert(int n, int c);
    extern int lnewline(void);
    
    if (paste_slot_buffer == NULL || paste_slot_size == 0)
        return 1;
    
    /* Insert content byte by byte */
    for (int i = 0; i < paste_slot_size; i++) {
        char c = paste_slot_buffer[i];
        
        if (c == '\r') {
            if (lnewline() == 0)
                return 0;
            /* Skip \n if present */
            if (i + 1 < paste_slot_size && paste_slot_buffer[i + 1] == '\n')
                i++;
        } else if (c == '\n') {
            if (lnewline() == 0)
                return 0;
        } else {
            if (linsert(1, (unsigned char)c) == 0)
                return 0;
        }
    }
    
    return 1;
}
