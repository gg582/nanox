#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "custom_types.h"
#include "line.h"
#include "util.h"

#define MAX_CUSTOM_TYPES 512

static char custom_type_names[MAX_CUSTOM_TYPES][MAX_TOKEN_LEN];
static int custom_type_count = 0;
static struct buffer *tracked_buffer = NULL;
static bool tracked_dirty = true;

static bool is_ident_start(unsigned char c)
{
    return c == '_' || isalpha((unsigned char)c) || c >= 0x80;
}

static bool is_ident_char(unsigned char c)
{
    return c == '_' || isalnum((unsigned char)c) || c >= 0x80;
}

static bool token_equals(const char *token, const char *keyword)
{
    return keyword && token && strcmp(token, keyword) == 0;
}

static bool token_in_list(const char *token, const char *const *list, size_t count)
{
    if (!token)
        return false;
    for (size_t i = 0; i < count; i++) {
        if (list[i] && strcmp(token, list[i]) == 0)
            return true;
    }
    return false;
}

static void custom_types_clear(void)
{
    for (int i = 0; i < custom_type_count; i++)
        custom_type_names[i][0] = '\0';
    custom_type_count = 0;
}

static void custom_types_add(const char *name)
{
    if (!name || !*name)
        return;
    if (custom_type_count >= MAX_CUSTOM_TYPES)
        return;
    for (int i = 0; i < custom_type_count; i++) {
        if (strcmp(custom_type_names[i], name) == 0)
            return;
    }
    mystrscpy(custom_type_names[custom_type_count++], name, MAX_TOKEN_LEN);
}

static void handle_identifier_token(const char *token,
                                    bool *expect_direct_name,
                                    bool *using_alias_pending,
                                    bool *typedef_pending,
                                    char *typedef_candidate)
{
    if (!token || !*token)
        return;

    if (*expect_direct_name) {
        custom_types_add(token);
        *expect_direct_name = false;
        *using_alias_pending = false;
        return;
    }

    if (*using_alias_pending) {
        custom_types_add(token);
        *using_alias_pending = false;
        return;
    }

    if (*typedef_pending)
        mystrscpy(typedef_candidate, token, MAX_TOKEN_LEN);
}

static void flush_typedef_candidate(bool *typedef_pending, char *typedef_candidate)
{
    if (*typedef_pending && typedef_candidate[0] != '\0')
        custom_types_add(typedef_candidate);
    *typedef_pending = false;
    typedef_candidate[0] = '\0';
}

