#include <stdio.h>
#include <stdlib.h>
#include "estruct.h"
#include "edef.h"
#include "line.h"
#include "lsp_facade.h"
#include "nanox.h"

/* Forward declarations for subsystem functions currently in completion.c */
extern int completion_should_use_lsp(void);
extern int get_owner_symbol_near_cursor(struct line *line, int prefix_start, char *owner, size_t owner_sz);
extern int is_java_file(const char *fname);
extern int is_python_file(const char *fname);
extern int is_node_file(const char *fname);
extern int resolve_java_class_name(const char *owner, char *resolved, size_t resolved_sz);
extern void add_java_member_matches(const char *class_name, const char *prefix);
extern void add_runtime_module_matches(int lang_type, const char *owner, const char *prefix);

/* Scraper language types (from scraper.h) */
#define SCRAPER_LANG_PYTHON 1
#define SCRAPER_LANG_NODE   2

int lsp_facade_is_active(void) {
    return completion_should_use_lsp();
}

void lsp_facade_provide_completions(struct line *line, int prefix_start, const char *prefix) {
    char owner[256];
    if (get_owner_symbol_near_cursor(line, prefix_start, owner, sizeof(owner))) {
        const char *fname = curbp ? curbp->b_fname : NULL;
        if (is_java_file(fname)) {
            char resolved[256];
            if (resolve_java_class_name(owner, resolved, sizeof(resolved))) {
                add_java_member_matches(resolved, prefix);
            }
        } else if (is_python_file(fname)) {
            add_runtime_module_matches(SCRAPER_LANG_PYTHON, owner, prefix);
        } else if (is_node_file(fname)) {
            add_runtime_module_matches(SCRAPER_LANG_NODE, owner, prefix);
        }
    }
}
