/*  random.c
 *
 *      This file contains the command processing functions for a number of
 *      random commands. There is no functional grouping here, for sure.
 *
 *  Modified by Petri Kutvonen
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "util.h"
#include "nanox.h"
#include "completion.h"

int tabsize;                    /* Tab size (0: use real tabs) */

int is_markdown_file(void) {
    if (!curbp->b_fname[0]) return FALSE;
    char *ext = strrchr(curbp->b_fname, '.');
    if (ext && (strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".markdown") == 0))
        return TRUE;
    return FALSE;
}

int is_markup_file(void) {
    if (!curbp->b_fname[0]) return FALSE;
    char *ext = strrchr(curbp->b_fname, '.');
    if (ext && (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".xml") == 0 || 
                strcasecmp(ext, ".xhtml") == 0 || strcasecmp(ext, ".svg") == 0))
        return TRUE;
    return FALSE;
}

static int get_indent(struct line *lp) {
    int nicol = 0;
    int i, c;
    for (i = 0; i < llength(lp); ++i) {
        c = lgetc(lp, i);
        if (c != ' ' && c != '\t')
            break;
        if (c == '\t')
            nicol = nextab(nicol);
        else
            ++nicol;
    }
    return nicol;
}

static void set_indent(int target) {
    int ch;
    int cur = get_indent(curwp->w_dotp);
    if (cur == target) {
        /* Already correct, just move to first non-white */
        curwp->w_doto = 0;
        while (curwp->w_doto < llength(curwp->w_dotp)) {
            ch = lgetc(curwp->w_dotp, curwp->w_doto);
            if (ch != ' ' && ch != '\t')
                break;
            curwp->w_doto++;
        }
        return;
    }

    curwp->w_doto = 0;
    while (curwp->w_doto < llength(curwp->w_dotp)) {
        ch = lgetc(curwp->w_dotp, curwp->w_doto);
        if (ch != ' ' && ch != '\t')
            break;
        ldelchar(1, FALSE);
    }

    if (target > 0) {
        if (nanox_cfg.soft_tab) {
            int i;
            for (i = 0; i < target; ++i)
                linsert(1, ' ');
        } else {
            int step = tab_width;
            if (step == 0) step = 1; /* Avoid division by zero */
            int num_tabs = target / step;
            int num_spaces = target % step;
            while (num_tabs--) linsert(1, '\t');
            while (num_spaces--) linsert(1, ' ');
        }
    }
    /* Move cursor to first non-whitespace character */
    curwp->w_doto = 0;
    while (curwp->w_doto < llength(curwp->w_dotp)) {
        int ch2 = lgetc(curwp->w_dotp, curwp->w_doto);
        if (ch2 != ' ' && ch2 != '\t') break;
        curwp->w_doto++;
    }
}

static int is_closing_block(struct line *lp) {
    int i, len;
    int c;
    char buffer[32];
    int buf_idx = 0;
    char *ext = strrchr(curbp->b_fname, '.');

    len = llength(lp);
    for (i = 0; i < len; ++i) {
        c = lgetc(lp, i);
        if (c != ' ' && c != '\t')
            break;
    }
    if (i == len) return FALSE;

    /* Check for single character closing pairs first */
    c = lgetc(lp, i);
    if (c == '}' || c == ')' || c == ']') return TRUE;

    /* Otherwise extract the first word */
    while (i < len && buf_idx < 31) {
        c = lgetc(lp, i);
        if (c == ' ' || c == '\t' || c == '\n' || c == '(' || c == '{' || c == '[' || c == ')' || c == '}' || c == ']')
            break;
        buffer[buf_idx++] = (char)c;
        i++;
    }
    buffer[buf_idx] = '\0';

    if (!ext) return FALSE;

    if (strcasecmp(ext, ".sh") == 0 || strcasecmp(ext, ".bash") == 0) {
        if (strcmp(buffer, "fi") == 0) return TRUE;
        if (strcmp(buffer, "done") == 0) return TRUE;
        if (strcmp(buffer, "esac") == 0) return TRUE;
        if (strcmp(buffer, "else") == 0) return TRUE;
        if (strcmp(buffer, "elif") == 0) return TRUE;
    } else if (strcasecmp(ext, ".py") == 0) {
        if (strcmp(buffer, "else") == 0) return TRUE;
        if (strcmp(buffer, "elif") == 0) return TRUE;
        if (strcmp(buffer, "except") == 0) return TRUE;
        if (strcmp(buffer, "finally") == 0) return TRUE;
    } else if (strcasecmp(ext, ".lua") == 0) {
        if (strcmp(buffer, "end") == 0) return TRUE;
        if (strcmp(buffer, "else") == 0) return TRUE;
        if (strcmp(buffer, "elseif") == 0) return TRUE;
        if (strcmp(buffer, "until") == 0) return TRUE;
    }

    /* Check for C preprocessor dedents */
    if (curbp->b_mode & MDCMOD) {
        if (strcmp(buffer, "#else") == 0 || strcmp(buffer, "#elif") == 0 || 
            strcmp(buffer, "#endif") == 0) return TRUE;
    }

    return FALSE;
}

