#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "estruct.h"
#include "efunc.h"
#include "completion.h"
#include "util.h"
#include "utf8.h"

completion_state_t completion_state;

static char **all_words = NULL;
static int all_words_count = 0;
static int all_words_capacity = 0;

static const char *common_keywords[] = {
    "if", "else", "while", "for", "do", "return", "break", "continue", "switch", "case", "default",
    "int", "char", "float", "double", "void", "struct", "union", "enum", "typedef", "static", "extern",
    "include", "define", "ifdef", "ifndef", "endif", "import", "from", "as", "def", "class", "try", "except",
    "finally", "with", "yield", "lambda", "assert", "pass", "None", "True", "False", "and", "or", "not", "is", "in",
    "let", "const", "var", "function", "async", "await", "promise", "then", "catch", "export",
    "fn", "mut", "match", "use", "mod", "pub", "impl", "trait", "type", "where", "crate", "self", "super",
    "sizeof", "alignas", "alignof", "bool", "static_assert", "thread_local", "template", "typename", "mutable", "virtual", "override"
};

void add_word(const char *word) {
    if (word == NULL || *word == '\0') return;
    
    /* Check for duplicates */
    for (int i = 0; i < all_words_count; i++) {
        if (strcmp(all_words[i], word) == 0) return;
    }

    if (all_words_count >= all_words_capacity) {
        all_words_capacity = all_words_capacity == 0 ? 256 : all_words_capacity * 2;
        all_words = realloc(all_words, sizeof(char *) * (size_t)all_words_capacity);
    }
    all_words[all_words_count++] = strdup(word);
}

static int initialized = 0;

void completion_init(void) {
    if (initialized) {
        completion_state.count = 0;
        completion_state.selected_index = 0;
        completion_state.is_visible = 0;
        return;
    }

    completion_state.count = 0;
    completion_state.selected_index = 0;
    completion_state.is_visible = 0;

    /* Add common keywords */
    for (size_t i = 0; i < sizeof(common_keywords) / sizeof(common_keywords[0]); i++) {
        add_word(common_keywords[i]);
    }

    /* Try to load system libraries */
    FILE *fp = popen("/sbin/ldconfig -p", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            /* Format: \tlibname.so.X (libc6,x86-64) => /path/to/libname.so.X */
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p;
            while (*end && *end != ' ' && *end != '\t' && *end != '(') end++;
            if (end > p) {
                char temp = *end;
                *end = '\0';
                /* Extract base name if it starts with lib */
                if (strncmp(p, "lib", 3) == 0) {
                    add_word(p);
                    /* Also add name without lib and without .so... */
                    char *name = strdup(p + 3);
                    char *dot = strchr(name, '.');
                    if (dot) {
                        *dot = '\0';
                    }
                    add_word(name);
                    free(name);
                }
                *end = temp;
            }
        }
        pclose(fp);
    }
    initialized = 1;
}

void completion_update(const char *prefix) {
    if (prefix == NULL || *prefix == '\0') {
        completion_state.count = 0;
        completion_state.is_visible = 0;
        return;
    }

    completion_state.count = 0;
    completion_state.selected_index = 0;
    
    for (int i = 0; i < all_words_count && completion_state.count < MAX_COMPLETIONS; i++) {
        if (strncmp(all_words[i], prefix, strlen(prefix)) == 0) {
            completion_state.matches[completion_state.count++] = all_words[i];
        }
    }

    completion_state.is_visible = (completion_state.count > 0);
}

void completion_draw(int row, int col) {
    if (!completion_state.is_visible) return;

    int height = completion_state.count;
    if (height > 5) height = 5; /* Limit height */

    for (int i = 0; i < height; i++) {
        int r = row - height + i;
        if (r < 0) continue;

        movecursor(r, col);
        if (i == completion_state.selected_index) {
            TTrev(TRUE);
        }
        
        char buf[MAX_COMPLETION_LEN];
        snprintf(buf, sizeof(buf), " %-20s ", completion_state.matches[i]);
        
        /* Display with proper UTF-8 handling if needed, but these are mostly ASCII */
        for (int j = 0; buf[j]; j++) {
            TTputc(buf[j]);
        }

        if (i == completion_state.selected_index) {
            TTrev(FALSE);
        }
    }
    TTflush();
}

const char* completion_get_selected(void) {
    if (completion_state.is_visible && completion_state.selected_index < completion_state.count) {
        return completion_state.matches[completion_state.selected_index];
    }
    return NULL;
}

void completion_next(void) {
    if (completion_state.count > 0) {
        completion_state.selected_index = (completion_state.selected_index + 1) % completion_state.count;
        if (completion_state.selected_index >= 5) {
            /* Scroll logic could be added here if we had more items visible */
        }
    }
}

void completion_prev(void) {
    if (completion_state.count > 0) {
        completion_state.selected_index = (completion_state.selected_index - 1 + completion_state.count) % completion_state.count;
    }
}

void completion_hide(void) {
    completion_state.is_visible = 0;
}
