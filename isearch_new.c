/*  isearch.c
 *
 * Full Minibuffer Window Implementation for UTF-8 ISearch
 * 
 * This implementation provides:
 * - Dedicated 1-line minibuffer window/buffer at term.t_nrow
 * - Cloned update() and updateline() logic for minibuffer rendering
 * - Native linsert()/ldelete() for all string manipulations
 * - UTF-8 gate buffer for proper character completion
 * - Sign-extension masking with TTputc((unsigned char)c & 0xFF)
 * - CJK width tracking via mystrnlen_raw_w
 */

#include <stdio.h>
#include <string.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "util.h"

extern struct terminal term;

/* ====================================================================
 * MINIBUFFER WINDOW SYSTEM
 * ==================================================================== */

/* Minibuffer dedicated structures */
static struct window *minibuf_wp = NULL;    /* Minibuffer window */
static struct buffer *minibuf_bp = NULL;    /* Minibuffer buffer */

/* UTF-8 input gate buffer */
#define GATE_BUF_SIZE 8
static unsigned char gate_buf[GATE_BUF_SIZE];
static int gate_len = 0;

/* Initialize the minibuffer window and buffer system */
static void minibuf_init(void)
{
    struct line *lp;
    
    if (minibuf_bp != NULL)
        return;  /* Already initialized */
    
    /* Create minibuffer buffer */
    minibuf_bp = (struct buffer *)malloc(sizeof(struct buffer));
    if (minibuf_bp == NULL)
        return;
    
    /* Initialize buffer header line */
    lp = lalloc(0);
    if (lp == NULL) {
        free(minibuf_bp);
        minibuf_bp = NULL;
        return;
    }
    
    /* Setup circular line list */
    lp->l_fp = lp;
    lp->l_bp = lp;
    
    /* Initialize buffer structure */
    minibuf_bp->b_bufp = NULL;
    minibuf_bp->b_dotp = lp;
    minibuf_bp->b_doto = 0;
    minibuf_bp->b_markp = lp;
    minibuf_bp->b_marko = 0;
    minibuf_bp->b_linep = lp;
    minibuf_bp->b_mode = 0;
    minibuf_bp->b_active = TRUE;
    minibuf_bp->b_nwnd = 1;
    minibuf_bp->b_flag = 0;
    strcpy(minibuf_bp->b_fname, "");
    strcpy(minibuf_bp->b_bname, "*minibuf*");
    
    /* Create minibuffer window */
    minibuf_wp = (struct window *)malloc(sizeof(struct window));
    if (minibuf_wp == NULL) {
        lfree(lp);
        free(minibuf_bp);
        minibuf_bp = NULL;
        return;
    }
    
    /* Initialize window structure */
    minibuf_wp->w_bufp = minibuf_bp;
    minibuf_wp->w_linep = lp;
    minibuf_wp->w_dotp = lp;
    minibuf_wp->w_doto = 0;
    minibuf_wp->w_markp = lp;
    minibuf_wp->w_marko = 0;
    minibuf_wp->w_force = 0;
    minibuf_wp->w_flag = 0;
    
    /* Reset gate buffer */
    gate_len = 0;
}

/* Clear minibuffer content */
static void minibuf_clear(void)
{
    if (minibuf_bp == NULL || minibuf_wp == NULL)
        return;
    
    /* Reset to empty first line */
    struct line *lp = minibuf_bp->b_linep;
    struct line *next;
    
    /* Free all content lines */
    lp = lforw(lp);
    while (lp != minibuf_bp->b_linep) {
        next = lforw(lp);
        /* Clear line content */
        lp->l_used = 0;
        lp = next;
    }
    
    /* Reset position to start */
    minibuf_wp->w_dotp = minibuf_bp->b_linep;
    minibuf_wp->w_doto = 0;
    minibuf_bp->b_dotp = minibuf_bp->b_linep;
    minibuf_bp->b_doto = 0;
    
    gate_len = 0;
}