static void check_indent_dedent(void) {
    struct line *lp = curwp->w_dotp;
    int target_indent = -1;
    struct line *scan_lp;

    if (!is_closing_block(lp)) return;

    /* Find the first non-white character to check for C braces */
    int i, len = llength(lp);
    for (i = 0; i < len; i++) {
        int c = lgetc(lp, i);
        if (c != ' ' && c != '\t') break;
    }
    int first_char = (i < len) ? lgetc(lp, i) : 0;

    /* If it's a closing brace in C mode, use matching brace logic */
    if ((curbp->b_mode & MDCMOD) && (first_char == '}' || first_char == ')' || first_char == ']')) {
        struct line *oldlp = curwp->w_dotp;
        int oldoff = curwp->w_doto;
        int count = 1;
        int oc;

        if (first_char == '}') oc = '{';
        else if (first_char == ')') oc = '(';
        else oc = '[';

        curwp->w_doto = i;
        while (backchar(FALSE, 1)) {
            int ch;
            if (curwp->w_doto == llength(curwp->w_dotp)) ch = '\n';
            else ch = lgetc(curwp->w_dotp, curwp->w_doto);

            if (ch == first_char) ++count;
            else if (ch == oc) --count;

            if (count == 0) {
                target_indent = get_indent(curwp->w_dotp);
                break;
            }
            if (boundry(curwp->w_dotp, curwp->w_doto, REVERSE)) break;
        }
        curwp->w_dotp = oldlp;
        curwp->w_doto = oldoff;
    } else {
        /* Handle keyword-based dedenting (e.g. fi, done, end) */
        char *ext = strrchr(curbp->b_fname, '.');
        if (ext && (strcasecmp(ext, ".sh") == 0 || strcasecmp(ext, ".bash") == 0)) {
            char word[32];
            int idx = 0;
            for (int k = i; k < len && idx < 31; k++) {
                int c = lgetc(lp, k);
                if (isspace(c)) break;
                word[idx++] = (char)c;
            }
            word[idx] = '\0';

            if (strcmp(word, "fi") == 0 || strcmp(word, "done") == 0 || strcmp(word, "esac") == 0 || strcmp(word, "else") == 0 || strcmp(word, "elif") == 0) {
                struct line *oldlp = curwp->w_dotp;
                int oldoff = curwp->w_doto;
                int count = 1;
                char *open_word = NULL;
                char *close_word = word;

                if (strcmp(word, "fi") == 0) open_word = "if";
                else if (strcmp(word, "done") == 0) open_word = "for"; /* could also be while/until */
                else if (strcmp(word, "esac") == 0) open_word = "case";
                else if (strcmp(word, "else") == 0 || strcmp(word, "elif") == 0) {
                    open_word = "if";
                    count = 1;
                }

                /* Simplified scan back for matching keyword */
                struct line *scan = lback(lp);
                while (scan != curbp->b_linep) {
                    int slen = llength(scan);
                    int si = 0;
                    while (si < slen && isspace(lgetc(scan, si))) si++;
                    if (si < slen) {
                        char sword[32];
                        int sidx = 0;
                        for (int k = si; k < slen && sidx < 31; k++) {
                            int sc = lgetc(scan, k);
                            if (isspace(sc)) break;
                            sword[sidx++] = (char)sc;
                        }
                        sword[sidx] = '\0';

                        if (open_word && strcmp(sword, open_word) == 0) {
                            if (--count == 0) {
                                target_indent = get_indent(scan);
                                break;
                            }
                        } else if (strcmp(sword, close_word) == 0) {
                            count++;
                        }
                    }
                    scan = lback(scan);
                }
                curwp->w_dotp = oldlp;
                curwp->w_doto = oldoff;
            }
        } else if (curbp->b_mode & MDCMOD) {
            /* Handle C preprocessor dedenting (#else, #elif, #endif) */
            char word[32];
            int idx = 0;
            for (int k = i; k < len && idx < 31; k++) {
                int c = lgetc(lp, k);
                if (isspace(c)) break;
                word[idx++] = (char)c;
            }
            word[idx] = '\0';

            if (strcmp(word, "#else") == 0 || strcmp(word, "#elif") == 0 || strcmp(word, "#endif") == 0) {
                int count = 1;
                struct line *scan = lback(lp);
                while (scan != curbp->b_linep) {
                    int slen = llength(scan);
                    int si = 0;
                    while (si < slen && isspace(lgetc(scan, si))) si++;
                    if (si < slen) {
                        char sword[32];
                        int sidx = 0;
                        for (int k = si; k < slen && sidx < 31; k++) {
                            int sc = lgetc(scan, k);
                            if (isspace(sc)) break;
                            sword[sidx++] = (char)sc;
                        }
                        sword[sidx] = '\0';

                        if (strcmp(sword, "#if") == 0 || strcmp(sword, "#ifdef") == 0 || strcmp(sword, "#ifndef") == 0) {
                            if (--count == 0) {
                                target_indent = get_indent(scan);
                                break;
                            }
                        } else if (strcmp(sword, "#endif") == 0) {
                            count++;
                        }
                    }
                    scan = lback(scan);
                }
            }
        }
    }

    if (target_indent < 0) {
        /* For keywords like fi, done, esac, we scan back for matching if, do, case */
        scan_lp = lback(lp);
        while (scan_lp != curbp->b_linep) {
            int is_blank = TRUE;
            for (int k = 0; k < llength(scan_lp); k++) {
                int c = lgetc(scan_lp, k);
                if (c != ' ' && c != '\t') {
                    is_blank = FALSE;
                    break;
                }
            }

            if (!is_blank) {
                target_indent = get_indent(scan_lp);
                break;
            }
            scan_lp = lback(scan_lp);
        }
    }

    if (target_indent >= 0) {
        int cur_indent = get_indent(lp);
        if (cur_indent != target_indent) {
            int old_doto = curwp->w_doto;
            set_indent(target_indent);
            /* Dedenting can shrink the line, so clamp the cursor to the new end. */
            if (old_doto > llength(curwp->w_dotp))
                old_doto = llength(curwp->w_dotp);
            if (old_doto < 0)
                old_doto = 0;
            curwp->w_doto = old_doto;
        }
    }
}

int handle_markup_char(int c) {
    if (!nanox_cfg.use_auto_doc_completion || !is_markup_file())
        return TRUE;

    if (c == '>') {
        /* Auto-close tag: if we just typed '>', check if it's a start tag */
        struct line *lp = curwp->w_dotp;
        int off = curwp->w_doto;
        if (off <= 1) return TRUE;

        /* Look back for '<' */
        int scan = off - 2;
        while (scan >= 0 && lp->text[scan] != '<') {
            if (lp->text[scan] == '>') return TRUE; /* Already closed or nested? skip */
            scan--;
        }

        if (scan >= 0 && lp->text[scan] == '<') {
            /* Found start of a potential tag */
            int tag_start = scan + 1;
            if (tag_start >= off - 1) return TRUE; /* Empty tag? <> */
            if (lp->text[tag_start] == '/' || lp->text[tag_start] == '!' || lp->text[tag_start] == '?')
                return TRUE; /* Closing tag, comment, or PI */

            /* Extract tag name */
            char tag_name[64];
            int tidx = 0;
            int p = tag_start;
            while (p < off - 1 && tidx < 63 && !isspace(lp->text[p]) && lp->text[p] != '/') {
                tag_name[tidx++] = lp->text[p++];
            }
            tag_name[tidx] = '\0';

            /* Check if it's a self-closing tag or void element */
            if (lp->text[off-2] == '/') return TRUE;
            static const char *void_elements[] = {
                "area", "base", "br", "col", "embed", "hr", "img", "input",
                "link", "meta", "param", "source", "track", "wbr", NULL
            };
            if (is_markup_file()) { /* More specific check could be done for HTML vs XML */
                for (int v = 0; void_elements[v]; v++) {
                    if (strcasecmp(tag_name, void_elements[v]) == 0) return TRUE;
                }
            }

            /* Insert closing tag */
            char close_tag[128];
            snprintf(close_tag, sizeof(close_tag), "</%s>", tag_name);
            linstr(close_tag);
            /* Move cursor back to be between tags */
            backchar(FALSE, (int)strlen(close_tag));
        }
    } else if (c == '/') {
        /* Auto-complete closing tag on '</' */
        struct line *lp = curwp->w_dotp;
        int off = curwp->w_doto;
        if (off >= 2 && lp->text[off-2] == '<') {
            /* User just typed '</' */
            /* Scan back for the last unclosed tag */
            /* (Simplistic scan for now: just find the previous start tag) */
            /* In a real implementation we might want a proper stack-based scanner */
            
            /* Delete the '/' we just typed if we want to replace it, 
             * but actually standard behavior is to complete after '</'
             */
             /* For now let's just do nothing or minimal completion */
        }
    }
    return TRUE;
}

/*
 * Set fill column to n.
 */
int setfillcol(int f, int n)
{
    fillcol = n;
    mlwrite("(Fill column is %d)", n);
    return TRUE;
}

/*
 * Display the current position of the cursor, in origin 1 X-Y coordinates,
 * the character that is under the cursor (in hex), and the fraction of the
 * text that is before the cursor. The displayed column is not the current
 * column, but the column that would be used on an infinite width display.
 * Normally this is bound to "C-X =".
 */
int showcpos(int f, int n)
{
    struct line *lp;            /* current line */
    long numchars;              /* # of chars in file */
    int numlines;               /* # of lines in file */
    long predchars;             /* # chars preceding point */
    int predlines;              /* # lines preceding point */
    int curchar;                /* character under cursor */
    int ratio;
    int col;
    int savepos;                /* temp save for current offset */
    int ecol;               /* column pos/end of current line */

    /* starting at the beginning of the buffer */
    lp = lforw(curbp->b_linep);

    /* start counting chars and lines */
    numchars = 0;
    numlines = 0;
    predchars = 0;
    predlines = 0;
    curchar = 0;
    while (lp != curbp->b_linep) {
        /* if we are on the current line, record it */
        if (lp == curwp->w_dotp) {
            predlines = numlines;
            predchars = numchars + curwp->w_doto;
            if ((curwp->w_doto) == llength(lp))
                curchar = '\n';
            else
                curchar = lgetc(lp, curwp->w_doto);
        }
        /* on to the next line */
        ++numlines;
        numchars += llength(lp) + 1;
        lp = lforw(lp);
    }

    /* if at end of file, record it */
    if (curwp->w_dotp == curbp->b_linep) {
        predlines = numlines;
        predchars = numchars;
        curchar = 0;
    }

    /* Get real column and end-of-line column. */
    col = getccol(FALSE);
    savepos = curwp->w_doto;
    curwp->w_doto = llength(curwp->w_dotp);
    ecol = getccol(FALSE);
    curwp->w_doto = savepos;

    ratio = 0;              /* Ratio before dot. */
    if (numchars != 0)
        ratio = (100L * predchars) / numchars;

    /* summarize and report the info */
    mlwrite("Line %d/%d Col %d/%d Char %D/%D (%d%%) char = 0x%x",
        predlines + 1, numlines + 1, col, ecol, predchars, numchars, ratio, curchar);
    return TRUE;
}

