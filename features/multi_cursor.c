#include <stdio.h>
#include <stdlib.h>
#include "estruct.h"
#include "edef.h"
#include "line.h"
#include "multi_cursor.h"
#include "nanox.h"

#define MAX_MULTI_CURSORS 64

struct cursor_state {
    struct line *linep;
    int offset;
    int active;
};

static struct cursor_state cursors[MAX_MULTI_CURSORS];
static int cursor_count = 0;
static int active_cursor_idx = 0;

void multi_cursor_create(int count) {
    if (count <= 1 || !curwp || !curbp) return;
    if (count > MAX_MULTI_CURSORS) count = MAX_MULTI_CURSORS;
    
    struct line *lp = curwp->w_dotp;
    int doto = curwp->w_doto;
    
    cursor_count = 0;
    while (lp != curbp->b_linep && cursor_count < count) {
        cursors[cursor_count].linep = lp;
        cursors[cursor_count].offset = (doto > llength(lp)) ? llength(lp) : doto;
        cursors[cursor_count].active = (cursor_count == 0);
        
        lp = lforw(lp);
        cursor_count++;
    }
    active_cursor_idx = 0;
}

int multi_cursor_select(int index) {
    if (index < 1 || index > cursor_count) return 0;
    
    /* Save current cursor state before switching */
    if (active_cursor_idx >= 0 && active_cursor_idx < cursor_count) {
        cursors[active_cursor_idx].linep = curwp->w_dotp;
        cursors[active_cursor_idx].offset = curwp->w_doto;
        cursors[active_cursor_idx].active = 0;
    }
    
    active_cursor_idx = index - 1;
    cursors[active_cursor_idx].active = 1;
    
    /* Switch editor cursor */
    curwp->w_dotp = cursors[active_cursor_idx].linep;
    curwp->w_doto = cursors[active_cursor_idx].offset;
    curwp->w_flag |= WFMOVE;
    
    return 1;
}

void multi_cursor_single(void) {
    cursor_count = 0;
    active_cursor_idx = 0;
}
