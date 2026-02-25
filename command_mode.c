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

#define CMD_BUF_SIZE 256

static int cmd_active = 0;
static char cmd_buffer[CMD_BUF_SIZE];
static int cmd_pos = 0;

/* Initialize command mode system */
void command_mode_init(void) {
    cmd_active = 0;
    cmd_buffer[0] = '\0';
    cmd_pos = 0;
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
    
    /* Show command prompt in status bar - this becomes atomic with the update */
    mlwrite("F1 Command: [number] goto line | Help");
    
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
    else {
        mlwrite("Unknown command: %s (try number, Help)", cmd_buffer);
    }
}

/* Handle key input in command mode */
int command_mode_handle_key(int c) {
    if (!cmd_active) return 0;
    
    switch (c) {
        case 0x0D: /* Enter */
        case 0x0A: /* LF */
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
    
    /* Show current command buffer in status bar */
    mlwrite("F1 Command: %s_", cmd_buffer);
    
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
}

/* Command mode activation wrapper for key binding */
int command_mode_activate_command(int f, int n) {
    command_mode_activate();
    return TRUE;
}