int getcline(void)
{                       /* get the current line number */
    struct line *lp;            /* current line */
    int numlines;               /* # of lines before point */

    /* starting at the beginning of the buffer */
    lp = lforw(curbp->b_linep);

    /* start counting lines */
    numlines = 0;
    while (lp != curbp->b_linep) {
        /* if we are on the current line, record it */
        if (lp == curwp->w_dotp)
            break;
        ++numlines;
        lp = lforw(lp);
    }

    /* and return the resulting count */
    return numlines + 1;
}

/*
 * Return current column.  Stop at first non-blank given TRUE argument.
 *
 * See vtputc() for rough formatting of unicode characters. We show
 * control characters as multiple characters, the rest are given one
 * unicode slot each and assumed to show as a single fixed size char.
 */
int getccol(int bflg)
{
    int i, col;
    struct line *dlp = curwp->w_dotp;
    int byte_offset = curwp->w_doto;
    int len = llength(dlp);

    col = i = 0;
    while (i < byte_offset) {
        unicode_t c;

        i += utf8_to_unicode(dlp->text, i, len, &c);
        if (c != ' ' && c != '\t' && bflg)
            break;
        col = next_column(col, c, tab_width);
    }
    return col;
}

/*
 * Set current column.
 *
 * int pos;     position to set cursor
 */
int setccol(int pos)
{
    int i;                  /* index into current line */
    int col;                /* current cursor column   */
    int llen;               /* length of line in bytes */
    struct line *dlp = curwp->w_dotp;

    col = 0;
    llen = llength(dlp);

    /* scan the line until we are at or past the target column */
    i = 0;
    while (i < llen && col < pos) {
        unicode_t c;
        int bytes;

        /* advance one character */
        bytes = utf8_to_unicode(dlp->text, i, llen, &c);
        col = next_column(col, c, tab_width);
        i += bytes;
    }

    /* set us at the new position */
    curwp->w_doto = i;

    /* and tell whether we made it */
    return col >= pos;
}

/*
 * Twiddle the two characters on either side of dot. If dot is at the end of
 * the line twiddle the two characters before it. Return with an error if dot
 * is at the beginning of line; it seems to be a bit pointless to make this
 * work. This fixes up a very common typo with a single stroke. Normally bound
 * to "C-T". This always works within a line, so "WFEDIT" is good enough.
 */
int twiddle(int f, int n)
{
    struct line *dotp;
    int doto;
    unicode_t cr, cl;
    int len_r, len_l;
    int doto_l, doto_r;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    
    dotp = curwp->w_dotp;
    doto = curwp->w_doto;

    /* If at end of line, move back one char first */
    if (doto == llength(dotp)) {
        if (doto == 0) return FALSE; /* Empty line */
        do {
            doto--;
        } while (doto > 0 && !is_beginning_utf8((unsigned char)dotp->text[doto]));
    }
    
    doto_r = doto;
    if (doto_r == 0) return FALSE; /* At beginning of line */

    /* Find start of left char */
    doto_l = doto_r;
    do {
        doto_l--;
    } while (doto_l > 0 && !is_beginning_utf8((unsigned char)dotp->text[doto_l]));

    /* Get the two characters */
    len_l = utf8_to_unicode(dotp->text, doto_l, llength(dotp), &cl);
    len_r = utf8_to_unicode(dotp->text, doto_r, llength(dotp), &cr);

    /* Move to left position, delete both chars, and insert swapped */
    curwp->w_doto = doto_l;
    ldelete((long)(len_l + len_r), FALSE);
    
    linsert(1, cr);
    linsert(1, cl);

    return TRUE;
}

/*
 * Quote the next character, and insert it into the buffer. All the characters
 * are taken literally, with the exception of the newline, which always has
 * its line splitting meaning. The character is always read, even if it is
 * inserted 0 times, for regularity. Bound to "C-Q"
 */
int quote(int f, int n)
{
    int s;
    int c;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    c = tgetc();
    if (n < 0)
        return FALSE;
    if (n == 0)
        return TRUE;
    if (c == '\n') {
        do {
            s = lnewline();
        } while (s == TRUE && --n);
        return s;
    }
    return linsert(n, c);
}

/*
 * Set tab size if given non-default argument (n <> 1).  Otherwise, insert a
 * tab into file.  If given argument, n, of zero, change to true tabs.
 * If n > 1, simulate tab stop every n-characters using spaces. This has to be
 * done in this slightly funny way because the tab (in ASCII) has been turned
 * into "C-I" (in 10 bit code) already. Bound to "C-I".
 */
int insert_tab(int f, int n)
{
    if (n < 0)
        return FALSE;

    /* If an indent/outdent range is active, commit it now (modern editor style) */
    if (indent_selection_active && indent_start_lp != NULL) {
        indent_end_lp = curwp->w_dotp;
        indent_selection_active = FALSE;
        return indent_apply_range(f, n);
    }
    /* If a fully-set range is pending, apply it */
    if (indent_start_lp != NULL && indent_end_lp != NULL) {
        return indent_apply_range(f, n);
    }

    /* Prefer completion when cursor is on a symbol/path prefix.
     * This enables TAB -> open candidates, TAB again -> focus list. */
    if (n == 1 && nanox_cfg.autocomplete) {
        int completion_handled = completion_try_at_cursor();
        if (completion_handled)
            return TRUE; /* candidates exist: open/drive completion UI */
        /* no candidates: continue and insert a normal tab/indent */
    }

    /* If we are in CMODE and at the beginning of the line, do smart re-indent */
    if ((curbp->b_mode & MDCMOD) != 0 && n == 1) {
        int i;
        int only_white = TRUE;
        for (i = 0; i < curwp->w_doto; i++) {
            int ch = lgetc(curwp->w_dotp, i);
            if (ch != ' ' && ch != '\t') {
                only_white = FALSE;
                break;
            }
        }
        if (only_white) {
            struct line *lp = lback(curwp->w_dotp);
            int target = 0;
            while (lp != curbp->b_linep) {
                int is_blank = TRUE;
                for (int k = 0; k < llength(lp); k++) {
                    int ch2 = lgetc(lp, k);
                    if (ch2 != ' ' && ch2 != '\t') {
                        is_blank = FALSE;
                        break;
                    }
                }
                if (!is_blank) {
                    target = get_indent(lp);
                    /* If previous line ends with {, increase indent */
                    int last_idx = llength(lp) - 1;
                    while (last_idx >= 0 && (lgetc(lp, last_idx) == ' ' || lgetc(lp, last_idx) == '\t'))
                        last_idx--;
                    if (last_idx >= 0 && lgetc(lp, last_idx) == '{')
                        target += (curbp->b_tabsize ? curbp->b_tabsize : tab_width);
                    break;
                }
                lp = lback(lp);
            }
            set_indent(target);
            return TRUE;
        }
    }

    if (n == 0 || n > 1) {
        curbp->b_tabsize = n;
        if (!(curbp->b_flag & BFMAKE))
            tabsize = n;
        return TRUE;
    }
    if (!curbp->b_tabsize)
        return linsert(1, '\t');
    return linsert(curbp->b_tabsize - (getccol(FALSE) % curbp->b_tabsize), ' ');
}

