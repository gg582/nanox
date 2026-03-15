/*
 * cutln.c
 *
 * CutLn functionality (F7 integration)
 */

#include <stdio.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "paste_slot.h"

static void cutln_prime_kill_buffer(void)
{
    if ((lastflag & CFKILL) == 0)
        kdelete();
    thisflag |= CFKILL;
}

int cutln_cut_current_line(int f, int n)
{
    int lines_to_cut = 1;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    if (f != FALSE) {
        if (n <= 0)
            return FALSE;
        lines_to_cut = n;
    }

    if (curwp->w_dotp == curbp->b_linep)
        return FALSE;

    cutln_prime_kill_buffer();

    while (lines_to_cut--) {
        struct line *lp = curwp->w_dotp;
        long chunk;
        int len;
        int has_next_line;

        if (lp == curbp->b_linep)
            break;

        curwp->w_dotp = lp;
        curwp->w_doto = 0;
        len = llength(lp);
        has_next_line = (lforw(lp) != curbp->b_linep);
        chunk = len + (has_next_line ? 1L : 0L);
        if (chunk == 0)
            chunk = 1;

        if (ldelete(chunk, TRUE) != TRUE)
            return FALSE;
    }

    curwp->w_flag |= WFHARD;
    mlwrite("Line cut.");
    return TRUE;
}

/*
 * F7: CutLn End (Perform Cut)
 * Only works if selection (mark) is active.
 */
int cutln_end_cut(int f, int n)
{
    if (cutln_active) {
        int s = killregion(f, n);
        cutln_active = FALSE;
        mlwrite("Region cut.");
        return s;
    } else {
        mlwrite("No selection active. Press F7 to start cut.");
        return FALSE;
    }
}

/*
 * Shift+F7: CutLn Start (Set Mark)
 */
int cutln_start_cut(int f, int n)
{
    int s = setmark(f, n);
    if (s == TRUE) {
        cutln_active = TRUE;
        mlwrite("Cut selection started. Press Shift+F7 to cut.");
    }
    return s;
}

/*
 * F6: CutLn End (Perform Copy)
 * Only works if selection (mark) is active.
 */
int cutln_end_copy(int f, int n)
{
    if (cutln_active) {
        int s = copyregion(f, n);
        cutln_active = FALSE;
        mlwrite("Region copied.");
        return s;
    } else {
        mlwrite("No selection active. Press F6 to start copy.");
        return FALSE;
    }
}

/*
 * Shift+F6: CutLn Start (Set Mark)
 */
int cutln_start_copy(int f, int n)
{
    int s = setmark(f, n);
    if (s == TRUE) {
        cutln_active = TRUE;
        mlwrite("Copy selection started. Press Shift+F6 to copy.");
    }
    return s;
}

/*
 * Ctrl+F7: Paste Menu
 * Activates the paste slot window (same as Ctrl+Shift+V logic)
 */
int cutln_paste_menu(int f, int n)
{
    paste_slot_set_active(1);
    paste_slot_display();
    return TRUE;
}
