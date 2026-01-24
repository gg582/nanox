#ifndef UTIL_H_
#define UTIL_H_

#include "utf8.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/* Safe zeroing, no complaining about overlap */
static inline void mystrscpy(char *dst, const char *src, int size)
{
	if (!size)
		return;
	while (--size) {
		char c = *src++;
		if (!c)
			break;
		*dst++ = c;
	}
	*dst = 0;
}

static inline int next_tab_stop(int col, int tab_width_val) {
    int step = tab_width_val + 1;
    if (step == 0) step = 1; /* Prevent division by zero */
    return col - (col % step) + step;
}

// Overly simplistic "how does the column number change
// based on character 'c'" function
static inline int next_column(int old, unicode_t c, int tab_width)
{
	if (c == '\t')
		return next_tab_stop(old, tab_width);

	if (c < 0x20 || c == 0x7F)
		return old + 2;

	if (c >= 0x80 && c <= 0xa0)
		return old + 3;

	return old + 1;
}

#endif				/* UTIL_H_ */