int completion_menu_command(int f, int n)
{
    (void)f;
    (void)n;
    if (!completion_try_at_cursor()) {
        TTbeep();
        return FALSE;
    }
    return TRUE;
}

/*
 * change tabs to spaces
 *
 * int f, n;        default flag and numeric repeat count
 */
int detab(int f, int n)
{
    int inc;                /* increment to next line [sgn(n)] */

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */

    if (f == FALSE)
        n = 1;

    /* loop thru detabbing n lines */
    inc = ((n > 0) ? 1 : -1);
    while (n) {
        curwp->w_doto = 0;      /* start at the beginning */

        /* detab the entire current line */
        while (curwp->w_doto < llength(curwp->w_dotp)) {
            /* if we have a tab */
            if (lgetc(curwp->w_dotp, curwp->w_doto) == '\t') {
                ldelchar(1, FALSE);
                        int step = tab_width;
        if (step == 0) step = 1;
        insspace(TRUE, step - (curwp->w_doto % step));
            }
            forwchar(FALSE, 1);
        }

        /* advance/or back to the next line */
        forwline(TRUE, inc);
        n -= inc;
    }
    curwp->w_doto = 0;          /* to the begining of the line */
    thisflag &= ~CFCPCN;            /* flag that this resets the goal column */
    lchange(WFEDIT);            /* yes, we have made at least an edit */
    return TRUE;
}

/*
 * change spaces to tabs where posible
 *
 * int f, n;        default flag and numeric repeat count
 */
int entab(int f, int n)
{
    int inc;                /* increment to next line [sgn(n)] */
    int fspace;             /* pointer to first space if in a run */
    int ccol;               /* current cursor column */
    char cchar;             /* current character */

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */

    if (f == FALSE)
        n = 1;

    /* loop thru entabbing n lines */
    inc = ((n > 0) ? 1 : -1);
    while (n) {
        curwp->w_doto = 0;      /* start at the beginning */

        /* entab the entire current line */
        fspace = -1;
        ccol = 0;
        while (curwp->w_doto < llength(curwp->w_dotp)) {
            /* see if it is time to compress */
            if ((fspace >= 0) && (nextab(fspace) <= ccol)) {
                if (ccol - fspace < 2)
                    fspace = -1;
                else {
                    /* there is a bug here dealing with mixed space/tabed
                       lines.......it will get fixed                */
                    backchar(TRUE, ccol - fspace);
                    ldelete((long)(ccol - fspace), FALSE);
                    linsert(1, '\t');
                    fspace = -1;
                }
            }

            /* get the current character */
            cchar = lgetc(curwp->w_dotp, curwp->w_doto);

            switch (cchar) {
            case '\t':      /* a tab...count em up */
                ccol = nextab(ccol);
                break;

            case ' ':       /* a space...compress? */
                if (fspace == -1)
                    fspace = ccol;
                ccol++;
                break;

            default:        /* any other char...just count */
                ccol++;
                fspace = -1;
                break;
            }
            forwchar(FALSE, 1);
        }

        /* advance/or back to the next line */
        forwline(TRUE, inc);
        n -= inc;
    }
    curwp->w_doto = 0;          /* to the begining of the line */
    thisflag &= ~CFCPCN;            /* flag that this resets the goal column */
    lchange(WFEDIT);            /* yes, we have made at least an edit */
    return TRUE;
}

/*
 * trim trailing whitespace from the point to eol
 *
 * int f, n;        default flag and numeric repeat count
 */
int trim(int f, int n)
{
    struct line *lp;            /* current line pointer */
    int offset;             /* original line offset position */
    int length;             /* current length */
    int inc;                /* increment to next line [sgn(n)] */

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */

    if (f == FALSE)
        n = 1;

    /* loop thru trimming n lines */
    inc = ((n > 0) ? 1 : -1);
    while (n) {
        lp = curwp->w_dotp;     /* find current line text */
        offset = curwp->w_doto;     /* save original offset */
        length = lp->used;        /* find current length */

        /* trim the current line */
        while (length > offset) {
            if (lgetc(lp, length - 1) != ' ' && lgetc(lp, length - 1) != '\t')
                break;
            length--;
        }
        lp->used = length;

        /* advance/or back to the next line */
        forwline(TRUE, inc);
        n -= inc;
    }
    lchange(WFEDIT);
    thisflag &= ~CFCPCN;            /* flag that this resets the goal column */
    return TRUE;
}

/*
 * Open up some blank space. The basic plan is to insert a bunch of newlines,
 * and then back up over them. Everything is done by the subcommand
 * procerssors. They even handle the looping. Normally this is bound to "C-O".
 */
int openline(int f, int n)
{
    int i;
    int s;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if (n < 0)
        return FALSE;
    if (n == 0)
        return TRUE;
    i = n;                  /* Insert newlines.     */
    do {
        s = lnewline();
    } while (s == TRUE && --i);
    if (s == TRUE)              /* Then back up overtop */
        s = backchar(f, n);     /* of them all.         */
    return s;
}

static int handle_markdown_list(void) {
    struct line *lp = curwp->w_dotp;
    int len = llength(lp);
    int i = 0;
    while (i < len && (lp->text[i] == ' ' || lp->text[i] == '\t')) i++;
    
    int indent_len = i;
    int marker_start = i;
    
    /* Check for unordered list markers: -, *, + */
    if (i < len && (lp->text[i] == '-' || lp->text[i] == '*' || lp->text[i] == '+')) {
        i++;
        if (i < len && (lp->text[i] == ' ' || lp->text[i] == '\t')) {
            while (i < len && (lp->text[i] == ' ' || lp->text[i] == '\t')) i++;
            int marker_end = i;
            
            /* If the rest of the line is empty, "stop" the list */
            if (i == len) {
                curwp->w_doto = marker_start;
                ldelete((long)(len - marker_start), FALSE);
                return lnewline();
            }
            
            /* Continue the list */
            if (lnewline() == FALSE) return FALSE;
            for (int k = 0; k < marker_end; k++) {
                linsert(1, lp->text[k]);
            }
            return TRUE;
        }
    }
    
    /* Check for ordered list markers: 1. , 2) , etc. */
    i = marker_start;
    if (i < len && isdigit(lp->text[i])) {
        int num = 0;
        while (i < len && isdigit(lp->text[i])) {
            num = num * 10 + (lp->text[i] - '0');
            i++;
        }
        if (i < len && (lp->text[i] == '.' || lp->text[i] == ')')) {
            char separator = lp->text[i];
            i++;
            if (i < len && (lp->text[i] == ' ' || lp->text[i] == '\t')) {
                while (i < len && (lp->text[i] == ' ' || lp->text[i] == '\t')) i++;
                int marker_end = i;
                
                if (i == len) {
                    curwp->w_doto = marker_start;
                    ldelete((long)(len - marker_start), FALSE);
                    return lnewline();
                }
                
                if (lnewline() == FALSE) return FALSE;
                /* Insert indentation */
                for (int k = 0; k < indent_len; k++) linsert(1, lp->text[k]);
                /* Insert incremented number */
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%d%c ", num + 1, separator);
                linstr(num_buf);
                return TRUE;
            }
        }
    }
    
    return FAILED; /* Not a list item or didn't handle it */
}

/*
 * Insert a newline. Bound to "C-M". If we are in CMODE, do automatic
 * indentation as specified.
 */
int insert_newline(int f, int n)
{
    int s;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if (n < 0)
        return FALSE;

    /* Markdown/Markup auto-completion */
    if (n == 1 && nanox_cfg.use_auto_doc_completion) {
        if (is_markdown_file()) {
            s = handle_markdown_list();
            if (s != FAILED) return s;
        }
    }

    /* if this is a default <NL> */
    if (n == 1 && curwp->w_dotp != curbp->b_linep)
        return cinsert();

    /*
     * If a newline was typed, fill column is defined, the argument is non-
     * negative, wrap mode is enabled, and we are now past fill column,
     * and we are not read-only, perform word wrap.
     */
    if ((curwp->w_bufp->b_mode & MDWRAP) && fillcol > 0 &&
        getccol(FALSE) > fillcol && (curwp->w_bufp->b_mode & MDVIEW) == FALSE)
        execute(META | SPEC | 'W', FALSE, 1);

    /* insert some lines */
    while (n--) {
        if ((s = lnewline()) != TRUE)
            return s;
    }
    return TRUE;
}