static void rebuild_custom_types(struct buffer *bp)
{
    static const char *type_keywords[] = {
        "class", "struct", "enum", "union",
        "interface", "protocol", "record", "data",
        "trait", "object"
    };
    static const char *alias_keywords[] = {
        "type", "typealias", "alias"
    };

    custom_types_clear();
    if (!bp)
        return;

    enum {
        CT_STATE_CODE,
        CT_STATE_LINE_COMMENT,
        CT_STATE_BLOCK_COMMENT,
        CT_STATE_STRING_SQ,
        CT_STATE_STRING_DQ,
        CT_STATE_CHAR
    } state = CT_STATE_CODE;

    bool expect_direct_name = false;
    bool using_alias_pending = false;
    bool typedef_pending = false;
    char typedef_candidate[MAX_TOKEN_LEN];
    typedef_candidate[0] = '\0';

    struct line *lp = lforw(bp->b_linep);

    while (lp != bp->b_linep) {
        const unsigned char *text = ltext(lp);
        int len = llength(lp);
        int i = 0;

        while (i < len) {
            unsigned char c = text[i];

            switch (state) {
            case CT_STATE_CODE:
                if (c == '/' && i + 1 < len && text[i + 1] == '/') {
                    state = CT_STATE_LINE_COMMENT;
                    i = len;
                    break;
                }
                if (c == '/' && i + 1 < len && text[i + 1] == '*') {
                    state = CT_STATE_BLOCK_COMMENT;
                    i += 2;
                    continue;
                }
                if (c == '"') {
                    state = CT_STATE_STRING_DQ;
                    i++;
                    continue;
                }
                if (c == '\'') {
                    state = CT_STATE_CHAR;
                    i++;
                    continue;
                }
                if (c == '`') {
                    state = CT_STATE_STRING_SQ;
                    i++;
                    continue;
                }
                if (is_ident_start(c)) {
                    int start = i;
                    i++;
                    while (i < len && is_ident_char(text[i]))
                        i++;
                    int tok_len = i - start;
                    if (tok_len >= MAX_TOKEN_LEN)
                        tok_len = MAX_TOKEN_LEN - 1;
                    char token[MAX_TOKEN_LEN];
                    memcpy(token, text + start, (size_t)tok_len);
                    token[tok_len] = '\0';

                    if (token_equals(token, "typedef")) {
                        typedef_pending = true;
                        using_alias_pending = false;
                        expect_direct_name = false;
                        typedef_candidate[0] = '\0';
                        continue;
                    }
                    if (token_equals(token, "using")) {
                        using_alias_pending = true;
                        expect_direct_name = false;
                        continue;
                    }
                    if (token_equals(token, "namespace") && using_alias_pending) {
                        using_alias_pending = false;
                        continue;
                    }
                    if (token_in_list(token, alias_keywords, ARRAY_SIZE(alias_keywords))) {
                        expect_direct_name = true;
                        using_alias_pending = false;
                        continue;
                    }
                    if (token_in_list(token, type_keywords, ARRAY_SIZE(type_keywords))) {
                        expect_direct_name = true;
                        continue;
                    }

                    handle_identifier_token(token, &expect_direct_name,
                                             &using_alias_pending, &typedef_pending,
                                             typedef_candidate);
                    continue;
                }
                if (c == ';')
                    flush_typedef_candidate(&typedef_pending, typedef_candidate);
                i++;
                break;
            case CT_STATE_LINE_COMMENT:
                i = len;
                break;
            case CT_STATE_BLOCK_COMMENT:
                if (c == '*' && i + 1 < len && text[i + 1] == '/') {
                    state = CT_STATE_CODE;
                    i += 2;
                } else {
                    i++;
                }
                break;
            case CT_STATE_STRING_DQ:
                if (c == '\\' && i + 1 < len)
                    i += 2;
                else if (c == '"') {
                    state = CT_STATE_CODE;
                    i++;
                } else {
                    i++;
                }
                break;
            case CT_STATE_STRING_SQ:
                if (c == '\\' && i + 1 < len)
                    i += 2;
                else if (c == '`') {
                    state = CT_STATE_CODE;
                    i++;
                } else {
                    i++;
                }
                break;
            case CT_STATE_CHAR:
                if (c == '\\' && i + 1 < len)
                    i += 2;
                else if (c == '\'') {
                    state = CT_STATE_CODE;
                    i++;
                } else {
                    i++;
                }
                break;
            }
        }

        if (state == CT_STATE_LINE_COMMENT)
            state = CT_STATE_CODE;

        lp = lforw(lp);
    }

    flush_typedef_candidate(&typedef_pending, typedef_candidate);
}

void custom_types_mark_dirty(struct buffer *bp)
{
    if (!bp)
        return;
    if (tracked_buffer == bp)
        tracked_dirty = true;
}

void custom_types_ensure(struct buffer *bp)
{
    if (!bp)
        return;
    if (tracked_buffer != bp) {
        tracked_buffer = bp;
        tracked_dirty = true;
    }
    if (!tracked_dirty)
        return;
    rebuild_custom_types(bp);
    tracked_dirty = false;
}

int custom_types_count(void)
{
    return custom_type_count;
}

const char *custom_types_get(int index)
{
    if (index < 0 || index >= custom_type_count)
        return NULL;
    return custom_type_names[index];
}

int custom_types_contains(const char *word)
{
    if (!word || !*word)
        return 0;
    for (int i = 0; i < custom_type_count; i++) {
        if (strcmp(custom_type_names[i], word) == 0)
            return 1;
    }
    return 0;
}
