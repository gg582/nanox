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
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "utf8.h"

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

/* Display paste slot window - UTF-8 compatible */
void paste_slot_display(void)
{
    extern void mlwrite(const char *fmt, ...);
    int col;
    char *content = paste_slot_buffer;
    int content_len = paste_slot_size;
    int current_row = 0;
    int byte_pos = 0;
    
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
    
    /* Display content with UTF-8 awareness */
    current_row = 3;
    col = 1;
    
    while (byte_pos < content_len && current_row < term.t_nrow - 1) {
        /* Start new line */
        movecursor(current_row, 0);
        TTputc('|');
        col = 1;
        
        /* Display characters until newline or end of line */
        while (byte_pos < content_len && col < term.t_ncol - 1) {
            char c = content[byte_pos];
            
            if (c == '\n' || c == '\r') {
                /* Handle newline */
                if (c == '\r' && byte_pos + 1 < content_len && content[byte_pos + 1] == '\n')
                    byte_pos++;  /* Skip \r in \r\n */
                byte_pos++;
                break;
            } else {
                /* Display UTF-8 character */
                unicode_t uc;
                int bytes = utf8_to_unicode((unsigned char *)content, byte_pos, content_len, &uc);
                
                /* Output the UTF-8 bytes */
                for (int b = 0; b < bytes && byte_pos + b < content_len; b++) {
                    TTputc((unsigned char)content[byte_pos + b]);
                }
                
                byte_pos += bytes;
                col++;  /* Simplified: assume 1 column per character */
            }
        }
        
        /* Fill rest of line with spaces */
        while (col < term.t_ncol - 1) {
            TTputc(' ');
            col++;
        }
        
        movecursor(current_row, term.t_ncol - 1);
        TTputc('|');
        current_row++;
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
    mlwrite("Press 'p' or Enter to paste, ESC to cancel");
    
    TTflush();
}

/* Insert paste slot content into document without auto-indent - UTF-8 compatible */
int paste_slot_insert(void)
{
    extern int linsert(int n, int c);
    extern int lnewline(void);
    
    if (paste_slot_buffer == NULL || paste_slot_size == 0)
        return 1;
    
    /* Insert content with UTF-8 awareness */
    int byte_pos = 0;
    while (byte_pos < paste_slot_size) {
        char c = paste_slot_buffer[byte_pos];
        
        if (c == '\r') {
            if (lnewline() == 0)
                return 0;
            byte_pos++;
            /* Skip \n if present */
            if (byte_pos < paste_slot_size && paste_slot_buffer[byte_pos] == '\n')
                byte_pos++;
        } else if (c == '\n') {
            if (lnewline() == 0)
                return 0;
            byte_pos++;
        } else {
            /* Insert UTF-8 character byte by byte */
            unicode_t uc;
            int bytes = utf8_to_unicode((unsigned char *)paste_slot_buffer, byte_pos, paste_slot_size, &uc);
            
            /* Insert each byte of the UTF-8 sequence */
            for (int b = 0; b < bytes; b++) {
                if (linsert(1, (unsigned char)paste_slot_buffer[byte_pos + b]) == 0)
                    return 0;
            }
            
            byte_pos += bytes;
        }
    }
    
    return 1;
}