int cinsert(void)
{                       /* insert a newline and indentation for C */
    int bracef;             /* was there a brace at the end of line? */
    int target_indent;
    struct line *lp = curwp->w_dotp;
    int doto = curwp->w_doto;

    /* check for a brace at the end of the line (ignoring trailing whitespace) */
    int tptr = doto - 1;
    while (tptr >= 0 && (lgetc(lp, tptr) == ' ' || lgetc(lp, tptr) == '\t'))
        tptr--;
    bracef = (tptr >= 0 && lgetc(lp, tptr) == '{');

    /* check for dedent if current line starts with a closing block */
    check_indent_dedent();

    /* recapture lp and target_indent after possible dedent */
    lp = curwp->w_dotp;
    target_indent = get_indent(lp);

    /* check for preprocessor macros that should increase indentation */
    int ppf = 0;
    if (curbp->b_mode & MDCMOD) {
        int i = 0;
        while (i < llength(lp) && isspace(lgetc(lp, i))) i++;
        if (i < llength(lp) && lgetc(lp, i) == '#') {
            char word[32];
            int idx = 0;
            while (i < llength(lp) && !isspace(lgetc(lp, i)) && idx < 31) {
                word[idx++] = (char)lgetc(lp, i);
                i++;
            }
            word[idx] = '\0';
            if (strcmp(word, "#if") == 0 || strcmp(word, "#ifdef") == 0 || 
                strcmp(word, "#ifndef") == 0 || strcmp(word, "#else") == 0 || 
                strcmp(word, "#elif") == 0) ppf = 1;
        }
    }

    /* put in the newline */
    if (lnewline() == FALSE)
        return FALSE;

    /* and the saved indentation */
    set_indent(target_indent);

    /* and one level of indentation for an open brace or preprocessor block */
    if (bracef || ppf) {
        int step = (curbp->b_tabsize ? curbp->b_tabsize : (tab_width + 1));
        if (step == 8 && !nanox_cfg.soft_tab) linsert(1, '\t');
        else {
            for (int i = 0; i < step; i++) linsert(1, ' ');
        }
        curwp->w_doto = llength(curwp->w_dotp);
    }

    return TRUE;
}

/*
 * insert a brace into the text here...we are in CMODE
 *
 * int n;   repeat count
 * int c;   brace to insert (always } for now)
 */
int insbrace(int n, int c)
{
    int ch;                 /* last character before input */
    int oc;                 /* caractere oppose a c */
    int i, count;
    int target;             /* column brace should go after */
    struct line *oldlp;
    int oldoff;

    /* if the character at the cursor is already the same brace, skip insertion/re-indent */
    if (curwp->w_doto < llength(curwp->w_dotp) && lgetc(curwp->w_dotp, curwp->w_doto) == c) {
        curwp->w_doto++;
        return TRUE;
    }

    /* if we aren't at the beginning of the line... */
    if (curwp->w_doto != 0) {
        /* scan to see if all space before this is white space */
        for (i = curwp->w_doto - 1; i >= 0; --i) {
            ch = lgetc(curwp->w_dotp, i);
            if (ch != ' ' && ch != '\t')
                return linsert(n, c);
        }
    }

    /* if there is content AFTER the cursor, skip auto-indentation */
    if (curwp->w_doto < llength(curwp->w_dotp)) {
        /* if the same character is already there, just move forward? 
           Actually, the user said "disable indentation alignment", so we just insert. */
        for (i = curwp->w_doto; i < llength(curwp->w_dotp); i++) {
            ch = lgetc(curwp->w_dotp, i);
            if (ch != ' ' && ch != '\t')
                return linsert(n, c);
        }
    }

    /* chercher le caractere oppose correspondant */
    switch (c) {
    case '}':
        oc = '{';
        break;
    case ']':
        oc = '[';
        break;
    case ')':
        oc = '(';
        break;
    default:
        return linsert(n, c);
    }

    oldlp = curwp->w_dotp;
    oldoff = curwp->w_doto;

    count = 1;

    /* Scan back to find the matching open fence */
    while (backchar(FALSE, 1)) {
        if (curwp->w_doto == llength(curwp->w_dotp))
            ch = '\n';
        else
            ch = lgetc(curwp->w_dotp, curwp->w_doto);

        if (ch == c)
            ++count;
        else if (ch == oc)
            --count;

        if (count == 0) break;

        if (boundry(curwp->w_dotp, curwp->w_doto, REVERSE))
            break;
    }

    if (count != 0) {           /* no match */
        /* If pairs don't match, recalculate based on previous line's indent */
        struct line *lp = lback(oldlp);
        target = 0;
        while (lp != curbp->b_linep) {
            int is_blank = TRUE;
            for (int k = 0; k < llength(lp); k++) {
                int ch2 = lgetc(lp, k);
                if (ch2 != ' ' && ch2 != '\t') {
                    is_blank = FALSE;
                    break;
                }
            }
            if (!is_blank) {
                target = get_indent(lp);
                break;
            }
            lp = lback(lp);
        }
        curwp->w_dotp = oldlp;
        curwp->w_doto = oldoff;
    } else {
        /* Match found! Use the indentation of the line with the open fence */
        target = get_indent(curwp->w_dotp);
        curwp->w_dotp = oldlp;
        curwp->w_doto = oldoff;
    }

    /* Adjust indentation of the current line */
    set_indent(target);

    /* restore position for the actual brace insertion */
    curwp->w_doto = llength(curwp->w_dotp);

    /* and insert the required brace(s) */
    return linsert(n, c);
}

int inspound(void)
{                       /* insert a # into the text here...we are in CMODE */
    int ch;                 /* last character before input */
    int i;

    /* if we are at the beginning of the line, no go */
    if (curwp->w_doto == 0)
        return linsert(1, '#');

    /* scan to see if all space before this is white space */
    for (i = curwp->w_doto - 1; i >= 0; --i) {
        ch = lgetc(curwp->w_dotp, i);
        if (ch != ' ' && ch != '\t')
            return linsert(1, '#');
    }

    /* if in C mode, we allow indentation for # */
    if (curbp->b_mode & MDCMOD)
        return linsert(1, '#');

    /* delete back first */
    while (getccol(FALSE) >= 1)
        backdel(FALSE, 1);

    /* and insert the required pound */
    return linsert(1, '#');
}

/*
 * Delete blank lines around dot. What this command does depends if dot is
 * sitting on a blank line. If dot is sitting on a blank line, this command
 * deletes all the blank lines above and below the current line. If it is
 * sitting on a non blank line then it deletes all of the blank lines after
 * the line. Normally this command is bound to "C-X C-O". Any argument is
 * ignored.
 */
int deblank(int f, int n)
{
    struct line *lp1;
    struct line *lp2;
    long nld;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    lp1 = curwp->w_dotp;
    while (llength(lp1) == 0 && (lp2 = lback(lp1)) != curbp->b_linep)
        lp1 = lp2;
    lp2 = lp1;
    nld = 0;
    while ((lp2 = lforw(lp2)) != curbp->b_linep && llength(lp2) == 0)
        ++nld;
    if (nld == 0)
        return TRUE;
    curwp->w_dotp = lforw(lp1);
    curwp->w_doto = 0;
    return ldelete(nld, FALSE);
}

