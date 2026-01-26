/*  random.c
 *
 *      This file contains the command processing functions for a number of
 *      random commands. There is no functional grouping here, for sure.
 *
 *  Modified by Petri Kutvonen
 */

#include <string.h>
#include <ctype.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "util.h"
#include "nanox.h"

int tabsize;                    /* Tab size (0: use real tabs) */

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
            int step = tab_width + 1;
            if (step == 0) step = 1; /* Avoid division by zero */
            int num_tabs = target / step;
            int num_spaces = target % step;
            while (num_tabs--) linsert(1, '\t');
            while (num_spaces--) linsert(1, ' ');
        }
    }
    curwp->w_doto = llength(curwp->w_dotp);
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

        i += utf8_to_unicode(dlp->l_text, i, len, &c);
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
        bytes = utf8_to_unicode(dlp->l_text, i, llen, &c);
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
    int cl;
    int cr;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    dotp = curwp->w_dotp;
    doto = curwp->w_doto;
    if (doto == llength(dotp) && --doto < 0)
        return FALSE;
    cr = lgetc(dotp, doto);
    if (--doto < 0)
        return FALSE;
    cl = lgetc(dotp, doto);
    lputc(dotp, doto + 0, cr);
    lputc(dotp, doto + 1, cl);
    lchange(WFEDIT);
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
                        target += (tabsize ? tabsize : (tab_width + 1));
                    break;
                }
                lp = lback(lp);
            }
            set_indent(target);
            return TRUE;
        }
    }

    if (n == 0 || n > 1) {
        tabsize = n;
        return TRUE;
    }
    if (!tabsize)
        return linsert(1, '\t');
    return linsert(tabsize - (getccol(FALSE) % tabsize), ' ');
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
                        int step = tab_width + 1;
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
        length = lp->l_used;        /* find current length */

        /* trim the current line */
        while (length > offset) {
            if (lgetc(lp, length - 1) != ' ' && lgetc(lp, length - 1) != '\t')
                break;
            length--;
        }
        lp->l_used = length;

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

    /* put in the newline */
    if (lnewline() == FALSE)
        return FALSE;

    /* and the saved indentation */
    set_indent(target_indent);

    /* and one level of indentation for an open brace */
    if (bracef) {
        int step = (tabsize ? tabsize : (tab_width + 1));
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

        int target_indent = get_indent(curwp->w_dotp);
        
        /* check if we are on a line that just opened a brace */
        int doto = llength(curwp->w_dotp);
        int tptr = doto - 1;
        while (tptr >= 0 && (lgetc(curwp->w_dotp, tptr) == ' ' || lgetc(curwp->w_dotp, tptr) == '\t'))
            tptr--;
        
        if (tptr >= 0 && lgetc(curwp->w_dotp, tptr) == '{') {
            target_indent += (tabsize ? tabsize : (tab_width + 1));
        }

        if (lnewline() == FALSE)
            return FALSE;
        
        set_indent(target_indent);
    }
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
    toplp = curwp->w_linep->l_bp;
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
        if (curwp->w_dotp == curwp->w_bufp->b_linep->l_fp && curwp->w_doto == 0)
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
