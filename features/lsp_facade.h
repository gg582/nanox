#include "line.h"
#ifndef LSP_FACADE_H_
#define LSP_FACADE_H_

/* LSP Subsystem Facade */
int lsp_facade_is_active(void);
void lsp_facade_provide_completions(struct line *line, int prefix_start, const char *prefix);

#endif
