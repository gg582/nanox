#ifndef UTIL_H_
#define UTIL_H_

#include "utf8.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/* Unicode character range constants for display width calculations */
#define HANGUL_COMPAT_JAMO_START 0x3130
#define HANGUL_COMPAT_JAMO_END   0x318F
#define CJK_UNIFIED_START        0x4E00
#define CJK_UNIFIED_END          0x9FFF
#define HANGUL_SYLLABLES_START   0xAC00
#define HANGUL_SYLLABLES_END     0xD7AF
#define HIRAGANA_START           0x3040
#define HIRAGANA_END             0x309F
#define KATAKANA_START           0x30A0
#define KATAKANA_END             0x30FF

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

/*
 * mystrnlen_raw_w()
 *
 * Return the display width of a Unicode character, with specific handling for
 * CJK characters which are generally 2 columns wide.
 */
static inline int mystrnlen_raw_w(unicode_t c) {
    // Hangul Compatibility Jamo - display as width 1
    // These are individual Korean letters like ㄱ, ㄴ, ㄷ, etc.
    // Even though wcwidth() returns 2 for these, they display as 1 column in modern terminals
    if (c >= HANGUL_COMPAT_JAMO_START && c <= HANGUL_COMPAT_JAMO_END) return 1;
    // CJK Unified Ideographs
    if (c >= CJK_UNIFIED_START && c <= CJK_UNIFIED_END) return 2;
    // Hangul Syllables
    if (c >= HANGUL_SYLLABLES_START && c <= HANGUL_SYLLABLES_END) return 2;
    // Hiragana
    if (c >= HIRAGANA_START && c <= HIRAGANA_END) return 2;
    // Katakana
    if (c >= KATAKANA_START && c <= KATAKANA_END) return 2;
    // Fallback to unicode_width for other characters
    return unicode_width(c);
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

    /* Use mystrnlen_raw_w for proper handling of wide characters */
    return old + mystrnlen_raw_w(c);
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
        if (bytes <= 0)
            break;
        width += mystrnlen_raw_w(c);
        i += bytes;
    }
    
    return width;
}

#endif              /* UTIL_H_ */