/*
 * Insert a newline, then enough tabs and spaces to duplicate the indentation
 * of the previous line. Assumes tabs are every eight characters. Quite simple.
 * Figure out the indentation of the current line. Insert a newline by calling
 * the standard routine. Insert the indentation by inserting the right number
 * of tabs and spaces. Return TRUE if all ok. Return FALSE if one of the
 * subcomands failed. Normally bound to "C-J".
 */
int indent(int f, int n)
{
    if (curbp->b_mode & MDVIEW)
        return rdonly();
    if (n < 0)
        return FALSE;

    while (n--) {
        if (curbp->b_mode & MDCMOD)
            check_indent_dedent();

        struct line *lp = curwp->w_dotp;
        int target_indent = get_indent(lp);
        
        /* check if we are on a line that just opened a brace */
        int doto = llength(lp);
        int tptr = doto - 1;
        while (tptr >= 0 && (lgetc(lp, tptr) == ' ' || lgetc(lp, tptr) == '\t'))
            tptr--;
        
        int bracef = (tptr >= 0 && lgetc(lp, tptr) == '{');

        /* check for preprocessor block opening */
        int ppf = 0;
        if (curbp->b_mode & MDCMOD) {
            int i = 0;
            while (i < llength(lp) && isspace(lgetc(lp, i))) i++;
            if (i < llength(lp) && lgetc(lp, i) == '#') {
                char word[32];
                int idx = 0;
                while (i < llength(lp) && !isspace(lgetc(lp, i)) && idx < 31) {
                    word[idx++] = (char)lgetc(lp, i);
                    i++;
                }
                word[idx] = '\0';
                if (strcmp(word, "#if") == 0 || strcmp(word, "#ifdef") == 0 || 
                    strcmp(word, "#ifndef") == 0 || strcmp(word, "#else") == 0 || 
                    strcmp(word, "#elif") == 0) ppf = 1;
            }
        }

        if (bracef || ppf) {
            target_indent += (curbp->b_tabsize ? curbp->b_tabsize : tab_width);
        }

        if (lnewline() == FALSE)
            return FALSE;
        
        set_indent(target_indent);
    }
    return TRUE;
}

/* Maximum detectable indentation step width (covers 2/3/4/6/8-space indentation) */
#define MAX_INDENT_DETECT 8

/*
 * Detect the dominant indentation step from the current buffer content.
 * Scans non-blank lines and counts how often each positive indent
 * increment (1-MAX_INDENT_DETECT spaces) occurs, then returns the most
 * common step.  Returns tab_width if the file is tab-indented.
 */
static int detect_indent_step(void)
{
    struct line *lp;
    int freq[MAX_INDENT_DETECT + 1]; /* freq[1..MAX_INDENT_DETECT] */
    int i, prev_indent = -1;

    for (i = 0; i <= MAX_INDENT_DETECT; i++) freq[i] = 0;

    /* Tab-indented file: return tab_width immediately */
    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        if (llength(lp) > 0 && lgetc(lp, 0) == '\t')
            return tab_width;
    }

    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        int blank = TRUE;
        for (i = 0; i < llength(lp); i++) {
            int c = lgetc(lp, i);
            if (c != ' ' && c != '\t') { blank = FALSE; break; }
        }
        if (!blank) {
            int ind = get_indent(lp);
            if (prev_indent >= 0 && ind > prev_indent) {
                int delta = ind - prev_indent;
                if (delta >= 1 && delta <= MAX_INDENT_DETECT)
                    freq[delta]++;
            }
            prev_indent = ind;
        }
    }

    int best = tab_width, best_count = 0;
    for (i = 1; i <= MAX_INDENT_DETECT; i++) {
        if (freq[i] > best_count) {
            best_count = freq[i];
            best = i;
        }
    }
    return best;
}

/*
 * Helper to adjust indentation by delta levels
 */
static void adjust_indent(struct line *lp, int delta)
{
    int step = curbp->b_tabsize ? curbp->b_tabsize : detect_indent_step();
    int cur = get_indent(lp);
    int target = cur + delta * step;
    if (target < 0) target = 0;
    
    struct line *save_lp = curwp->w_dotp;
    int save_doto = curwp->w_doto;
    
    curwp->w_dotp = lp;
    set_indent(target);
    
    if (save_lp == lp) {
        /* set_indent already updated curwp->w_doto to end of indent */
    } else {
        curwp->w_dotp = save_lp;
        curwp->w_doto = save_doto;
    }
}

static long line_number_for(struct line *lp)
{
    if (lp == NULL)
        return -1;
    long lineno = 0;
    struct line *scan = curbp->b_linep;
    while ((scan = lforw(scan)) != curbp->b_linep) {
        ++lineno;
        if (scan == lp)
            return lineno;
    }
    return -1;
}

static const char *indent_mode_label(void)
{
    return (indent_range_type < 0) ? "Outdent" : "Indent";
}

static char indent_start_key(void)
{
    return (indent_range_type < 0) ? 'H' : 'J';
}

static void announce_indent_state(const char *action)
{
    const char *mode = indent_mode_label();
    char start_key = indent_start_key();
    long start_line = line_number_for(indent_start_lp);
    long end_line = line_number_for(indent_end_lp);

    if (indent_start_lp && indent_end_lp && start_line > 0 && end_line > 0) {
        long first = start_line;
        long last = end_line;
        if (first > last) {
            long tmp = first;
            first = last;
            last = tmp;
        }
        mlwrite("[%s: lines %ld-%ld | Tab/gg: %s | BS: cancel]",
                action, first, last,
                (indent_range_type < 0) ? "outdent" : "indent");
        return;
    }

    if (indent_start_lp && start_line > 0) {
        mlwrite("[%s: %s start line %ld | move to end, then Tab/gg | BS: cancel]",
                action, mode, start_line);
        return;
    }

    if (indent_start_lp) {
        mlwrite("[%s: %s start set | move to end, then Tab/gg | BS: cancel]",
                action, mode);
        return;
    }

    if (indent_end_lp && end_line > 0) {
        mlwrite("[%s: %s end line %ld | set start with Ctrl+%c]",
                action, mode, end_line, start_key);
        return;
    }

    if (indent_end_lp) {
        mlwrite("[%s: %s end set | set start with Ctrl+%c]",
                action, mode, start_key);
        return;
    }

    mlwrite("[%s]", action);
}

static void indent_reset_range(void)
{
    indent_selection_active = FALSE;
    indent_start_lp = NULL;
    indent_end_lp = NULL;
    indent_range_type = 0;
}

static int indent_range_pending(void)
{
    if (indent_selection_active)
        return TRUE;
    if (indent_start_lp != NULL && indent_end_lp != NULL)
        return TRUE;
    return FALSE;
}

static int indent_set_range_from_mark(void)
{
    if (curwp->w_markp == NULL) {
        indent_start_lp = NULL;
        indent_end_lp = NULL;
        mlwrite("[%s: Set start with Ctrl+%c first]", indent_mode_label(),
                indent_start_key());
        return FALSE;
    }

    indent_start_lp = curwp->w_markp;
    indent_end_lp = curwp->w_dotp;

    if (indent_start_lp == indent_end_lp)
        return TRUE;

    struct line *scan = curbp->b_linep;
    struct line *first = NULL;
    struct line *second = NULL;

    while ((scan = lforw(scan)) != curbp->b_linep) {
        if (scan == curwp->w_markp) {
            first = curwp->w_markp;
            second = curwp->w_dotp;
            break;
        }
        if (scan == curwp->w_dotp) {
            first = curwp->w_dotp;
            second = curwp->w_markp;
            break;
        }
    }

    if (first == NULL || second == NULL) {
        indent_start_lp = NULL;
        indent_end_lp = NULL;
        mlwrite("[%s: Selection invalid. Set range again]", indent_mode_label());
        return FALSE;
    }

    indent_start_lp = first;
    indent_end_lp = second;
    return TRUE;
}