/* Insert a UTF-8 character into minibuffer using linsert() */
static int minibuf_insert_char(unicode_t c)
{
    struct window *save_wp = curwp;
    struct buffer *save_bp = curbp;
    int result;
    
    /* Switch to minibuffer context */
    curwp = minibuf_wp;
    curbp = minibuf_bp;
    
    /* Use native linsert() for insertion */
    result = linsert(1, c);
    
    /* Restore original context */
    curwp = save_wp;
    curbp = save_bp;
    
    return result;
}

/* Delete characters from minibuffer using ldelete() */
static int minibuf_delete_char(long n)
{
    struct window *save_wp = curwp;
    struct buffer *save_bp = curbp;
    int result;
    
    if (minibuf_wp->w_doto == 0 && minibuf_wp->w_dotp == minibuf_bp->b_linep)
        return FALSE;  /* At beginning */
    
    /* Move back one UTF-8 character */
    if (minibuf_wp->w_doto > 0) {
        int byte_offset = minibuf_wp->w_doto;
        unsigned char *text = minibuf_wp->w_dotp->l_text;
        
        /* Move back to find UTF-8 character boundary */
        while (byte_offset > 0 && !is_beginning_utf8(text[byte_offset - 1]))
            byte_offset--;
        
        if (byte_offset > 0)
            byte_offset--;
        
        int bytes_to_delete = minibuf_wp->w_doto - byte_offset;
        
        /* Switch to minibuffer context */
        curwp = minibuf_wp;
        curbp = minibuf_bp;
        
        /* Move cursor back */
        minibuf_wp->w_doto = byte_offset;
        
        /* Delete using ldelete() */
        result = ldelete((long)bytes_to_delete, FALSE);
        
        /* Restore original context */
        curwp = save_wp;
        curbp = save_bp;
        
        return result;
    }
    
    return FALSE;
}

/* Update minibuffer display - cloned from update() and updateline() */
static void minibuf_update(const char *prompt)
{
    int col = 0;
    int i, len;
    unicode_t c;
    unsigned char *text;
    struct line *lp;
    
    if (minibuf_wp == NULL || minibuf_bp == NULL)
        return;
    
    /* Move to bottom line */
    movecursor(term.t_nrow, 0);
    
    /* Output prompt - mask each byte to prevent sign extension */
    while (prompt && *prompt) {
        TTputc((unsigned char)*prompt & 0xFF);
        prompt++;
        col++;
    }
    
    /* Get minibuffer line */
    lp = minibuf_wp->w_dotp;
    if (lp == minibuf_bp->b_linep) {
        /* Empty buffer, just clear to EOL */
        TTeeol();
        TTflush();
        movecursor(term.t_nrow, col);
        TTflush();
        return;
    }
    
    /* Display buffer content with proper UTF-8 and CJK width handling */
    text = lp->l_text;
    len = lp->l_used;
    i = 0;
    
    while (i < len && col < term.t_ncol - 1) {
        int bytes = utf8_to_unicode(text, i, len, &c);
        
        if (bytes <= 0)
            break;
        
        /* Check display width using mystrnlen_raw_w for CJK */
        int char_width = mystrnlen_raw_w(c);
        
        if (col + char_width >= term.t_ncol - 1)
            break;
        
        /* Output each byte with sign-extension masking */
        for (int j = 0; j < bytes; j++) {
            TTputc((unsigned char)text[i + j] & 0xFF);
        }
        
        col += char_width;
        i += bytes;
    }
    
    /* Clear to end of line */
    TTeeol();
    
    /* Position cursor at correct column accounting for wide chars */
    int cursor_col = 0;
    if (prompt) {
        cursor_col = strlen(prompt);
    }
    
    /* Calculate cursor position with CJK width */
    i = 0;
    int byte_pos = minibuf_wp->w_doto;
    
    while (i < byte_pos && i < len) {
        int bytes = utf8_to_unicode(text, i, len, &c);
        if (bytes <= 0)
            break;
        cursor_col += mystrnlen_raw_w(c);
        i += bytes;
    }
    
    movecursor(term.t_nrow, cursor_col);
    TTflush();
}

