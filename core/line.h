#ifndef LINE_H_
#define LINE_H_

#include "utf8.h"
#include <stdatomic.h>
#include <stdint.h>
#include "highlight.h"
#include "../utils/mymemory.h"

/*
 * All text is kept in circularly linked lists of "struct line" structures. These
 * begin at the header line (which is the blank line beyond the end of the
 * buffer). This line is pointed to by the "struct buffer". Each line contains a the
 * number of bytes in the line (the "used" size), the size of the text array,
 * and the text. The end of line is not stored as a byte; it's implied. Future
 * additions will include update hints, and a list of marks into the line.
 */
struct line {
    _Atomic(struct line *) next;          /* 8 bytes */
    _Atomic(struct line *) prev;          /* 8 bytes */
    MemoryHandle l_handle;                /* 8 bytes */
    HighlightState hl_start_state;        /* 104 bytes */
    HighlightState hl_end_state;          /* 104 bytes */
    _Atomic int size;                     /* 4 bytes */
    _Atomic int used;                     /* 4 bytes */
    uint32_t l_offset;                    /* 4 bytes */
    char l_diag;                          /* 1 byte */
    char _padding[3];                     /* 3 bytes manual padding for 8-byte alignment */
};

#define ltext(lp)       (((unsigned char * restrict)handle_deref((lp)->l_handle)) + (lp)->l_offset)

#define lforw(lp)       ((lp)->next)
#define lback(lp)       ((lp)->prev)
#define lgetc(lp, n)    (ltext(lp)[(n)]&0xFF)
#define lputc(lp, n, c) (ltext(lp)[(n)]=(c))
#define llength(lp)     ((lp)->used)

extern void lfree(struct line *lp);
extern void lmark_dirty(struct line *lp);
extern void lchange(int flag);
extern int l_unshare(struct line *lp);
extern int insspace(int f, int n);
extern int linstr(char *instr);
extern int linsert(int n, int c);
extern int sanitize_and_insert(int n, int c);
extern int linsert_block(const char *block, int len);
extern int lowrite(int c);
extern int lover(char *ostr);
extern int lnewline(void);
extern int ldelete(long n, int kflag);
extern int ldelchar(long n, int kflag);
extern int lgetchar(unicode_t *);
extern char *getctext(void);
extern int putctext(char *iline);
extern int ldelnewline(void);
extern void kdelete(void);
extern int kinsert(int c);
extern int yank(int f, int n);
extern struct line *lalloc(int);        /* Allocate a line. */

#endif              /* LINE_H_ */
