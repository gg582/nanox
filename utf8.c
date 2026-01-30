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
 * NOTE 2! This does *not* verify things like minimality. So overlong forms
 * are happily accepted and decoded, as are the various "invalid values".
 */
unsigned utf8_to_unicode(unsigned char *line, unsigned index, unsigned len, unicode_t *res)
{
    unsigned value;
    unsigned char c = line[index];
    unsigned bytes, mask, i;

    *res = c;
    line += index;
    len -= index;

    /*
     * 0xxxxxxx is valid utf8
     * 10xxxxxx is invalid UTF-8, we assume it is Latin1
     */
    if (c < 0xc0)
        return 1;

    /* Ok, it's 11xxxxxx, do a stupid decode */
    mask = 0x20;
    bytes = 2;
    while (c & mask) {
        bytes++;
        mask >>= 1;
    }

    /* Invalid? Do it as a single byte Latin1 */
    if (bytes > 6)
        return 1;
    if (bytes > len) {
        *res = 0xFFFD; /* Replacement character for incomplete sequence */
        return 1;
    }

    value = c & (mask - 1);

    /* Ok, do the bytes */
    for (i = 1; i < bytes; i++) {
        c = line[i];
        if ((c & 0xc0) != 0x80)
            return 1;
        value = (value << 6) | (c & 0x3f);
    }
    *res = value;
    return bytes;
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
