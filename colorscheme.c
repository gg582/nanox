#include "colorscheme.h"
#include "platform.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 256
#define MAX_NAME 64

static char current_scheme_name[MAX_NAME] = "default";
static HighlightStyle styles[HL_COUNT];

/* Built-in fallback scheme */
static void set_default_scheme(void) {
    /* Normal: default fg/bg */
    styles[HL_NORMAL] = (HighlightStyle){-1, -1, false, false};
    /* Comment: Bright Black (Gray) */
    styles[HL_COMMENT] = (HighlightStyle){8, -1, false, false};
    /* String: Green */
    styles[HL_STRING] = (HighlightStyle){2, -1, false, false};
    /* Number: Magenta */
    styles[HL_NUMBER] = (HighlightStyle){5, -1, false, false};
    /* Bracket: Cyan */
    styles[HL_BRACKET] = (HighlightStyle){6, -1, false, false};
    /* Operator: Cyan */
    styles[HL_OPERATOR] = (HighlightStyle){6, -1, false, false};
    /* Keyword: Yellow */
    styles[HL_KEYWORD] = (HighlightStyle){3, -1, true, false};
    /* Type: Cyan */
    styles[HL_TYPE] = (HighlightStyle){6, -1, false, false};
    /* Function: Blue */
    styles[HL_FUNCTION] = (HighlightStyle){4, -1, false, false};
    /* Flow: Yellow */
    styles[HL_FLOW] = (HighlightStyle){3, -1, true, false};
    /* Preproc: Red */
    styles[HL_PREPROC] = (HighlightStyle){1, -1, false, false};
    /* Return: Bright Red */
    styles[HL_RETURN] = (HighlightStyle){9, -1, true, false};
    /* Escape: Bright Cyan */
    styles[HL_ESCAPE] = (HighlightStyle){14, -1, false, false};
    /* Control: Bright Red */
    styles[HL_CONTROL] = (HighlightStyle){9, -1, false, true};
    /* Ternary: Yellow */
    styles[HL_TERNARY] = (HighlightStyle){3, -1, true, false};
    /* Error: Bright Red */
    styles[HL_ERROR] = (HighlightStyle){9, -1, false, false};
    /* Notice: Orange/Gold */
    styles[HL_NOTICE] = (HighlightStyle){208, -1, true, false};
}

static int parse_color(const char *val) {
    if (strcmp(val, "default") == 0) return -1;
    
    /* True Color: #RRGGBB */
    if (val[0] == '#') {
        int r, g, b;
        if (sscanf(val + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            /* Pack as 0x01RRGGBB to distinguish from ANSI 0-255 */
            return 0x01000000 | (r << 16) | (g << 8) | b;
        }
    }

    int offset = 0;
    if (strncmp(val, "bright_", 7) == 0) {
        offset = 8;
        val += 7;
    }

    if (strcmp(val, "black") == 0) return 0 + offset;
    if (strcmp(val, "red") == 0) return 1 + offset;
    if (strcmp(val, "green") == 0) return 2 + offset;
    if (strcmp(val, "yellow") == 0) return 3 + offset;
    if (strcmp(val, "blue") == 0) return 4 + offset;
    if (strcmp(val, "magenta") == 0) return 5 + offset;
    if (strcmp(val, "cyan") == 0) return 6 + offset;
    if (strcmp(val, "white") == 0) return 7 + offset;

    return -1;
}

static void parse_attributes(char *value, HighlightStyle *style) {
    char *token = strtok(value, " ");
    while (token) {
        if (strncmp(token, "fg=", 3) == 0) {
            style->fg = parse_color(token + 3);
        } else if (strncmp(token, "bg=", 3) == 0) {
            style->bg = parse_color(token + 3);
        } else if (strcmp(token, "bold=true") == 0) {
            style->bold = true;
        } else if (strcmp(token, "underline=true") == 0) {
            style->underline = true;
        }
        token = strtok(NULL, " ");
    }
}

static bool is_safe_name(const char *name) {
    for (; *name; name++) {
        if (!isalnum(*name) && *name != '.' && *name != '_' && *name != '-') return false;
    }
    return true;
}

static void load_scheme_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[MAX_LINE];
    bool in_styles = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace(*p)) p++;
        if (*p == 0 || *p == ';' || *p == '#') continue;

        /* Strip trailing whitespace/newlines */
        size_t len = strlen(p);
        while (len > 0 && isspace(p[len-1])) p[--len] = 0;

        if (p[0] == '[') {
            if (strcmp(p, "[styles]") == 0) in_styles = true;
            else in_styles = false;
            continue;
        }

        if (in_styles) {
            char *eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            char *key = p;
            char *val = eq + 1;
            while (isspace(*val)) val++;
            while (len > 0 && isspace(key[strlen(key)-1])) key[strlen(key)-1] = 0;

            HighlightStyleID id = HL_COUNT;
            if (strcmp(key, "normal") == 0) id = HL_NORMAL;
            else if (strcmp(key, "comment") == 0) id = HL_COMMENT;
            else if (strcmp(key, "string") == 0) id = HL_STRING;
            else if (strcmp(key, "number") == 0) id = HL_NUMBER;
            else if (strcmp(key, "bracket") == 0) id = HL_BRACKET;
            else if (strcmp(key, "operator") == 0) id = HL_OPERATOR;
            else if (strcmp(key, "keyword") == 0) id = HL_KEYWORD;
            else if (strcmp(key, "type") == 0) id = HL_TYPE;
            else if (strcmp(key, "function") == 0) id = HL_FUNCTION;
            else if (strcmp(key, "flow") == 0) id = HL_FLOW;
            else if (strcmp(key, "preproc") == 0) id = HL_PREPROC;
            else if (strcmp(key, "return") == 0) id = HL_RETURN;
            else if (strcmp(key, "escape") == 0) id = HL_ESCAPE;
            else if (strcmp(key, "control") == 0) id = HL_CONTROL;
            else if (strcmp(key, "ternary") == 0) id = HL_TERNARY;
            else if (strcmp(key, "error") == 0) id = HL_ERROR;
            else if (strcmp(key, "notice") == 0) id = HL_NOTICE;
            
                    if (id != HL_COUNT) {                parse_attributes(val, &styles[id]);
            }
        }
    }
    fclose(f);

    /* Propagate normal background to other styles if they are unset */
    if (styles[HL_NORMAL].bg != -1) {
        for (int i = 0; i < HL_COUNT; i++) {
            if (i == HL_NORMAL) continue;
            if (styles[i].bg == -1) {
                styles[i].bg = styles[HL_NORMAL].bg;
            }
        }
    }
}

