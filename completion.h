#ifndef COMPLETION_H
#define COMPLETION_H

#include "estruct.h"

#define MAX_COMPLETIONS 100
#define MAX_COMPLETION_LEN 128

typedef struct {
    char *matches[MAX_COMPLETIONS];
    int count;
    int selected_index;
    int is_visible;
} completion_state_t;

extern completion_state_t completion_state;

void completion_init(void);
void completion_update(const char *prefix);
void completion_draw(int row, int col);
const char* completion_get_selected(void);
void completion_next(void);
void completion_prev(void);
void completion_hide(void);

#endif /* COMPLETION_H */
