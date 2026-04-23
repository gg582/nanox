#ifndef RENDER_PLUGIN_H
#define RENDER_PLUGIN_H

#include "estruct.h"
#include "highlight.h"

typedef struct {
    struct window *wp;
    struct line *lp;
    int visual_row;
    int visual_col;
    int target_col_start;
    int target_col_end;
    HighlightStyleID current_style;
    int line_num;
} render_ctx_t;

typedef enum {
    RENDER_HOOK_PRE_LINE,
    RENDER_HOOK_POST_LINE,
    RENDER_HOOK_INLINE,
    RENDER_HOOK_GUTTER
} render_hook_type_t;

typedef void (*render_hook_fn)(render_ctx_t *ctx);

typedef struct {
    render_hook_type_t type;
    render_hook_fn fn;
    void *data;
} render_plugin_t;

void render_plugin_register(render_plugin_t plugin);
void render_plugin_execute(render_hook_type_t type, render_ctx_t *ctx);

#endif
