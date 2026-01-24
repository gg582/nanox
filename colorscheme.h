#ifndef COLORSCHEME_H_
#define COLORSCHEME_H_

#include <stdbool.h>
#include <stddef.h>

/* Highlight Categories */
typedef enum {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_STRING,
    HL_NUMBER,
    HL_BRACKET,
    HL_OPERATOR,
    HL_KEYWORD,
    HL_RETURN,
    HL_TERNARY,
    HL_ERROR,
    HL_NOTICE,
    HL_COUNT
} HighlightStyleID;

/* Style Definition */
typedef struct {
    int fg;         /* -1 = default, 0-255 = ANSI, > 0xFFFFFF = RGB (packed 0x01RRGGBB) */
    int bg;         /* -1 = default */
    bool bold;
    bool underline;
} HighlightStyle;

void colorscheme_init(const char *scheme_name);
HighlightStyle colorscheme_get(HighlightStyleID id);
const char *colorscheme_get_name(void);

#endif /* COLORSCHEME_H_ */
