#include "cursor.h"
#include "utf8.h"

void cursor_init(struct cursor *c, struct line *lp, int offset)
{
    c->linep = lp;
    c->offset = offset;
}

int cursor_forwchar(struct cursor *c, int n, struct line *head)
{
    if (n < 0)
        return cursor_backchar(c, -n, head);
    while (n--) {
        int len = llength(c->linep);
        if (c->offset >= len) {
            if (c->linep == head)
                return 0; /* FALSE */
            c->linep = lforw(c->linep);
            c->offset = 0;
        } else {
            unicode_t ch;
            int bytes = utf8_to_unicode(ltext(c->linep), c->offset, len, &ch);
            if (bytes <= 0) bytes = 1; /* Fallback for safety */
            c->offset += bytes;
            if (c->offset > len) {
                c->offset = len;
            }
        }
    }
    return 1; /* TRUE */
}

int cursor_backchar(struct cursor *c, int n, struct line *head)
{
    if (n < 0)
        return cursor_forwchar(c, -n, head);
    while (n--) {
        if (c->offset <= 0) {
            struct line *lp = lback(c->linep);
            if (lp == head)
                return 0; /* FALSE */
            c->linep = lp;
            c->offset = llength(lp);
        } else {
            unsigned char ch;
            do {
                c->offset--;
                ch = lgetc(c->linep, c->offset);
                if (is_beginning_utf8(ch))
                    break;
            } while (c->offset > 0);
        }
    }
    return 1; /* TRUE */
}

void cursor_gotobol(struct cursor *c)
{
    c->offset = 0;
}

void cursor_gotoeol(struct cursor *c)
{
    c->offset = llength(c->linep);
}
