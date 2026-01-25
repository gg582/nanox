#ifndef HIGHLIGHT_H_
#define HIGHLIGHT_H_

#include "colorscheme.h"
#include <stddef.h>
#include <stdbool.h>

#define HL_MAX_SPANS 256
#define MAX_TOKENS 32
#define MAX_TOKEN_LEN 64
#define MAX_PROFILES 512
#define MAX_EXTS 1232
#define MAX_EXT_LEN 64

typedef struct {
    char start[MAX_TOKEN_LEN];
    char end[MAX_TOKEN_LEN];
} BlockCommentPair;

typedef struct {
    char name[1232];
    char extensions[MAX_EXTS][MAX_EXT_LEN];
    int ext_count;

    char line_comments[MAX_TOKENS][MAX_TOKEN_LEN];
    int line_comment_count;
    
    BlockCommentPair block_comments[MAX_TOKENS];
    int block_comment_count;
    
    char string_delims[MAX_TOKENS]; /* Characters */
    
    char keywords[MAX_TOKENS * 32][MAX_TOKEN_LEN];
    int keyword_count;
    char type_keywords[MAX_TOKENS * 32][MAX_TOKEN_LEN];
    int type_keyword_count;
    char flow_keywords[MAX_TOKENS * 32][MAX_TOKEN_LEN];
    int flow_keyword_count;
    char preproc_keywords[MAX_TOKENS * 4][MAX_TOKEN_LEN];
    int preproc_keyword_count;
    char return_keywords[MAX_TOKENS][MAX_TOKEN_LEN];
    int return_keyword_count;

    bool enable_triple_quotes;
    bool enable_number_highlight;
    bool enable_bracket_highlight;
} HighlightProfile;

typedef struct {
    int start;
    int end;
    HighlightStyleID style;
} Span;

typedef struct {
    Span spans[HL_MAX_SPANS];
    Span *heap_spans;
    int count;
    int capacity;
} SpanVec;

typedef enum {
    HS_NORMAL = 0,
    HS_BLOCK_COMMENT,
    HS_STRING,
    HS_TRIPLE_STRING
} StateID;

typedef struct {
    StateID state;
    int sub_id; /* Index into config arrays for matching pairs */
} HighlightState;

void highlight_init(const char *rule_config_path);
const HighlightProfile *highlight_get_profile(const char *filename);
void highlight_line(const char *text, int len, HighlightState start, const HighlightProfile *profile, SpanVec *out, HighlightState *end);
bool highlight_is_enabled(void);
void span_vec_free(SpanVec *vec);

#endif /* HIGHLIGHT_H_ */
