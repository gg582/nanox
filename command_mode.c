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
    mlwrite("F1 Command: [number] goto line | Help | Sed s/pattern/replacement/");
    
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

/* Parse sed expression: s/pattern/replacement/flags */
static int parse_sed_expr(const char *expr, char **pattern, char **replacement, int *global) {
    const char *p = expr;
    char *pat_start, *pat_end, *repl_start, *repl_end;
    
    /* Skip leading whitespace and 'sed' keyword if present */
    while (*p && isspace(*p)) p++;
    if (strncasecmp(p, "sed", 3) == 0) p += 3;
    while (*p && isspace(*p)) p++;
    
    /* Must start with 's/' */
    if (*p != 's' || *(p+1) != '/') {
        mlwrite("Invalid sed syntax. Use: s/pattern/replacement/ or s/pattern/replacement/g");
        return FALSE;
    }
    p += 2; /* Skip 's/' */
    
    /* Extract pattern */
    pat_start = (char *)p;
    while (*p && *p != '/') {
        if (*p == '\\' && *(p+1)) p++; /* Skip escaped chars */
        p++;
    }
    if (*p != '/') {
        mlwrite("Invalid sed syntax: missing second '/'");
        return FALSE;
    }
    pat_end = (char *)p;
    p++; /* Skip '/' */
    
    /* Extract replacement */
    repl_start = (char *)p;
    while (*p && *p != '/') {
        if (*p == '\\' && *(p+1)) p++; /* Skip escaped chars */
        p++;
    }
    repl_end = (char *)p;
    
    /* Check for flags */
    *global = 0;
    if (*p == '/') {
        p++;
        while (*p) {
            if (*p == 'g') *global = 1;
            p++;
        }
    }
    
    /* Allocate and copy pattern */
    int pat_len = pat_end - pat_start;
    *pattern = malloc(pat_len + 1);
    if (!*pattern) return FALSE;
    strncpy(*pattern, pat_start, pat_len);
    (*pattern)[pat_len] = '\0';
    
    /* Allocate and copy replacement */
    int repl_len = repl_end - repl_start;
    *replacement = malloc(repl_len + 1);
    if (!*replacement) {
        free(*pattern);
        return FALSE;
    }
    strncpy(*replacement, repl_start, repl_len);
    (*replacement)[repl_len] = '\0';
    
    return TRUE;
}