static int indent_begin_range(int f, int n, int type, const char *action)
{
    int status = setmark(f, n);
    if (status != TRUE)
        return status;

    indent_range_type = type;
    indent_selection_active = TRUE;
    indent_start_lp = curwp->w_dotp;
    indent_end_lp = NULL;
    announce_indent_state(action);
    return TRUE;
}

static int indent_finalize_range(int type, const char *action)
{
    indent_range_type = type;

    if (!indent_selection_active) {
        mlwrite("[%s: Set start with Ctrl+%c first]", indent_mode_label(),
                indent_start_key());
        return FALSE;
    }

    if (!indent_set_range_from_mark()) {
        indent_reset_range();
        return FALSE;
    }

    indent_selection_active = FALSE;
    announce_indent_state(action);
    return TRUE;
}

/* Ctrl+J: Set start point for indent */
int indent_start_set(int f, int n)
{
    return indent_begin_range(f, n, 1, "Indent start set");
}

/* Ctrl+Shift+J: Set end point for indent */
int indent_end_set(int f, int n)
{
    return indent_finalize_range(1, "Indent end set");
}

/* Ctrl+H: Set start point for outdent */
int outdent_start_set(int f, int n)
{
    return indent_begin_range(f, n, -1, "Outdent start set");
}

/* Ctrl+Shift+H: Set end point for outdent */
int outdent_end_set(int f, int n)
{
    return indent_finalize_range(-1, "Outdent end set");
}

/* gg: Apply indent/outdent to range */
int indent_apply_range(int f, int n)
{
    if (indent_start_lp == NULL || indent_end_lp == NULL) {
        mlwrite("[Range not set. Use Ctrl+J (indent) or Ctrl+H (outdent) to mark start, move cursor to end, then Tab or gg]");
        return FALSE;
    }
    if (curbp->b_mode & MDVIEW) return rdonly();

    struct line *lp1 = indent_start_lp;
    struct line *lp2 = indent_end_lp;
    
    /* Determine order */
    struct line *scan = curbp->b_linep;
    struct line *first = NULL;
    struct line *last = NULL;
    
    while ((scan = lforw(scan)) != curbp->b_linep) {
        if (scan == lp1) {
            if (first == NULL) { first = lp1; last = lp2; }
            break;
        }
        if (scan == lp2) {
            if (first == NULL) { first = lp2; last = lp1; }
            break;
        }
    }
    
    if (first == NULL) {
        indent_reset_range();
        mlwrite("[gg: Selection invalid. Mark the range again.]");
        return FALSE;
    }

    long start_line = line_number_for(lp1);
    long end_line = line_number_for(lp2);

    scan = first;
    struct line *stop = lforw(last);
    while (scan != stop) {
        struct line *next = lforw(scan);
        adjust_indent(scan, indent_range_type);
        scan = next;
    }
    
    lchange(WFHARD);
    const char *mode = indent_mode_label();
    if (start_line > 0 && end_line > 0) {
        long first_line = start_line;
        long last_line = end_line;
        if (first_line > last_line) {
            long tmp = first_line;
            first_line = last_line;
            last_line = tmp;
        }
        mlwrite("[%s applied: lines %ld-%ld]", mode, first_line, last_line);
    } else {
        mlwrite("[%s range applied]", mode);
    }
    
    /* Clear range after apply */
    indent_reset_range();
    return TRUE;
}

/* Backspace: Cancel operation */
int indent_cancel(int f, int n)
{
    if (indent_start_lp != NULL || indent_end_lp != NULL || indent_selection_active) {
        indent_reset_range();
        mlwrite("[Indent selection canceled | start with Ctrl+J or Ctrl+H]");
        return TRUE;
    }
    return backdel(f, n);
}

/* g prefix handler for gg */
int g_prefix_handler(int f, int n)
{
    int c;

    if (!indent_range_pending())
        return linsert(1, 'g');

    while ((c = getcmd()) == 0)
        ttpause();

    if (c == 'g') {
        /* If selection is active (start set but end not set), finalize with cursor */
        if (indent_selection_active && indent_start_lp != NULL) {
            indent_end_lp = curwp->w_dotp;
            indent_selection_active = FALSE;
        }
        return indent_apply_range(f, n);
    }
    /* Fallback: insert 'g' and then the next character */
    linsert(1, 'g');
    execute(c, FALSE, 1);
    return TRUE;
}

/*
 * Delete forward. This is real easy, because the basic delete routine does
 * all of the work. Watches for negative arguments, and does the right thing.
 * If any argument is present, it kills rather than deletes, to prevent loss
 * of text if typed with a big argument. Normally bound to "C-D".
 */
int forwdel(int f, int n)
{
    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if (n < 0)
        return backdel(f, -n);
    if (f != FALSE) {           /* Really a kill.       */
        if ((lastflag & CFKILL) == 0)
            kdelete();
        thisflag |= CFKILL;
    }
    return ldelchar((long)n, f);
}

/*
 * Delete backwards. This is quite easy too, because it's all done with other
 * functions. Just move the cursor back, and delete forwards. Like delete
 * forward, this actually does a kill if presented with an argument. Bound to
 * both "RUBOUT" and "C-H".
 */
int backdel(int f, int n)
{
    int s;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if (n < 0)
        return forwdel(f, -n);
    if (f != FALSE) {           /* Really a kill.       */
        if ((lastflag & CFKILL) == 0)
            kdelete();
        thisflag |= CFKILL;
    }
    /* If cursor is on an empty line at column 0, prefer removing that line
     * itself (join with next line) to avoid being stuck at BOB.
     */
    if (n == 1 && curwp->w_doto == 0 && curwp->w_dotp->used == 0 &&
        lforw(curwp->w_dotp) != curbp->b_linep)
        return ldelchar(1, f);
    if ((s = backchar(f, n)) == TRUE)
        s = ldelchar(n, f);
    return s;
}

/*
 * Kill text. If called without an argument, it kills from dot to the end of
 * the line, unless it is at the end of the line, when it kills the newline.
 * If called with an argument of 0, it kills from the start of the line to dot.
 * If called with a positive argument, it kills from dot forward over that
 * number of newlines. If called with a negative argument it kills backwards
 * that number of newlines. Normally bound to "C-K".
 */
int killtext(int f, int n)
{
    struct line *nextp;
    long chunk;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if ((lastflag & CFKILL) == 0)       /* Clear kill buffer if */
        kdelete();          /* last wasn't a kill.  */
    thisflag |= CFKILL;
    if (f == FALSE) {
        chunk = llength(curwp->w_dotp) - curwp->w_doto;
        if (chunk == 0)
            chunk = 1;
    } else if (n == 0) {
        chunk = curwp->w_doto;
        curwp->w_doto = 0;
    } else if (n > 0) {
        chunk = llength(curwp->w_dotp) - curwp->w_doto + 1;
        nextp = lforw(curwp->w_dotp);
        while (--n) {
            if (nextp == curbp->b_linep)
                return FALSE;
            chunk += llength(nextp) + 1;
            nextp = lforw(nextp);
        }
    } else {
        mlwrite("neg kill");
        return FALSE;
    }
    return ldelete(chunk, TRUE);
}

/*
 * prompt and set an editor mode
 *
 * int f, n;        default and argument
 */
int setemode(int f, int n)
{
    return adjustmode(TRUE, FALSE);
}

/*
 * prompt and delete an editor mode
 *
 * int f, n;        default and argument
 */
int delmode(int f, int n)
{
    return adjustmode(FALSE, FALSE);
}

/*
 * prompt and set a global editor mode
 *
 * int f, n;        default and argument
 */
int setgmode(int f, int n)
{
    return adjustmode(TRUE, TRUE);
}

/*
 * prompt and delete a global editor mode
 *
 * int f, n;        default and argument
 */
int delgmode(int f, int n)
{
    return adjustmode(FALSE, TRUE);
}