/* Get minibuffer content as string */
static void minibuf_get_text(char *dest, int max_len)
{
    struct line *lp;
    int i;
    
    dest[0] = '\0';
    
    if (minibuf_bp == NULL || minibuf_wp == NULL)
        return;
    
    lp = minibuf_wp->w_dotp;
    if (lp == minibuf_bp->b_linep)
        return;
    
    /* Copy content */
    for (i = 0; i < lp->l_used && i < max_len - 1; i++) {
        dest[i] = lp->l_text[i];
    }
    dest[i] = '\0';
}

/* ====================================================================
 * UTF-8 INPUT GATE SYSTEM
 * ==================================================================== */

/* Check if we have a complete UTF-8 character in gate buffer */
static int gate_is_complete(void)
{
    if (gate_len == 0)
        return 0;
    
    unsigned char first = gate_buf[0];
    int expected_bytes;
    
    /* Determine expected UTF-8 sequence length */
    if ((first & 0x80) == 0) {
        expected_bytes = 1;  /* ASCII */
    } else if ((first & 0xE0) == 0xC0) {
        expected_bytes = 2;  /* 2-byte sequence */
    } else if ((first & 0xF0) == 0xE0) {
        expected_bytes = 3;  /* 3-byte sequence */
    } else if ((first & 0xF8) == 0xF0) {
        expected_bytes = 4;  /* 4-byte sequence */
    } else {
        /* Invalid UTF-8 start byte - treat as complete */
        return 1;
    }
    
    return (gate_len >= expected_bytes);
}

/* Add byte to gate buffer and try to commit complete UTF-8 character */
static int gate_add_byte(int byte)
{
    unicode_t uc;
    int bytes;
    
    /* Add byte to gate buffer */
    if (gate_len < GATE_BUF_SIZE) {
        gate_buf[gate_len++] = (unsigned char)byte;
    } else {
        /* Buffer overflow - reset and start over */
        gate_len = 0;
        gate_buf[gate_len++] = (unsigned char)byte;
    }
    
    /* Check if we have a complete UTF-8 character */
    if (gate_is_complete()) {
        /* Parse the UTF-8 sequence */
        bytes = utf8_to_unicode(gate_buf, 0, gate_len, &uc);
        
        if (bytes > 0 && bytes <= gate_len) {
            /* Valid UTF-8 character - commit to minibuffer */
            int result = minibuf_insert_char(uc);
            
            /* Clear gate buffer */
            gate_len = 0;
            
            return result;
        } else {
            /* Invalid sequence - insert as-is and reset */
            for (int i = 0; i < gate_len; i++) {
                minibuf_insert_char(gate_buf[i]);
            }
            gate_len = 0;
            return TRUE;
        }
    }
    
    /* Character not complete yet */
    return TRUE;
}

/* ====================================================================
 * ISEARCH IMPLEMENTATION
 * ==================================================================== */

static int (*saved_get_char)(void);
static int eaten_char = -1;

static int cmd_buff[CMDBUFLEN];
static int cmd_offset;
static int cmd_reexecute = -1;

/* Come here on the next term.t_getchar call */
static int uneat(void)
{
    int c;
    term.t_getchar = saved_get_char;
    c = eaten_char;
    eaten_char = -1;
    return c;
}

static void reeat(int c)
{
    if (eaten_char != -1)
        return;
    eaten_char = c;
    saved_get_char = term.t_getchar;
    term.t_getchar = uneat;
}

