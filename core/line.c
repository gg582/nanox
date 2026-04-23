/*  line.c
 *
 * The functions in this file are a general set of line management utilities.
 * They are the only routines that touch the text. They also touch the buffer
 * and window structures, to make sure that the necessary updating gets done.
 * There are routines in this file that handle the kill buffer too. It isn't
 * here for any good reason.
 *
 * Note that this code only updates the dot and mark values in the window list.
 * Since all the code acts on the current window, the buffer that we are
 * editing must be being displayed, which means that "b_nwnd" is non zero,
 * which means that the dot and mark values in the buffer headers are nonsense.
 *
 */

#include "line.h"

#include <stdio.h>
#include <stdlib.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "utf8.h"
#include "nanox.h"

extern struct kill *kbufp;

#define BLOCK_SIZE 16               /* Line block chunk size. */

/*
 * This routine allocates a block of memory large enough to hold a struct line
 * containing "used" characters. The block is always rounded up a bit. Return
 * a pointer to the new block, or NULL if there isn't any memory left. Print a
 * message in the message line if no space.
 */
struct line *lalloc(int used)
{
    struct line *lp;
    int size;

    size = (used + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
    if (size == 0)              /* Assume that is an empty. */
        size = BLOCK_SIZE;      /* Line is for type-in. */
    if ((lp = (struct line *)malloc(sizeof(struct line))) == NULL) {
        mlwrite("(OUT OF MEMORY)");
        return NULL;
    }
    if ((lp->text = (unsigned char *)malloc(size)) == NULL) {
        free(lp);
        mlwrite("(OUT OF MEMORY)");
        return NULL;
    }
    lp->size = size;
    lp->used = used;
    lp->hl_start_state = (HighlightState){0};
    lp->hl_end_state = (HighlightState){0};
    lp->l_diag = 0;
    return lp;
}

/*
 * Delete line "lp". Fix all of the links that might point at it (they are
 * moved to offset 0 of the next line. Unlink the line from whatever buffer it
 * might be in. Release the memory. The buffers are updated too; the magic
 * conditions described in the above comments don't hold here.
 */
void lfree(struct line *lp)
{
    struct buffer *bp;
    struct window *wp;

    wp = curwp;
    if (wp->w_linep == lp)
        wp->w_linep = lp->next;
    if (wp->w_dotp == lp) {
        wp->w_dotp = lp->next;
        wp->w_doto = 0;
    }
    if (wp->w_markp == lp) {
        wp->w_markp = lp->next;
        wp->w_marko = 0;
    }

    bp = bheadp;
    while (bp != NULL) {
        if (bp->b_hl_dirty_line == lp) {
            bp->b_hl_dirty_line = lp->next;
            if (bp->b_hl_dirty_line == bp->b_linep)
                bp->b_hl_dirty_line = NULL;
        }
        if (bp->b_nwnd == 0) {
            if (bp->b_dotp == lp) {
                bp->b_dotp = lp->next;
                bp->b_doto = 0;
            }
            if (bp->b_markp == lp) {
                bp->b_markp = lp->next;
                bp->b_marko = 0;
            }
        }
        bp = bp->b_bufp;
    }
    lp->prev->next = lp->next;
    lp->next->prev = lp->prev;
    if (lp->text != NULL)
        free(lp->text);
    free((char *)lp);
}

/*
 * This routine gets called when a character is changed in place in the current
 * buffer. It updates all of the required flags in the buffer and window
 * system. The flag used is passed as an argument; if the buffer is being
 * displayed in more than 1 window we change EDIT t HARD. Set MODE if the
 * mode line needs to be updated (the "*" has to be set).
 */
void lmark_dirty(struct line *lp)
{
    if (lp == NULL || lp == curbp->b_linep) return;
    if (curbp->b_hl_dirty_line == NULL) {
        curbp->b_hl_dirty_line = lp;
        return;
    }
    if (curbp->b_hl_dirty_line == lp) return;

    /* Scan forward and backward to see which is first */
    struct line *fw = lp;
    struct line *bw = lp;
    const int MAX_SCAN = 500;

    for (int i = 0; i < MAX_SCAN; i++) {
        fw = lforw(fw);
        if (fw == curbp->b_hl_dirty_line) {
            curbp->b_hl_dirty_line = lp;
            return;
        }
        if (fw == curbp->b_linep) break;
    }
    for (int i = 0; i < MAX_SCAN; i++) {
        bw = lback(bw);
        if (bw == curbp->b_hl_dirty_line) {
            /* curbp->b_hl_dirty_line is already before lp */
            return;
        }
        if (bw == curbp->b_linep) break;
    }
    
    /* If still not found (jumped > 500 lines), be safe and set to beginning */
    curbp->b_hl_dirty_line = lforw(curbp->b_linep);
}

void lchange(int flag)
{
    struct window *wp;

    if (curbp->b_nwnd != 1)         /* Ensure hard.     */
        flag = WFHARD;
    if ((curbp->b_flag & BFCHG) == 0) { /* First change, so     */
        flag |= WFMODE;         /* update mode lines.   */
        curbp->b_flag |= BFCHG;
    }
    wp = curwp;
    if (wp->w_bufp == curbp) {
        wp->w_flag |= flag;
        lmark_dirty(wp->w_dotp);
    }
}

/*
 * insert spaces forward into text
 *
 * int f, n;        default flag and numeric argument
 */
int insspace(int f, int n)
{
    linsert(n, ' ');
    backchar(f, n);
    return TRUE;
}

/*
 * linstr -- Insert a string at the current point
 */

int linstr(char *instr)
{
    int status = TRUE;
    char tmpc;

    if (instr != NULL)
        while ((tmpc = *instr) && status == TRUE) {
            if (tmpc == '\r') {
                status = lnewline();
                if (*(instr + 1) == '\n')
                    instr++;
            } else if (tmpc == '\n') {
                status = lnewline();
            } else {
                status = linsert(1, tmpc);
            }

            /* Insertion error? */
            if (status != TRUE) {
                mlwrite("%%Out of memory while inserting");
                break;
            }
            instr++;
        }
    return status;
}

/*
 * Insert "n" copies of the character "c" at the current location of dot. In
 * the easy case all that happens is the text is stored in the line. In the
 * hard case, the line has to be reallocated. When the window list is updated,
 * take special care; I screwed it up once. You always update dot in the
 * current window. You update mark, and a dot in another window, if it is
 * greater than the place where you did the insert. Return TRUE if all is
 * well, and FALSE on errors.
 */

int linsert_byte(int n, int c)
{
    unsigned char *cp1;
    unsigned char *cp2;
    struct line *lp1;
    struct line *lp2;
    struct line *lp3;
    int doto;
    int i;
    struct window *wp;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    lchange(WFEDIT);
    lp1 = curwp->w_dotp;            /* Current line         */
    if (lp1 == curbp->b_linep) {        /* At the end: special  */
        if (curwp->w_doto != 0) {
            mlwrite("bug: linsert");
            return FALSE;
        }
        if ((lp2 = lalloc(n)) == NULL)  /* Allocate new line        */
            return FALSE;
        lp3 = lp1->prev;        /* Previous line        */
        lp3->next = lp2;        /* Link in              */
        lp2->next = lp1;
        lp1->prev = lp2;
        lp2->prev = lp3;
        for (i = 0; i < n; ++i)
            lp2->text[i] = c;
        curwp->w_dotp = lp2;
        curwp->w_doto = n;
        return TRUE;
    }
    doto = curwp->w_doto;           /* Save for later.      */
    if (lp1->used + n > lp1->size) {    /* Hard: reallocate     */
        int newsize = (lp1->used + n + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
        unsigned char *newtext = (unsigned char *)realloc(lp1->text, newsize);
        if (newtext == NULL) return FALSE;
        lp1->text = newtext;
        lp1->size = newsize;
    }
    
    lp2 = lp1;
    lp2->used += n;
    cp2 = &lp1->text[lp2->used];
    cp1 = cp2 - n;
    while (cp1 != &lp1->text[doto])
        *--cp2 = *--cp1;

    for (i = 0; i < n; ++i)         /* Add the characters       */
        lp2->text[doto + i] = c;
    wp = curwp;             /* Update window        */
    if (wp->w_linep == lp1)
        wp->w_linep = lp2;
    if (wp->w_dotp == lp1) {
        wp->w_dotp = lp2;
        wp->w_doto += n;
    }
    if (wp->w_markp == lp1) {
        wp->w_markp = lp2;
        if (wp->w_marko > doto)
            wp->w_marko += n;
    }
    return TRUE;
}

int linsert(int n, int c)
{
    char utf8[6];
    int bytes = unicode_to_utf8(c, utf8), i;

    if (bytes == 1)
        return linsert_byte(n, (unsigned char)utf8[0]);
    for (i = 0; i < n; i++) {
        int j;
        for (j = 0; j < bytes; j++) {
            unsigned char c = utf8[j];
            if (!linsert_byte(1, c))
                return FALSE;
        }
    }
    return TRUE;
}

/*
 * nanox: UTF-8 safe control character filter.
 * 1. Allow essential control characters: \n, \t, \r.
 * 2. Block system control characters (0x00-0x1F except above, 0x7F).
 * 3. Allow everything else including UTF-8 multibyte sequences.
 */
int sanitize_and_insert(int n, int c)
{
    if (c == '\n' || c == '\t' || c == '\r') {
        if (c == '\n') {
            while (n--) {
                if (lnewline() == FALSE) return FALSE;
            }
            return TRUE;
        }
        return linsert(n, c);
    }
    else if ((c < 32 && c >= 0) || c == 127) {
        /* Silently drop raw control codes/junk */
        return TRUE;
    }
    else {
        return linsert(n, c);
    }
}

/*
 * Overwrite a character into the current line at the current position
 *
 * int c;   character to overwrite on current position
 */
int lowrite(int c)
{
    if (curwp->w_doto < curwp->w_dotp->used) {
        unicode_t existing_char;
        /* Determine the width of the existing character to be replaced */
        int bytes = lgetchar(&existing_char);

        if (existing_char != '\t' || ((curwp->w_doto) & tab_width) == tab_width) {
            /* Remove the old multi-byte character entirely */
            ldelete((long)bytes, FALSE);
        }
    }
    /* Insert the new character into the cleared space */
    return linsert(1, c);
}

/*
 * lover -- Overwrite a string at the current point
 */
int lover(char *ostr)
{
    int status = TRUE;
    char tmpc;

    if (ostr != NULL)
        while ((tmpc = *ostr) && status == TRUE) {
            if (tmpc == '\r') {
                status = lnewline();
                if (*(ostr + 1) == '\n')
                    ostr++;
            } else if (tmpc == '\n') {
                status = lnewline();
            } else {
                status = lowrite(tmpc);
            }

            /* Insertion error? */
            if (status != TRUE) {
                mlwrite("%%Out of memory while overwriting");
                break;
            }
            ostr++;
        }
    return status;
}

/*
 * Insert a newline into the buffer at the current location of dot in the
 * current window. The funny ass-backwards way it does things is not a botch;
 * it just makes the last line in the file not a special case. Return TRUE if
 * everything works out and FALSE on error (memory allocation failure). The
 * update of dot and mark is a bit easier then in the above case, because the
 * split forces more updating.
 */
int lnewline(void)
{
	char *cp1;
	char *cp2;
	struct line *lp1;
	struct line *lp2;
	struct line *lp3;
	int doto;

	if (curbp) curbp->b_version++;
	struct window *wp;

	if (curbp->b_mode & MDVIEW)		/* don't allow this command if      */
		return rdonly();		/* we are in read only mode     */

	lp1 = curwp->w_dotp;			/* Get the address and  */
	doto = curwp->w_doto;			/* offset of "."        */

	lchange(WFHARD);

	/* Special case: at end of buffer (on sentinel line) */
	if (lp1 == curbp->b_linep) {
		if (doto != 0) {
			mlwrite("bug: lnewline at sentinel");
			return FALSE;
		}
		/* Allocate an empty new line */
		if ((lp2 = lalloc(0)) == NULL)
			return FALSE;
		lp3 = lp1->prev;		/* Previous line        */
		lp3->next = lp2;		/* Link in              */
		lp2->next = lp1;
		lp1->prev = lp2;
		lp2->prev = lp3;
		/* Cursor stays on sentinel (end of buffer) */
		return TRUE;
	}

	/* Ensure we don't split a UTF-8 character - adjust to character boundary */
	while (doto > 0 && doto < lp1->used && !is_beginning_utf8((unsigned char)lp1->text[doto])) {
		doto--;
	}

	/* Allocate a new line for the second half */
	if ((lp2 = lalloc(lp1->used - doto)) == NULL)
		return FALSE;

	cp1 = (char *)&lp1->text[doto];
	cp2 = (char *)&lp2->text[0];
	while (cp1 != (char *)&lp1->text[lp1->used])
		*cp2++ = *cp1++;

	lp2->next = lp1->next;		/* Link in new line */
	lp1->next = lp2;
	lp2->next->prev = lp2;
	lp2->prev = lp1;
	lp1->used = doto;

	/* Update window pointers */
	wp = curwp;
	if (wp->w_dotp == lp1) {
		if (wp->w_doto >= doto) {
			wp->w_dotp = lp2;
			wp->w_doto -= doto;
		}
	}
	if (wp->w_markp == lp1) {
		if (wp->w_marko > doto) {
			wp->w_markp = lp2;
			wp->w_marko -= doto;
		}
	}
	return TRUE;
}

int lgetchar(unicode_t *c)
{
    int len = llength(curwp->w_dotp);
    unsigned char *buf = curwp->w_dotp->text;
    return utf8_to_unicode(buf, curwp->w_doto, len, c);
}

/*
 * ldelete() really fundamentally works on bytes, not characters.
 * It is used for things like "scan 5 words forwards, and remove
 * the bytes we scanned".
 *
 * If you want to delete characters, use ldelchar().
 */
int ldelchar(long n, int kflag)
{
    while (n-- > 0) {
        unicode_t c;
        /* Get the byte count of the actual character under the cursor */
        int bytes = lgetchar(&c);

        if (bytes <= 0)
            return FALSE;

        /* Delete all bytes belonging to this character to maintain data integrity */
        if (ldelete((long)bytes, kflag) != TRUE)
            return FALSE;
    }

    /* Force hard update to sync virtual screen with modified line text */
    lchange(WFHARD);
    return TRUE;
}

/*
 * This function deletes "n" bytes, starting at dot. It understands how do deal
 * with end of lines, etc. It returns TRUE if all of the characters were
 * deleted, and FALSE if they were not (because dot ran into the end of the
 * buffer. The "kflag" is TRUE if the text should be put in the kill buffer.
 *
 * long n;      # of chars to delete
 * int kflag;        put killed text in kill buffer flag
 */
int ldelete(long n, int kflag)
{
	char *cp1;
	char *cp2;
	struct line *dotp;
	int doto;
	int chunk;
	struct window *wp;

	if (curbp->b_mode & MDVIEW)		/* don't allow this command if      */
		return rdonly();		/* we are in read only mode     */
	while (n != 0) {
		dotp = curwp->w_dotp;
		doto = curwp->w_doto;
		if (dotp == curbp->b_linep)	/* Hit end of buffer.       */
			return FALSE;
		chunk = dotp->used - doto;	/* Size of chunk.       */
		if (chunk > n)
			chunk = n;
		if (chunk == 0) {		/* End of line, merge.  */
			lchange(WFHARD);
			if (ldelnewline() == FALSE || (kflag != FALSE && kinsert('\n') == FALSE))
				return FALSE;
			--n;
			continue;
		}
		lchange(WFHARD);
		cp1 = (char *)&dotp->text[doto];	/* Scrunch text.        */
		cp2 = cp1 + chunk;
		if (kflag != FALSE) {		/* Kill?                */
			while (cp1 != cp2) {
				/* Cast to unsigned char to prevent sign extension */
				if (kinsert((unsigned char)*cp1) == FALSE)
					return FALSE;
				++cp1;
			}
			cp1 = (char *)&dotp->text[doto];
		}
		while (cp2 != (char *)&dotp->text[dotp->used])
			*cp1++ = *cp2++;
		dotp->used -= chunk;
		
		/* Fix window offsets - currently only curwp used in this editor */
		wp = curwp;
		if (wp->w_dotp == dotp && wp->w_doto >= doto) {
			wp->w_doto -= chunk;
			if (wp->w_doto < doto)
				wp->w_doto = doto;
		}
		if (wp->w_markp == dotp && wp->w_marko >= doto) {
			wp->w_marko -= chunk;
			if (wp->w_marko < doto)
				wp->w_marko = doto;
		}
		n -= chunk;
	}
	return TRUE;
}

/*
 * getctext:    grab and return a string with the text of
 *      the current line
 */
char *getctext(void)
{
    struct line *lp;            /* line to copy */
    int size;               /* length of line to return */
    unsigned char *sp;               /* string pointer into line */
    char *dp;               /* string pointer into returned line */
    static char rline[NSTRING];     /* line to return */

    /* find the contents of the current line and its length */
    lp = curwp->w_dotp;
    sp = lp->text;
    size = lp->used;
    if (size >= NSTRING)
        size = NSTRING - 1;

    /* copy it across */
    dp = rline;
    while (size--)
        *dp++ = *sp++;
    *dp = 0;
    return rline;
}

/*
 * putctext:
 *  replace the current line with the passed in text
 *
 * char *iline;         contents of new line
 */
int putctext(char *iline)
{
    int status;

    /* delete the current line */
    curwp->w_doto = 0;          /* starting at the beginning of the line */
    if ((status = killtext(TRUE, 1)) != TRUE)
        return status;

    /* insert the new line */
    if ((status = linstr(iline)) != TRUE)
        return status;
    status = lnewline();
    backline(TRUE, 1);
    return status;
}

/*
 * Join the current line with the next one. Vim-style 'J'.
 * It replaces the newline and any leading whitespace of the next line
 * with a single space.
 */
int joinline(int f, int n)
{
    struct line *lp1;
    struct line *lp2;
    int status;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    if (n < 0)
        return FALSE;
    if (n == 0)
        n = 1;

    while (n--) {
        lp1 = curwp->w_dotp;
        lp2 = lp1->next;

        if (lp2 == curbp->b_linep) /* At the end of buffer */
            break;

        /* Move to the end of the current line */
        curwp->w_doto = lp1->used;

        /* Delete the newline */
        if ((status = ldelnewline()) != TRUE)
            return status;

        /* Now curwp->w_dotp is the joined line.
         * The next line's content is now at curwp->w_doto.
         * We want to replace leading whitespace there with a single space.
         */

        /* Remove leading whitespace from what was the next line */
        while (curwp->w_doto < curwp->w_dotp->used &&
               (curwp->w_dotp->text[curwp->w_doto] == ' ' ||
                curwp->w_dotp->text[curwp->w_doto] == '\t')) {
            ldelchar(1, FALSE);
        }

        /* Add a single space if the line isn't empty and doesn't already end in a space
         * and we aren't at the end of the line.
         */
        if (curwp->w_doto > 0 && curwp->w_dotp->text[curwp->w_doto - 1] != ' ' &&
            curwp->w_dotp->text[curwp->w_doto - 1] != '\t' &&
            curwp->w_doto < curwp->w_dotp->used) {
            linsert(1, ' ');
            /* back up so we are at the start of the joined content (Vim behavior) */
            backchar(FALSE, 1);
        }

        /* Vim's J typically leaves the cursor at the join point.
         * ldelnewline already set w_doto to the end of the old lp1.
         */
    }

    curwp->w_flag |= WFEDIT | WFMOVE;
    return TRUE;
}

/*
 * Delete a newline. Join the current line with the next line.
 If the next line
 * is the magic header line always return TRUE; merging the last line with the
 * header line can be thought of as always being a successful operation, even
 * if nothing is done, and this makes the kill buffer work "right". Easy cases
 * can be done by shuffling data around. Hard cases require that lines be moved
 * about in memory. Return FALSE on error and TRUE if all looks ok. Called by
 * "ldelete" only.
 */
int ldelnewline(void)
{
    char *cp1;
    char *cp2;
    struct line *lp1;
    struct line *lp2;
    struct line *lp3;
    struct window *wp;

    if (curbp) curbp->b_version++;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    lp1 = curwp->w_dotp;
    lp2 = lp1->next;
    if (lp2 == curbp->b_linep) {        /* At the buffer end.   */
        if (lp1->used == 0)       /* Blank line.              */
            lfree(lp1);
        return TRUE;
    }
    if (lp2->used <= lp1->size - lp1->used) {
        cp1 = (char *)&lp1->text[lp1->used];
        cp2 = (char *)&lp2->text[0];
        while (cp2 != (char *)&lp2->text[lp2->used])
            *cp1++ = *cp2++;
        wp = curwp;
        if (wp->w_linep == lp2)
            wp->w_linep = lp1;
        if (wp->w_dotp == lp2) {
            wp->w_dotp = lp1;
            wp->w_doto += lp1->used;
        }
        if (wp->w_markp == lp2) {
            wp->w_markp = lp1;
            wp->w_marko += lp1->used;
        }
        lp1->used += lp2->used;
        lp1->next = lp2->next;
        lp2->next->prev = lp1;
        if (curbp->b_hl_dirty_line == lp2)
            curbp->b_hl_dirty_line = lp1;
        free((char *)lp2->text);
        free((char *)lp2);
        return TRUE;
    }
    if ((lp3 = lalloc(lp1->used + lp2->used)) == NULL)
        return FALSE;
    cp1 = (char *)&lp1->text[0];
    cp2 = (char *)&lp3->text[0];
    while (cp1 != (char *)&lp1->text[lp1->used])
        *cp2++ = *cp1++;
    cp1 = (char *)&lp2->text[0];
    while (cp1 != (char *)&lp2->text[lp2->used])
        *cp2++ = *cp1++;
    lp1->prev->next = lp3;
    lp3->next = lp2->next;
    lp2->next->prev = lp3;
    lp3->prev = lp1->prev;
    wp = curwp;
    if (wp->w_linep == lp1 || wp->w_linep == lp2)
        wp->w_linep = lp3;
    if (wp->w_dotp == lp1)
        wp->w_dotp = lp3;
    else if (wp->w_dotp == lp2) {
        wp->w_dotp = lp3;
        wp->w_doto += lp1->used;
    }
    if (wp->w_markp == lp1)
        wp->w_markp = lp3;
    else if (wp->w_markp == lp2) {
        wp->w_markp = lp3;
        wp->w_marko += lp1->used;
    }
    if (curbp->b_hl_dirty_line == lp1 || curbp->b_hl_dirty_line == lp2)
        curbp->b_hl_dirty_line = lp3;
    free((char *)lp1->text);
    free((char *)lp1);
    free((char *)lp2->text);
    free((char *)lp2);
    return TRUE;
}

/*
 * Delete all of the text saved in the kill buffer. Called by commands when a
 * new kill context is being created. The kill buffer array is released, just
 * in case the buffer has grown to immense size. No errors.
 */
void kdelete(void)
{
    struct kill *kp;            /* ptr to scan kill buffer chunk list */

    if (kbufh != NULL) {

        /* first, delete all the chunks */
        kbufp = kbufh;
        while (kbufp != NULL) {
            kp = kbufp->d_next;
            free(kbufp);
            kbufp = kp;
        }

        /* and reset all the kill buffer pointers */
        kbufh = kbufp = NULL;
    }
    kused = KBLOCK;
}

/*
 * Insert a character to the kill buffer, allocating new chunks as needed.
 * Return TRUE if all is well, and FALSE on errors.
 *
 * int c;           character to insert in the kill buffer
 */
int kinsert(int c)
{
    struct kill *nchunk;            /* ptr to newly malloced chunk */

    /* check to see if we need a new chunk */
    if (kused >= KBLOCK) {
        if ((nchunk = (struct kill *)malloc(sizeof(struct kill))) == NULL)
            return FALSE;
        if (kbufh == NULL)      /* set head ptr if first time */
            kbufh = nchunk;
        if (kbufp != NULL)      /* point the current to this new one */
            kbufp->d_next = nchunk;
        kbufp = nchunk;
        kbufp->d_next = NULL;
        kused = 0;
    }

    /* and now insert the character */
    kbufp->d_chunk[kused++] = (char)(unsigned char)c;
    return TRUE;
}

/*
 * Yank text back from the kill buffer. This is really easy. All of the work
 * is done by the standard insert routines. All you do is run the loop, and
 * check for errors. Bound to "C-Y".
 */
int yank(int f, int n)
{
    int c;
    int i;
    unsigned char *sp;          /* pointer into string to insert */
    struct kill *kp;            /* pointer into kill buffer */

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if (n < 0)
        return FALSE;
    /* make sure there is something to yank */
    if (kbufh == NULL)
        return TRUE;            /* not an error, just nothing */

    /* for each time.... */
    while (n--) {
        kp = kbufh;
        while (kp != NULL) {
            if (kp->d_next == NULL)
                i = kused;
            else
                i = KBLOCK;
            sp = (unsigned char *)kp->d_chunk;
            while (i--) {
                c = (int)*sp++;
                if (c == '\r') {
                    if (lnewline() == FALSE)
                        return FALSE;
                    if (i > 0 && *sp == '\n') {
                        sp++;
                        i--;
                    }
                } else if (c == '\n') {
                    if (lnewline() == FALSE)
                        return FALSE;
                } else {
                    if (linsert_byte(1, c) == FALSE)
                        return FALSE;
                }
            }
            kp = kp->d_next;
        }
    }
    return TRUE;
}

/*
 * linsert_block -- Insert a block of text into the current line at dot.
 * This is a high-performance version of linstr for large pastes.
 * It minimizes window updates and buffer change notifications.
 */
int linsert_block(const char *block, int len)
{
    int i, j;
    int start = 0;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    for (i = 0; i <= len; i++) {
        /* If we hit a newline or the end of the block */
        if (i == len || block[i] == '\n' || block[i] == '\r') {
            int segment_len = i - start;
            if (segment_len > 0) {
                /* Insert the segment into the current line */
                struct line *lp1 = curwp->w_dotp;
                int doto = curwp->w_doto;
                struct line *lp2;

                if (lp1 == curbp->b_linep) {
                    /* Special case: end of buffer */
                    if ((lp2 = lalloc(segment_len)) == NULL) return FALSE;
                    struct line *lp3 = lp1->prev;
                    lp3->next = lp2;
                    lp2->next = lp1;
                    lp1->prev = lp2;
                    lp2->prev = lp3;
                    for (j = 0; j < segment_len; j++)
                        lp2->text[j] = block[start + j];
                    curwp->w_dotp = lp2;
                    curwp->w_doto = segment_len;
                } else {
                    /* Normal case: insert into line */
                    if (lp1->used + segment_len > lp1->size) {
                        /* Reallocate */
                        int newsize = (lp1->used + segment_len + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
                        unsigned char *newtext = (unsigned char *)realloc(lp1->text, newsize);
                        if (newtext == NULL) return FALSE;
                        lp1->text = newtext;
                        lp1->size = newsize;
                    }
                    
                    lp2 = lp1;
                    unsigned char *cp1 = &lp2->text[lp2->used + segment_len - 1];
                    unsigned char *cp2 = &lp2->text[lp2->used - 1];
                    for (j = lp2->used - 1; j >= doto; j--) *cp1-- = *cp2--;
                    lp2->used += segment_len;

                    /* Fill in the text */
                    for (j = 0; j < segment_len; j++)
                        lp2->text[doto + j] = block[start + j];
                    curwp->w_doto += segment_len;
                    
                    /* Final window updates for in-place case (lp1 == lp2) or segment finishing */
                    if (curwp->w_linep == lp1) curwp->w_linep = lp2;
                    if (curwp->w_dotp == lp1) curwp->w_dotp = lp2;
                    if (curwp->w_markp == lp1) curwp->w_markp = lp2;
                }
            }

            if (i < len) {
                /* Handle newline */
                if (lnewline() == FALSE) return FALSE;
                if (block[i] == '\r' && i + 1 < len && block[i+1] == '\n')
                    i++;
                start = i + 1;
            }
        }
    }

    lchange(WFHARD);
    return TRUE;
}