/*
 * change the editor mode status
 *
 * int kind;        true = set,          false = delete
 * int global;      true = global flag,  false = current buffer flag
 */
int adjustmode(int kind, int global)
{
    char *scan;             /* scanning pointer to convert prompt */
    int i;                  /* loop index */
    int status;             /* error return on input */
    char prompt[50];            /* string to prompt user with */
    char cbuf[NPAT];            /* buffer to recieve mode name into */

    /* build the proper prompt string */
    if (global)
        strcpy(prompt, "Global mode to ");
    else
        strcpy(prompt, "Mode to ");

    if (kind == TRUE)
        strcat(prompt, "add: ");
    else
        strcat(prompt, "delete: ");

    /* prompt the user and get an answer */

    status = minibuf_input(prompt, cbuf, NPAT - 1);
    if (status != TRUE)
        return status;

    /* make it uppercase */

    scan = cbuf;
    while (*scan != 0) {
        if (*scan >= 'a' && *scan <= 'z')
            *scan = *scan - 32;
        scan++;
    }

    /* test it against the modes we know */

    for (i = 0; i < NUMMODES; i++) {
        if (strcmp(cbuf, modename[i]) == 0) {
            /* finding a match, we process it */
            if (kind == TRUE)
                if (global)
                    gmode |= (1 << i);
                else
                    curbp->b_mode |= (1 << i);
            else if (global)
                gmode &= ~(1 << i);
            else
                curbp->b_mode &= ~(1 << i);
            /* display new mode line */
            if (global == 0)
                upmode();
            mlerase();      /* erase the junk */
            return TRUE;
        }
    }

    mlwrite("No such mode!");
    return FALSE;
}

/*
 * This function simply clears the message line,
 * mainly for macro usage
 *
 * int f, n;        arguments ignored
 */
int clrmes(int f, int n)
{
    mlforce("");
    return TRUE;
}

/*
 * This function writes a string on the message line
 * mainly for macro usage
 *
 * int f, n;        arguments ignored
 */
int writemsg(int f, int n)
{
    char *sp;               /* pointer into buf to expand %s */
    char *np;               /* ptr into nbuf */
    int status;
    char buf[NPAT];             /* buffer to recieve message into */
    char nbuf[NPAT * 2];            /* buffer to expand string into */

    if ((status = minibuf_input("Message to write: ", buf, NPAT - 1)) != TRUE)
        return status;

    /* expand all '%' to "%%" so mlwrite won't expect arguments */
    sp = buf;
    np = nbuf;
    while (*sp) {
        *np++ = *sp;
        if (*sp++ == '%')
            *np++ = '%';
    }
    *np = '\0';

    /* write the message out */
    mlforce(nbuf);
    return TRUE;
}

/*
 * the cursor is moved to a matching fence
 *
 * int f, n;        not used
 */
int getfence(int f, int n)
{
    struct line *oldlp;         /* original line pointer */
    int oldoff;             /* and offset */
    int sdir;               /* direction of search (1/-1) */
    int count;              /* current fence level count */
    char ch;                /* fence type to match against */
    char ofence;                /* open fence */
    char c;                 /* current character in scan */

    /* save the original cursor position */
    oldlp = curwp->w_dotp;
    oldoff = curwp->w_doto;

    /* get the current character */
    if (oldoff == llength(oldlp))
        ch = '\n';
    else
        ch = lgetc(oldlp, oldoff);

    /* setup proper matching fence */
    switch (ch) {
    case '(':
        ofence = ')';
        sdir = FORWARD;
        break;
    case '{':
        ofence = '}';
        sdir = FORWARD;
        break;
    case '[':
        ofence = ']';
        sdir = FORWARD;
        break;
    case ')':
        ofence = '(';
        sdir = REVERSE;
        break;
    case '}':
        ofence = '{';
        sdir = REVERSE;
        break;
    case ']':
        ofence = '[';
        sdir = REVERSE;
        break;
    default:
        TTbeep();
        return FALSE;
    }

    /* set up for scan */
    count = 1;
    if (sdir == REVERSE)
        backchar(FALSE, 1);
    else
        forwchar(FALSE, 1);

    /* scan until we find it, or reach the end of file */
    while (count > 0) {
        if (curwp->w_doto == llength(curwp->w_dotp))
            c = '\n';
        else
            c = lgetc(curwp->w_dotp, curwp->w_doto);
        if (c == ch)
            ++count;
        if (c == ofence)
            --count;
        if (sdir == FORWARD)
            forwchar(FALSE, 1);
        else
            backchar(FALSE, 1);
        if (boundry(curwp->w_dotp, curwp->w_doto, sdir))
            break;
    }

    /* if count is zero, we have a match, move the sucker */
    if (count == 0) {
        if (sdir == FORWARD)
            backchar(FALSE, 1);
        else
            forwchar(FALSE, 1);
        curwp->w_flag |= WFMOVE;
        return TRUE;
    }

    /* restore the current position */
    curwp->w_dotp = oldlp;
    curwp->w_doto = oldoff;
    TTbeep();
    return FALSE;
}

/*
 * Close fences are matched against their partners, and if
 * on screen the cursor briefly lights there
 *
 * char ch;         fence type to match against
 */
int fmatch(int ch)
{
    struct line *oldlp;         /* original line pointer */
    int oldoff;             /* and offset */
    struct line *toplp;         /* top line in current window */
    int count;              /* current fence level count */
    char opench;                /* open fence */
    char c;                 /* current character in scan */

    /* first get the display update out there */
    update(FALSE);

    /* save the original cursor position */
    oldlp = curwp->w_dotp;
    oldoff = curwp->w_doto;

    /* setup proper open fence for passed close fence */
    if (ch == ')')
        opench = '(';
    else if (ch == '}')
        opench = '{';
    else
        opench = '[';

    /* find the top line and set up for scan */
    toplp = curwp->w_linep->prev;
    count = 1;
    if (backchar(FALSE, 2) == FALSE)
        return TRUE;

    /* scan back until we find it, or reach past the top of the window */
    while (count > 0 && curwp->w_dotp != toplp) {
        if (curwp->w_doto == llength(curwp->w_dotp))
            c = '\n';
        else
            c = lgetc(curwp->w_dotp, curwp->w_doto);
        if (c == ch)
            ++count;
        if (c == opench)
            --count;
        backchar(FALSE, 1);
        if (curwp->w_dotp == curwp->w_bufp->b_linep->next && curwp->w_doto == 0)
            break;
    }

    /* if count is zero, we have a match, display the sucker */
    if (count == 0) {
        forwchar(FALSE, 1);
        update(FALSE);
        ttpause();
    }

    /* restore the current position */
    curwp->w_dotp = oldlp;
    curwp->w_doto = oldoff;
    return TRUE;
}

/*
 * ask for and insert a string into the current
 * buffer at the current point
 *
 * int f, n;        ignored arguments
 */
int istring(int f, int n)
{
    int status;             /* status return code */
    char tstring[NPAT + 1];         /* string to add */

    /* ask for string to insert */
    status = mlreplyt("String to insert<META>: ", tstring, NPAT, metac);
    if (status != TRUE)
        return status;

    if (f == FALSE)
        n = 1;

    if (n < 0)
        n = -n;

    /* insert it */
    while (n-- && (status = linstr(tstring))) ;
    return status;
}

/*
 * ask for and overwite a string into the current
 * buffer at the current point
 *
 * int f, n;        ignored arguments
 */
int ovstring(int f, int n)
{
    int status;             /* status return code */
    char tstring[NPAT + 1];         /* string to add */

    /* ask for string to insert */
    status = mlreplyt("String to overwrite<META>: ", tstring, NPAT, metac);
    if (status != TRUE)
        return status;

    if (f == FALSE)
        n = 1;

    if (n < 0)
        n = -n;

    /* insert it */
    while (n-- && (status = lover(tstring))) ;
    return status;
}