/* Get character for search */
static int get_char(void)
{
    int c;
    
    /* Re-executing? */
    if (cmd_reexecute >= 0) {
        if ((c = cmd_buff[cmd_reexecute++]) != 0)
            return c;
    }
    
    /* Real input mode */
    cmd_reexecute = -1;
    update(FALSE);
    
    if (cmd_offset >= CMDBUFLEN - 1) {
        mlwrite("? command too long");
        return metac;
    }
    
    c = get1key();
    cmd_buff[cmd_offset++] = c;
    cmd_buff[cmd_offset] = '\0';
    
    return c;
}

/* Subroutine to do incremental reverse search */
int risearch(int f, int n)
{
    struct line *curline;
    int curoff;
    
    curline = curwp->w_dotp;
    curoff = curwp->w_doto;
    
    backchar(TRUE, 1);
    
    if (!(isearch(f, -n))) {
        curwp->w_dotp = curline;
        curwp->w_doto = curoff;
        curwp->w_flag |= WFMOVE;
        update(FALSE);
        mlwrite("(search failed)");
        matchlen = strlen(pat);
    } else {
        mlerase();
    }
    
    matchlen = strlen(pat);
    return TRUE;
}

/* Forward incremental search */
int fisearch(int f, int n)
{
    struct line *curline;
    int curoff;
    
    curline = curwp->w_dotp;
    curoff = curwp->w_doto;
    
    if (!(isearch(f, n))) {
        curwp->w_dotp = curline;
        curwp->w_doto = curoff;
        curwp->w_flag |= WFMOVE;
        update(FALSE);
        mlwrite("(search failed)");
        matchlen = strlen(pat);
    } else {
        mlerase();
    }
    
    matchlen = strlen(pat);
    return TRUE;
}

/* Main incremental search function using minibuffer */
int isearch(int f, int n)
{
    int status;
    int c;
    int expc;
    char pat_save[NPAT];
    struct line *curline;
    int curoff;
    int init_direction;
    
    /* Initialize minibuffer system */
    minibuf_init();
    if (minibuf_bp == NULL || minibuf_wp == NULL) {
        mlwrite("? Cannot initialize minibuffer");
        return FALSE;
    }
    
    /* Initialize search state */
    cmd_reexecute = -1;
    cmd_offset = 0;
    cmd_buff[0] = '\0';
    strncpy(pat_save, pat, NPAT - 1);
    curline = curwp->w_dotp;
    curoff = curwp->w_doto;
    init_direction = n;
    
start_over:
    /* Clear and display minibuffer */
    minibuf_clear();
    gate_len = 0;
    
    const char *prompt = (n < 0) ? "I-Search backward: " : "I-Search forward: ";
    minibuf_update(prompt);
    
    status = TRUE;
    
    /* Get first character */
    c = ectoc(expc = get_char());
    
    /* Handle initial Control-S or Control-R */
    if ((c == IS_FORWARD) || (c == IS_REVERSE) || (expc == metac)) {
        /* Reuse old pattern */
        for (int i = 0; pat[i] != 0; i++) {
            minibuf_insert_char((unsigned char)pat[i]);
        }
        minibuf_update(prompt);
        
        if (c == IS_REVERSE) {
            n = -1;
            backchar(TRUE, 1);
        } else {
            n = 1;
        }
        
        status = scanmore(pat, n);
        c = ectoc(expc = get_char());
    }
    
    /* Main search loop */
    for (;;) {
        if (expc == metac) {
            status = scanmore(pat, n);
            update(FALSE);
            expc = get_char();
            c = ectoc(expc);
            continue;
        }
        
        switch (c) {
        case IS_ABORT:
            return FALSE;
            
        case IS_REVERSE:
        case IS_FORWARD:
            if (c == IS_REVERSE)
                n = -1;
            else
                n = 1;
            
            status = scanmore(pat, n);
            c = ectoc(expc = get_char());
            continue;
            
        case IS_NEWLINE:
        case '\n':
            /* Get final pattern from minibuffer */
            minibuf_get_text(pat, NPAT);
            return TRUE;
            
        case IS_QUOTE:
            c = ectoc(expc = get_char());
            break;
            
        case IS_TAB:
            break;
            
        case IS_BACKSP:
        case IS_RUBOUT:
            if (cmd_offset <= 1)
                return TRUE;
            
            --cmd_offset;
            cmd_buff[--cmd_offset] = '\0';
            
            /* Delete from minibuffer */
            minibuf_delete_char(1);
            minibuf_update(prompt);
            
            /* Restore and restart search */
            curwp->w_dotp = curline;
            curwp->w_doto = curoff;
            n = init_direction;
            strncpy(pat, pat_save, NPAT);
            cmd_reexecute = 0;
            goto start_over;
            
        default:
            if (c < ' ') {
                reeat(c);
                minibuf_get_text(pat, NPAT);
                return TRUE;
            }
        }
        
        /* Add character to search - use gate buffer for UTF-8 */
        if (c >= ' ' && c < 256) {
            /* Raw byte - add to gate buffer */
            gate_add_byte(c);
        } else {
            /* Unicode character from extended input */
            minibuf_insert_char(c);
        }
        
        /* Update display */
        minibuf_update(prompt);
        
        /* Get current search pattern from minibuffer */
        minibuf_get_text(pat, NPAT);
        
        if (strlen(pat) >= NPAT - 1) {
            mlwrite("? Search string too long");
            return TRUE;
        }
        
        /* Attempt search */
        if (!status) {
            TTputc(BELL);
            TTflush();
        } else if (!(status = checknext(pat[strlen(pat) - 1], pat, n))) {
            status = scanmore(pat, n);
        }
        
        c = ectoc(expc = get_char());
    }
}

