/*  region.c
 *
 *      The routines in this file deal with the region, that magic space
 *      between "." and mark. Some functions are commands. Some functions are
 *      just for internal use.
 *
 *  Modified by Petri Kutvonen
 */

#include <stdio.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

static int nanox_resolve_region(struct region *region)
{
    if (curwp->w_markp == NULL) {
        mlwrite("No mark set in this window");
        return FALSE;
    }
    return getregion(region);
}

static void nanox_prime_kill_buffer(void)
{
    if ((lastflag & CFKILL) == 0)
        kdelete();
    thisflag |= CFKILL;
}

static int nanox_copy_region_to_kill(const struct region *region)
{
    struct line *linep = region->r_linep;
    int loffs = region->r_offset;
    long remaining = region->r_size;

    while (remaining-- > 0) {
        if (linep == curbp->b_linep)
            return FALSE;
        if (loffs == llength(linep)) {
            if (kinsert('\n') != TRUE)
                return FALSE;
            linep = lforw(linep);
            loffs = 0;
        } else {
            if (kinsert(lgetc(linep, loffs)) != TRUE)
                return FALSE;
            ++loffs;
        }
    }
    return TRUE;
}

int kill_region_nanox(int f, int n)
{
    struct region region;

    if (curbp->b_mode & MDVIEW)
        return rdonly();
    if (nanox_resolve_region(&region) != TRUE)
        return FALSE;

    nanox_prime_kill_buffer();
    if (nanox_copy_region_to_kill(&region) != TRUE)
        return FALSE;

    curwp->w_dotp = region.r_linep;
    curwp->w_doto = region.r_offset;

    if (ldelete(region.r_size, FALSE) != TRUE)
        return FALSE;

    curwp->w_flag |= WFHARD;
    return TRUE;
}

int killregion(int f, int n)
{
    return kill_region_nanox(f, n);
}

/*
 * Copy all of the characters in the
 * region to the kill buffer. Don't move dot
 * at all. This is a bit like a kill region followed
 * by a yank. Bound to "M-W".
 */
int copy_region_nanox(int f, int n)
{
    struct region region;

    if (nanox_resolve_region(&region) != TRUE)
        return FALSE;

    nanox_prime_kill_buffer();
    if (nanox_copy_region_to_kill(&region) != TRUE)
        return FALSE;

    mlwrite("(region copied)");
    return TRUE;
}

int copyregion(int f, int n)
{
    return copy_region_nanox(f, n);
}

/*
 * Lower case region. Zap all of the upper
 * case characters in the region to lower case. Use
 * the region code to set the limits. Scan the buffer,
 * doing the changes. Call "lchange" to ensure that
 * redisplay is done in all buffers. Bound to
 * "C-X C-L".
 */
int lowerregion(int f, int n)
{
    struct line *linep;
    int loffs;
    int c;
    int s;
    struct region region;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if ((s = getregion(&region)) != TRUE)
        return s;
    lchange(WFHARD);
    linep = region.r_linep;
    loffs = region.r_offset;
    while (region.r_size--) {
        if (loffs == llength(linep)) {
            linep = lforw(linep);
            loffs = 0;
        } else {
            c = lgetc(linep, loffs);
            if (c >= 'A' && c <= 'Z')
                lputc(linep, loffs, c + 'a' - 'A');
            ++loffs;
        }
    }
    return TRUE;
}

/*
 * Upper case region. Zap all of the lower
 * case characters in the region to upper case. Use
 * the region code to set the limits. Scan the buffer,
 * doing the changes. Call "lchange" to ensure that
 * redisplay is done in all buffers. Bound to
 * "C-X C-L".
 */
int upperregion(int f, int n)
{
    struct line *linep;
    int loffs;
    int c;
    int s;
    struct region region;

    if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
        return rdonly();        /* we are in read only mode     */
    if ((s = getregion(&region)) != TRUE)
        return s;
    lchange(WFHARD);
    linep = region.r_linep;
    loffs = region.r_offset;
    while (region.r_size--) {
        if (loffs == llength(linep)) {
            linep = lforw(linep);
            loffs = 0;
        } else {
            c = lgetc(linep, loffs);
            if (c >= 'a' && c <= 'z')
                lputc(linep, loffs, c - 'a' + 'A');
            ++loffs;
        }
    }
    return TRUE;
}

/*
 * This routine figures out the
 * bounds of the region in the current window, and
 * fills in the fields of the "struct region" structure pointed
 * to by "rp". Because the dot and mark are usually very
 * close together, we scan outward from dot looking for
 * mark. This should save time. Return a standard code.
 * Callers of this routine should be prepared to get
 * an "ABORT" status; we might make this have the
 * conform thing later.
 */
int getregion(struct region *rp)
{
    struct line *flp;
    struct line *blp;
    long fsize;
    long bsize;

    if (curwp->w_markp == NULL) {
        mlwrite("No mark set in this window");
        return FALSE;
    }
    if (curwp->w_dotp == curwp->w_markp) {
        rp->r_linep = curwp->w_dotp;
        if (curwp->w_doto < curwp->w_marko) {
            rp->r_offset = curwp->w_doto;
            rp->r_size = (long)(curwp->w_marko - curwp->w_doto);
        } else {
            rp->r_offset = curwp->w_marko;
            rp->r_size = (long)(curwp->w_doto - curwp->w_marko);
        }
        return TRUE;
    }
    blp = curwp->w_dotp;
    bsize = (long)curwp->w_doto;
    flp = curwp->w_dotp;
    fsize = (long)(llength(flp) - curwp->w_doto + 1);
    while (flp != curbp->b_linep || lback(blp) != curbp->b_linep) {
        if (flp != curbp->b_linep) {
            flp = lforw(flp);
            if (flp == curwp->w_markp) {
                rp->r_linep = curwp->w_dotp;
                rp->r_offset = curwp->w_doto;
                rp->r_size = fsize + curwp->w_marko;
                return TRUE;
            }
            fsize += llength(flp) + 1;
        }
        if (lback(blp) != curbp->b_linep) {
            blp = lback(blp);
            bsize += llength(blp) + 1;
            if (blp == curwp->w_markp) {
                rp->r_linep = blp;
                rp->r_offset = curwp->w_marko;
                rp->r_size = bsize - curwp->w_marko;
                return TRUE;
            }
        }
    }
    mlwrite("Bug: lost mark");
    return FALSE;
}