void colorscheme_init(const char *requested_name) {
    set_default_scheme();
    
    char name[MAX_NAME] = {0};

    /* Priority 1: Env Var */
    const char *env_scheme = nanox_getenv("NANOX_COLORSCHEME");
    if (env_scheme && *env_scheme && is_safe_name(env_scheme)) {
        mystrscpy(name, env_scheme, MAX_NAME);
    } 
    /* Priority 2: Config/Argument */
    else if (requested_name && *requested_name && is_safe_name(requested_name)) {
        mystrscpy(name, requested_name, MAX_NAME);
    }
    /* Priority 3: Default */
    else {
        strcpy(name, "default");
    }

    if (strcmp(name, "default") == 0) return;

    /* Resolve path */
    char dir[512];
    char path[1024];
    char file_name[MAX_NAME + 12];
    
    /* 1. Try Config Dir */
    nanox_get_user_config_dir(dir, sizeof(dir));
    nanox_path_join(dir, sizeof(dir), dir, "colorscheme");

    /* Try .nanoxcolor */
    snprintf(file_name, sizeof(file_name), "%s.nanoxcolor", name);
    nanox_path_join(path, sizeof(path), dir, file_name);
    if (nanox_file_exists(path)) {
        load_scheme_file(path);
        mystrscpy(current_scheme_name, name, MAX_NAME);
        return;
    }
    /* Try .ini */
    snprintf(file_name, sizeof(file_name), "%s.ini", name);
    nanox_path_join(path, sizeof(path), dir, file_name);
    if (nanox_file_exists(path)) {
        load_scheme_file(path);
        mystrscpy(current_scheme_name, name, MAX_NAME);
        return;
    }

    /* 2. Try Data Dir */
    nanox_get_user_data_dir(dir, sizeof(dir));
    nanox_path_join(dir, sizeof(dir), dir, "colorscheme");
    
    /* Try .nanoxcolor */
    snprintf(file_name, sizeof(file_name), "%s.nanoxcolor", name);
    nanox_path_join(path, sizeof(path), dir, file_name);
    
    if (nanox_file_exists(path)) {
        load_scheme_file(path);
        mystrscpy(current_scheme_name, name, MAX_NAME);
        return;
    }

    /* Try .ini */
    snprintf(file_name, sizeof(file_name), "%s.ini", name);
    nanox_path_join(path, sizeof(path), dir, file_name);
    
    if (nanox_file_exists(path)) {
        load_scheme_file(path);
        mystrscpy(current_scheme_name, name, MAX_NAME);
        return;
    }
}

HighlightStyle colorscheme_get(HighlightStyleID id) {
    if (id < 0 || id >= HL_COUNT) return styles[HL_NORMAL];
    return styles[id];
}

const char *colorscheme_get_name(void) {
    return current_scheme_name;
}
