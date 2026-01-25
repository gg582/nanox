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

// Calculate the next column position based on character 'c'
// Uses proper Unicode width calculation for accurate cursor positioning
static inline int next_column(int old, unicode_t c, int tab_width)
{
	if (c == '\t')
		return next_tab_stop(old, tab_width);

	/* Use unicode_width for proper handling of wide characters */
	return old + unicode_width(c);
}

// Calculate display width of a UTF-8 string
// Returns the number of display columns the string will occupy
static inline int utf8_display_width(const char *str, int byte_len)
{
	int i = 0;
	int width = 0;
	
	while (i < byte_len && str[i]) {
		unicode_t c;
		int bytes = utf8_to_unicode((char *)str, i, byte_len, &c);
		width += unicode_width(c);
		i += bytes;
	}
	
	return width;
}

#endif				/* UTIL_H_ */
