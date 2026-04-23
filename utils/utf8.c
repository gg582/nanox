#include "utf8.h"
#include <wchar.h>

/*
 * utf8_to_unicode()
 *
 * Convert a UTF-8 sequence to its unicode value, and return the length of
 * the sequence in bytes.
 *
 * NOTE! Invalid UTF-8 will be converted to a one-byte sequence, so you can
 * either use it as-is (ie as Latin1) or you can check for invalid UTF-8
 * by checking for a length of 1 and a result > 127.
 *
 * NOTE 2! Invalid and overlong UTF-8 is converted to U+FFFD while advancing
 * by one byte so callers can recover safely.
 */
unsigned utf8_to_unicode(unsigned char *line, unsigned index, unsigned len, unicode_t *res)
{
    if (!line || !res || index >= len)
        return 0;

    unsigned char c = line[index];
    *res = c;

    if (c < 0x80)
        return 1;

    /* Reject stray continuation */
    if ((c & 0xC0) == 0x80)
        return 1;

    unsigned available = len - index;
    if ((c & 0xE0) == 0xC0) { /* 2-byte */
        if (available < 2)
            goto invalid;
        unsigned char c1 = line[index + 1];
        if ((c1 & 0xC0) != 0x80)
            goto invalid;
        unsigned value = ((unsigned)(c & 0x1F) << 6) | (unsigned)(c1 & 0x3F);
        if (value < 0x80) /* overlong */
            goto invalid;
        *res = value;
        return 2;
    }

    if ((c & 0xF0) == 0xE0) { /* 3-byte */
        if (available < 3)
            goto invalid;
        unsigned char c1 = line[index + 1];
        unsigned char c2 = line[index + 2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
            goto invalid;
        unsigned value = ((unsigned)(c & 0x0F) << 12) |
                         ((unsigned)(c1 & 0x3F) << 6) |
                         (unsigned)(c2 & 0x3F);
        if (value < 0x800) /* overlong */
            goto invalid;
        if (value >= 0xD800 && value <= 0xDFFF) /* surrogate */
            goto invalid;
        *res = value;
        return 3;
    }

    if ((c & 0xF8) == 0xF0) { /* 4-byte */
        if (available < 4)
            goto invalid;
        unsigned char c1 = line[index + 1];
        unsigned char c2 = line[index + 2];
        unsigned char c3 = line[index + 3];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
            goto invalid;
        unsigned value = ((unsigned)(c & 0x07) << 18) |
                         ((unsigned)(c1 & 0x3F) << 12) |
                         ((unsigned)(c2 & 0x3F) << 6) |
                         (unsigned)(c3 & 0x3F);
        if (value < 0x10000 || value > 0x10FFFF)
            goto invalid;
        *res = value;
        return 4;
    }

invalid:
    *res = 0xFFFD;
    return 1;
}

static void reverse_string(unsigned char *begin, unsigned char *end)
{
    do {
        char a = *begin, b = *end;
        *end = a;
        *begin = b;
        begin++;
        end--;
    } while (begin < end);
}

/*
 * unicode_to_utf8()
 *
 * Convert a unicode value to its canonical utf-8 sequence.
 *
 * NOTE! This does not check for - or care about - the "invalid" unicode
 * values.  Also, converting a utf-8 sequence to unicode and back does
 * *not* guarantee the same sequence, since this generates the shortest
 * possible sequence, while utf8_to_unicode() accepts both Latin1 and
 * overlong utf-8 sequences.
 */
unsigned unicode_to_utf8(unsigned int c, unsigned char *utf8)
{
    int bytes = 1;

    *utf8 = c;
    if (c > 0x7f) {
        int prefix = 0x40;
        char *p = utf8;
        do {
            *p++ = 0x80 + (c & 0x3f);
            bytes++;
            prefix >>= 1;
            c >>= 6;
        } while (c >= prefix);
        *p = c - 2 * prefix;
        reverse_string(utf8, p);
    }
    return bytes;
}

/*
 * unicode_width()
 *
 * Return the display width of a Unicode character.
 * Uses wcwidth() for proper handling of East Asian wide characters,
 * combining characters, and other special cases.
 */
int unicode_width(unicode_t c)
{
    int width;
    
    /* Handle special control characters */
    if (c < 0x20 || c == 0x7F)
        return 2;  /* Control chars displayed as ^X */
    
    if (c >= 0x80 && c <= 0xA0)
        return 3;  /* Displayed as \xx */
    
    /* Use wcwidth for proper Unicode width calculation */
    width = wcwidth((wchar_t)c);
    
    /* wcwidth returns -1 for non-printable chars, treat as 1 */
    if (width < 0)
        return 1;
    
    return width;
}
