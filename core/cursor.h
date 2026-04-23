#ifndef CURSOR_H_
#define CURSOR_H_

#include "line.h"

struct cursor {
    struct line *linep;
    int offset;
};

/* Cursor manager functions */
void cursor_init(struct cursor *c, struct line *lp, int offset);
int cursor_forwchar(struct cursor *c, int n, struct line *head);
int cursor_backchar(struct cursor *c, int n, struct line *head);
void cursor_gotobol(struct cursor *c);
void cursor_gotoeol(struct cursor *c);

#endif