/* Execute sed replace command with PCRE2 regex */
static void execute_sed(const char *sed_expr) {
    char *pattern = NULL;
    char *replacement = NULL;
    int global = 0;
    pcre2_code *re;
    int errorcode;
    PCRE2_SIZE erroroffset;
    int replace_count = 0;
    
    /* Parse the sed expression */
    if (!parse_sed_expr(sed_expr, &pattern, &replacement, &global)) {
        return;
    }
    
    /* Compile the regex pattern with PCRE2 */
    re = pcre2_compile(
        (PCRE2_SPTR)pattern,           /* pattern */
        PCRE2_ZERO_TERMINATED,         /* length (0-terminated) */
        PCRE2_UTF | PCRE2_MULTILINE,   /* options */
        &errorcode,                     /* error code */
        &erroroffset,                   /* error offset */
        NULL                            /* compile context */
    );
    
    if (!re) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
        mlwrite("Regex error at offset %d: %s", (int)erroroffset, buffer);
        free(pattern);
        free(replacement);
        return;
    }
    
    /* Create match data block */
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);
    if (!match_data) {
        mlwrite("Failed to create match data");
        pcre2_code_free(re);
        free(pattern);
        free(replacement);
        return;
    }
    
    /* Process each line in the buffer */
    struct line *lp;
    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        int orig_line_len = llength(lp);  /* Save original line length for deletion */
        int line_len = orig_line_len;
        if (line_len == 0) continue;
        
        char *line_text = malloc(line_len + 1);
        if (!line_text) continue;
        memcpy(line_text, lp->l_text, line_len);
        line_text[line_len] = '\0';
        
        PCRE2_SIZE offset = 0;
        int line_modified = 0;
        char *new_line = NULL;
        int new_len = 0;
        
        /* Find and replace matches in this line */
        while (offset < (PCRE2_SIZE)line_len) {
            int rc = pcre2_match(
                re,                         /* compiled pattern */
                (PCRE2_SPTR)line_text,     /* subject string */
                line_len,                   /* length of subject */
                offset,                     /* start offset */
                0,                          /* options */
                match_data,                 /* match data block */
                NULL                        /* match context */
            );
            
            if (rc < 0) {
                if (rc != PCRE2_ERROR_NOMATCH) {
                    mlwrite("PCRE2 match error: %d", rc);
                }
                break;
            }
            
            /* Get match offsets */
            PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
            PCRE2_SIZE match_start = ovector[0];
            PCRE2_SIZE match_end = ovector[1];
            
            /* Build new line with replacement */
            int before_len = (int)match_start;
            int after_start = (int)match_end;
            int after_len = line_len - (int)match_end;
            int total_new_len = new_len + before_len + strlen(replacement) + after_len;
            
            char *temp = realloc(new_line, total_new_len + 1);
            if (!temp) break;
            new_line = temp;
            
            /* Copy text before match (if first match, otherwise already copied) */
            if (new_len == 0) {
                memcpy(new_line, line_text, before_len);
                new_len = before_len;
            }
            
            /* Copy replacement */
            memcpy(new_line + new_len, replacement, strlen(replacement));
            new_len += strlen(replacement);
            
            /* Copy text after match */
            memcpy(new_line + new_len, line_text + after_start, after_len);
            new_len += after_len;
            new_line[new_len] = '\0';
            
            line_modified = 1;
            replace_count++;
            offset = match_end;
            
            /* Update line_text for next iteration */
            free(line_text);
            line_text = strdup(new_line);
            line_len = new_len;
            
            if (!global) break; /* Only replace first match if not global */
        }
        
        /* Replace the line if modified */
        if (line_modified && new_line) {
            
            /* Move cursor to the beginning of this line */
            curwp->w_dotp = lp;
            curwp->w_doto = 0;
            
            /* Delete entire ORIGINAL line content (use orig_line_len, not modified line_len) */
            if (orig_line_len > 0) {
                ldelete((long)orig_line_len, FALSE);
            }
            
            /* Insert new content at current position (beginning of line) */
            for (int i = 0; i < new_len; i++) {
                linsert(1, new_line[i]);
            }
            
            /* Update lp in case it was reallocated or merged */
            lp = curwp->w_dotp;
            
            /* Restore cursor position or adjust it */
            /* Keep cursor at the modified line for visual feedback */
            curwp->w_doto = 0;
            
            free(new_line);
        }
        
        free(line_text);
    }
    
    /* Cleanup */
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    free(pattern);
    free(replacement);
    
    /* Report results */
    if (replace_count > 0) {
        mlwrite("Replaced %d occurrence%s", replace_count, replace_count == 1 ? "" : "s");
        curbp->b_flag |= BFCHG; /* Mark buffer as changed */
    } else {
        mlwrite("No matches found");
    }
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
    /* Check for Sed command */
    else if (strncasecmp(cmd_buffer, "sed ", 4) == 0 || strncasecmp(cmd_buffer, "s/", 2) == 0) {
        execute_sed(cmd_buffer);
    }
    else {
        mlwrite("Unknown command: %s (try number, Help, or Sed s/pat/repl/)", cmd_buffer);
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

/* F6 Sed Replace - interactive sed-style regex replace with minibuffer */
int sed_replace_command(int f, int n) {
    char sed_expr[256];
    int status;
    
    /* Check for read-only mode */
    if (curbp->b_mode & MDVIEW)
        return rdonly();
    
    /* Get input using minibuffer */
    status = minibuf_input("Sed replace (s/pattern/replacement/[g]): ", sed_expr, sizeof(sed_expr));
    if (status != TRUE)
        return status;
    
    /* Execute the sed replace */
    execute_sed(sed_expr);
    return TRUE;
}
