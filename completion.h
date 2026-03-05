#ifndef COMPLETION_H
#define COMPLETION_H

#include "estruct.h"

#define MAX_COMPLETIONS 100
#define MAX_COMPLETION_LEN 128

typedef enum {
    COMPLETION_CONTEXT_DEFAULT = 0,
    COMPLETION_CONTEXT_PATH
} completion_context_t;

typedef struct {
    char *matches[MAX_COMPLETIONS];
    int count;
    int selected_index;
    int is_visible;
    int scroll_offset;
} completion_state_t;

extern completion_state_t completion_state;

void completion_init(void);
void completion_update(const char *prefix, completion_context_t ctx);
void completion_draw(int row, int col);
const char* completion_get_selected(void);
void completion_next(void);
void completion_prev(void);
void completion_hide(void);
int completion_try_at_cursor(void);
int completion_dropdown_is_active(void);
int completion_dropdown_handle_key(int key);
void completion_dropdown_render(void);

#endif /* COMPLETION_H */