/* Check if next character matches */
int checknext(char chr, char *patrn, int dir)
{
    struct line *curline;
    int curoff;
    int buffchar;
    int status;
    
    curline = curwp->w_dotp;
    curoff = curwp->w_doto;
    
    if (dir > 0) {
        if (curoff == llength(curline)) {
            curline = lforw(curline);
            if (curline == curbp->b_linep)
                return FALSE;
            curoff = 0;
            buffchar = '\n';
        } else {
            buffchar = lgetc(curline, curoff++);
        }
        
        if ((status = eq(buffchar, chr)) != 0) {
            curwp->w_dotp = curline;
            curwp->w_doto = curoff;
            curwp->w_flag |= WFMOVE;
        }
        return status;
    } else {
        return match_pat(patrn);
    }
}

/* Search for next occurrence of pattern */
int scanmore(char *patrn, int dir)
{
    int sts;
    
    if (dir < 0) {
        rvstrcpy(tap, patrn);
        sts = scanner(tap, REVERSE, PTBEG);
    } else {
        sts = scanner(patrn, FORWARD, PTEND);
    }
    
    if (!sts) {
        TTputc(BELL);
        TTflush();
    }
    
    return sts;
}

/* Match pattern at current position */
int match_pat(char *patrn)
{
    int i;
    int buffchar;
    struct line *curline;
    int curoff;
    
    curline = curwp->w_dotp;
    curoff = curwp->w_doto;
    
    for (i = 0; i < strlen(patrn); i++) {
        if (curoff == llength(curline)) {
            curline = lforw(curline);
            curoff = 0;
            if (curline == curbp->b_linep)
                return FALSE;
            buffchar = '\n';
        } else {
            buffchar = lgetc(curline, curoff++);
        }
        
        if (!eq(buffchar, patrn[i]))
            return FALSE;
    }
    
    return TRUE;
}

/* Display prompt with old pattern */
int promptpattern(char *prompt)
{
    char tpat[NPAT + 20];
    
    strcpy(tpat, prompt);
    strcat(tpat, " (");
    expandp(pat, &tpat[strlen(tpat)], NPAT / 2);
    strcat(tpat, ")<Meta>: ");
    
    if (!clexec) {
        mlwrite(tpat);
    }
    
    return strlen(tpat);
}
