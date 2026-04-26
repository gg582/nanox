#ifndef VIBLOCK_STRATEGY_H_
#define VIBLOCK_STRATEGY_H_

#include "estruct.h"

typedef struct ViblockStrategy ViblockStrategy;

struct ViblockStrategy {
    const char *name;
    int (*apply)(const char *input);
    void (*render_status)(int top, int bottom, int left, int right, int reverse);
};

/* Block modes */
typedef enum {
    BLOCK_MODE_NONE = 0,
    BLOCK_MODE_EDIT,
    BLOCK_MODE_REPLACE,
    BLOCK_MODE_SET_NR
} BlockMode;

/* External API */
void viblock_start(BlockMode mode, int reverse_flag);
void viblock_reset(void);
int viblock_handle_key(int c, int f, int n);
void viblock_render_status(void);
int viblock_is_active(void);
int viblock_selection_contains(struct line *lp, int col_start, int col_end);
int viblock_apply_flip(int start1, int end1, int start2, int end2);

#endif
