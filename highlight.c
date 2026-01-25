#include "highlight.h"
#include "colorscheme.h"
#include "platform.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <limits.h>

#ifdef USE_WINDOWS
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct {
    bool enable_colorscheme;
    char colorscheme_name[64];
} HighlightGlobalConfig;

static HighlightGlobalConfig global_config;
static HighlightProfile profiles[MAX_PROFILES];
static int profile_count = 0;
static bool initialized = false;

static void profile_init(HighlightProfile *p, const char *name)
{
    memset(p, 0, sizeof(*p));
    mystrscpy(p->name, name, sizeof(p->name));
    p->enable_number_highlight = true;
    p->enable_bracket_highlight = true;
    p->enable_triple_quotes = false;
    /* Defaults could be more extensive, but usually config overrides */
}

static HighlightProfile *prepare_profile(const char *name)
{
    for (int i = 0; i < profile_count; i++) {
        if (strcasecmp(profiles[i].name, name) == 0) {
            profile_init(&profiles[i], name);
            return &profiles[i];
        }
    }
    if (profile_count < MAX_PROFILES) {
        HighlightProfile *slot = &profiles[profile_count++];
        profile_init(slot, name);
        return slot;
    }
    return NULL;
}

/* Helper to trim whitespace */
static char *trim(char *s)
{
    char *p = s;
    while (isspace(*p))
        p++;
    if (*p == 0)
        return p;
    char *end = p + strlen(p) - 1;
    while (end > p && isspace(*end))
        *end-- = 0;
    return p;
}

static bool load_config_file(const char *path, bool allow_global)
{
    if (!path || !*path)
        return false;

    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[512];
    HighlightProfile *curr = NULL;
    bool ignore_section = false;
    bool added = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == 0 || *p == ';' || *p == '#')
            continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end)
                continue;
            *end = 0;
            char *sect = p + 1;

            if (strcasecmp(sect, "highlight") == 0) {
                if (allow_global) {
                    curr = NULL;
                    ignore_section = false;
                } else {
                    ignore_section = true;
                }
            } else {
                curr = prepare_profile(sect);
                if (curr) {
                    ignore_section = false;
                    added = true;
                } else {
                    ignore_section = true;
                }
            }
            continue;
        }

        if (ignore_section)
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = trim(p);
        char *val = trim(eq + 1);

        if (!curr) {
            if (!allow_global)
                continue;

            if (strcmp(key, "enable_colorscheme") == 0)
                global_config.enable_colorscheme = (strcasecmp(val, "true") == 0);
            else if (strcmp(key, "colorscheme") == 0)
                mystrscpy(global_config.colorscheme_name, val, sizeof(global_config.colorscheme_name));
            continue;
        }

        if (strcmp(key, "extensions") == 0) {
            char *tok = strtok(val, ",");
            curr->ext_count = 0;
            while (tok && curr->ext_count < MAX_EXTS) {
                mystrscpy(curr->extensions[curr->ext_count++], trim(tok), MAX_EXT_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "line_comment_tokens") == 0) {
            char *tok = strtok(val, ",");
            curr->line_comment_count = 0;
            while (tok && curr->line_comment_count < MAX_TOKENS) {
                mystrscpy(curr->line_comments[curr->line_comment_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "block_comment_pairs") == 0) {
            char *tok = strtok(val, ",");
            curr->block_comment_count = 0;
            while (tok && curr->block_comment_count < MAX_TOKENS) {
                tok = trim(tok);
                char *sp = strchr(tok, ' ');
                if (sp) {
                    *sp = 0;
                    mystrscpy(curr->block_comments[curr->block_comment_count].start, tok, MAX_TOKEN_LEN);
                    mystrscpy(curr->block_comments[curr->block_comment_count].end, trim(sp + 1), MAX_TOKEN_LEN);
                    curr->block_comment_count++;
                }
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "string_delims") == 0) {
            int j = 0;
            for (int i = 0; val[i]; i++) {
                if (val[i] != ',' && !isspace((unsigned char)val[i]) && j < MAX_TOKENS - 1)
                    curr->string_delims[j++] = val[i];
            }
            curr->string_delims[j] = 0;
        } else if (strcmp(key, "keywords") == 0) {
            char *tok = strtok(val, ",");
            curr->keyword_count = 0;
            while (tok && curr->keyword_count < MAX_TOKENS * 8) {
                mystrscpy(curr->keywords[curr->keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "types") == 0) {
            char *tok = strtok(val, ",");
            curr->type_keyword_count = 0;
            while (tok && curr->type_keyword_count < MAX_TOKENS * 8) {
                mystrscpy(curr->type_keywords[curr->type_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "flow") == 0) {
            char *tok = strtok(val, ",");
            curr->flow_keyword_count = 0;
            while (tok && curr->flow_keyword_count < MAX_TOKENS * 8) {
                mystrscpy(curr->flow_keywords[curr->flow_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "preproc") == 0) {
            char *tok = strtok(val, ",");
            curr->preproc_keyword_count = 0;
            while (tok && curr->preproc_keyword_count < MAX_TOKENS * 4) {
                mystrscpy(curr->preproc_keywords[curr->preproc_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "return_keywords") == 0) {
            char *tok = strtok(val, ",");
            curr->return_keyword_count = 0;
            while (tok && curr->return_keyword_count < MAX_TOKENS) {
                mystrscpy(curr->return_keywords[curr->return_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "enable_triple_quotes") == 0) {
            curr->enable_triple_quotes = (strcasecmp(val, "true") == 0);
        } else if (strcmp(key, "enable_number_highlight") == 0) {
            curr->enable_number_highlight = (strcasecmp(val, "true") == 0);
        } else if (strcmp(key, "enable_bracket_highlight") == 0) {
            curr->enable_bracket_highlight = (strcasecmp(val, "true") == 0);
        }
    }

    fclose(f);
    return added;
}

static bool load_lang_dir(const char *dir)
{
    if (!dir || !*dir)
        return false;

    bool loaded = false;
#ifdef USE_WINDOWS
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*.ini", dir);
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        const char *ext = strrchr(data.cFileName, '.');
        if (!ext || strcasecmp(ext + 1, "ini") != 0)
            continue;
        char path[PATH_MAX];
        nanox_path_join(path, sizeof(path), dir, data.cFileName);
        if (load_config_file(path, false))
            loaded = true;
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
#else
    DIR *dp = opendir(dir);
    if (!dp)
        return false;
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcasecmp(ext + 1, "ini") != 0)
            continue;
        char path[PATH_MAX];
        nanox_path_join(path, sizeof(path), dir, entry->d_name);
        if (!path[0])
            continue;
        if (load_config_file(path, false))
            loaded = true;
    }
    closedir(dp);
#endif
    return loaded;
}

static bool load_external_langs(const char *rule_config_path)
{
    bool loaded = false;
    char dir[PATH_MAX];
    char lang_path[PATH_MAX];
    bool tried_repo_langs = false;

    if (rule_config_path && *rule_config_path) {
        char base[PATH_MAX];
        mystrscpy(base, rule_config_path, sizeof(base));
        char *sep = strrchr(base, '/');
#ifdef USE_WINDOWS
        char *bsep = strrchr(base, '\\');
        if (!sep || (bsep && bsep > sep))
            sep = bsep;
#endif
        if (sep) {
            *sep = 0;
            nanox_path_join(lang_path, sizeof(lang_path), base, "langs");
            if (lang_path[0]) {
                loaded |= load_lang_dir(lang_path);
                if (strcmp(lang_path, "configs/nanox/langs") == 0)
                    tried_repo_langs = true;
            }
        }
    }

    nanox_get_user_config_dir(dir, sizeof(dir));
    if (dir[0]) {
        nanox_path_join(lang_path, sizeof(lang_path), dir, "langs");
        loaded |= load_lang_dir(lang_path);
    }

    nanox_get_user_data_dir(dir, sizeof(dir));
    if (dir[0]) {
        nanox_path_join(lang_path, sizeof(lang_path), dir, "langs");
        loaded |= load_lang_dir(lang_path);
    }

    if (!tried_repo_langs)
        loaded |= load_lang_dir("configs/nanox/langs");
    return loaded;
}

void highlight_init(const char *rule_config_path)
{
    profile_count = 0;
    global_config.enable_colorscheme = true;
    mystrscpy(global_config.colorscheme_name, "nanox-dark", sizeof(global_config.colorscheme_name));

    bool loaded_any = false;

    if (rule_config_path && *rule_config_path)
        loaded_any |= load_config_file(rule_config_path, true);

    loaded_any |= load_external_langs(rule_config_path);

    if (global_config.enable_colorscheme) {
        colorscheme_init(global_config.colorscheme_name);
    }
    initialized = (profile_count > 0) && loaded_any;
}

bool highlight_is_enabled(void)
{
    return initialized && global_config.enable_colorscheme;
}

const HighlightProfile *highlight_get_profile(const char *filename)
{
    if (!filename || !*filename)
        return NULL;
    const char *ext = strrchr(filename, '.');
    if (!ext)
        return NULL; /* Or check for exact filename matches like Makefile? */
    ext++; /* Skip dot */
    
    /* Special handling for no-extension files? */
    
    for (int i = 0; i < profile_count; i++) {
        for (int j = 0; j < profiles[i].ext_count; j++) {
            if (strcasecmp(ext, profiles[i].extensions[j]) == 0)
                return &profiles[i];
        }
    }
    return NULL;
}

static void add_span(SpanVec *vec, int start, int end, HighlightStyleID style)
{
    if (start >= end)
        return;

    Span s = {start, end, style};

    if (vec->count < HL_MAX_SPANS) {
        vec->spans[vec->count++] = s;
    } else {
        if (!vec->heap_spans) {
            vec->capacity = HL_MAX_SPANS * 2;
            vec->heap_spans = malloc(sizeof(Span) * vec->capacity);
            /* Copy existing */
            memcpy(vec->heap_spans, vec->spans, sizeof(Span) * HL_MAX_SPANS);
        } else if (vec->count >= vec->capacity) {
            vec->capacity *= 2;
            vec->heap_spans = realloc(vec->heap_spans, sizeof(Span) * vec->capacity);
        }
        if (vec->heap_spans)
            vec->heap_spans[vec->count++] = s;
    }
}

void span_vec_free(SpanVec *vec)
{
    if (vec->heap_spans) {
        free(vec->heap_spans);
        vec->heap_spans = NULL;
    }
    vec->count = 0;
}

static bool is_punct(char c)
{
    return strchr("()[]{},;:.", c) != NULL;
}

static bool is_operator(char c)
{
    return strchr("+-*/%=&|<>!^~", c) != NULL;
}

static bool starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool is_control(unsigned char c)
{
    return (c < 32 && c != '\t' && c != '\n' && c != '\r') || c == 127;
}

void highlight_line(const char *text, int len, HighlightState start, const HighlightProfile *profile, SpanVec *out, HighlightState *end)
{
    out->count = 0;
    out->heap_spans = NULL;
    out->capacity = 0;
    *end = start;

    if (!text) len = 0;
    if (len < 0 && text) len = strlen(text);
    
    if (!profile) return;

    HighlightState state = start;
    int pos = 0;

    while (pos < len) {
        unsigned char c = (unsigned char)text[pos];

        if (state.state == HS_NORMAL) {
            /* 0. Control characters */
            if (is_control(c)) {
                add_span(out, pos, pos + 1, HL_CONTROL);
                pos++;
                continue;
            }

            /* 0b. Trailing whitespace */
            if (isspace(c)) {
                bool trailing = true;
                for (int i = pos; i < len; i++) {
                    if (!isspace((unsigned char)text[i])) {
                        trailing = false;
                        break;
                    }
                }
                if (trailing) {
                    add_span(out, pos, len, HL_CONTROL);
                    pos = len;
                    continue;
                }
            }

            /* 1. Check Block Comments */
            bool matched_block = false;
            for (int i = 0; i < profile->block_comment_count; i++) {
                if (starts_with(text + pos, profile->block_comments[i].start)) {
                    state.state = HS_BLOCK_COMMENT;
                    state.sub_id = i;
                    
                    int start_len = strlen(profile->block_comments[i].start);
                    int search_pos = pos + start_len;
                    bool found_end = false;
                    const char *end_str = profile->block_comments[i].end;
                    int end_len = strlen(end_str);

                    while (search_pos <= len - end_len) {
                        if (strncmp(text + search_pos, end_str, end_len) == 0) {
                            found_end = true;
                            search_pos += end_len;
                            break;
                        }
                        search_pos++;
                    }

                    if (found_end) {
                        add_span(out, pos, search_pos, HL_COMMENT);
                        pos = search_pos;
                        state.state = HS_NORMAL;
                    } else {
                        add_span(out, pos, len, HL_COMMENT);
                        pos = len;
                    }
                    matched_block = true;
                    break;
                }
            }
            if (matched_block) continue;

            /* 2. Check Line Comments */
            bool matched_line = false;
            for (int i = 0; i < profile->line_comment_count; i++) {
                if (starts_with(text + pos, profile->line_comments[i])) {
                    add_span(out, pos, len, HL_COMMENT);
                    pos = len;
                    matched_line = true;
                    break;
                }
            }
            if (matched_line) continue;

            /* 3. Preprocessor (C-style starting with #) */
            if (c == '#' && (pos == 0 || isspace((unsigned char)text[pos-1]))) {
                int search = pos + 1;
                while (search < len && (isalnum((unsigned char)text[search]) || text[search] == '_')) {
                    search++;
                }
                add_span(out, pos, search, HL_PREPROC);
                pos = search;
                continue;
            }

            /* 4. Check Strings */
            if (profile->enable_triple_quotes && strncmp(text + pos, "\"\"\"", 3) == 0) {
                state.state = HS_TRIPLE_STRING;
                state.sub_id = '\"';
                add_span(out, pos, pos + 3, HL_STRING);
                pos += 3;
                continue;
            }

            char *delim_ptr = strchr(profile->string_delims, c);
            if (delim_ptr && *delim_ptr) {
                state.state = HS_STRING;
                state.sub_id = c;
                add_span(out, pos, pos + 1, HL_STRING);
                pos++;
                continue;
            }

            /* 5. Numbers */
            if (profile->enable_number_highlight && (isdigit(c) || (c == '.' && isdigit((unsigned char)text[pos+1])))) {
                int search = pos;
                if (c == '0' && pos + 1 < len) {
                    char next = text[pos+1];
                    if (next == 'x' || next == 'X') {
                        search += 2;
                        while (search < len && isxdigit((unsigned char)text[search])) search++;
                    } else if (next == 'b' || next == 'B') {
                        search += 2;
                        while (search < len && (text[search] == '0' || text[search] == '1')) search++;
                    } else if (isdigit((unsigned char)next)) {
                        search += 1;
                        while (search < len && isdigit((unsigned char)text[search])) search++;
                    } else {
                        search++;
                    }
                } else {
                    while (search < len && (isdigit((unsigned char)text[search]) || text[search] == '.' || 
                        text[search] == 'e' || text[search] == 'E')) {
                        if ((text[search] == 'e' || text[search] == 'E') && (text[search+1] == '+' || text[search+1] == '-'))
                            search++;
                        search++;
                    }
                }
                /* Suffixes */
                while (search < len && strchr("uUlLfF", text[search])) search++;
                
                add_span(out, pos, search, HL_NUMBER);
                pos = search;
                continue;
            }

            /* 6. Punctuation and Operators */
            if (profile->enable_bracket_highlight && (c == '?' || c == ':')) {
                add_span(out, pos, pos + 1, HL_TERNARY);
                pos++;
                continue;
            }
            if (profile->enable_bracket_highlight && is_punct(c)) {
                add_span(out, pos, pos + 1, HL_BRACKET);
                pos++;
                continue;
            }
            if (profile->enable_bracket_highlight && is_operator(c)) {
                add_span(out, pos, pos + 1, HL_OPERATOR);
                pos++;
                continue;
            }

            /* 7. Words (Keywords, Types, Flow, Functions) */
            int next_stop = pos;
            while (next_stop < len && (isalnum((unsigned char)text[next_stop]) || text[next_stop] == '_')) {
                next_stop++;
            }

            if (next_stop > pos) {
                char word[MAX_TOKEN_LEN];
                int word_len = next_stop - pos;
                HighlightStyleID style = HL_NORMAL;

                if (word_len < MAX_TOKEN_LEN) {
                    memcpy(word, text + pos, word_len);
                    word[word_len] = 0;

                    bool found = false;
                    /* Order of precedence: Return > Flow > Preproc > Type > Keyword */
                    for (int i = 0; i < profile->return_keyword_count; i++) {
                        if (strcmp(word, profile->return_keywords[i]) == 0) {
                            style = HL_RETURN; found = true; break;
                        }
                    }
                    if (!found) {
                        for (int i = 0; i < profile->flow_keyword_count; i++) {
                            if (strcmp(word, profile->flow_keywords[i]) == 0) {
                                style = HL_FLOW; found = true; break;
                            }
                        }
                    }
                    if (!found) {
                        for (int i = 0; i < profile->preproc_keyword_count; i++) {
                            if (strcmp(word, profile->preproc_keywords[i]) == 0) {
                                style = HL_PREPROC; found = true; break;
                            }
                        }
                    }
                    if (!found) {
                        for (int i = 0; i < profile->type_keyword_count; i++) {
                            if (strcmp(word, profile->type_keywords[i]) == 0) {
                                style = HL_TYPE; found = true; break;
                            }
                        }
                    }
                    if (!found) {
                        for (int i = 0; i < profile->keyword_count; i++) {
                            if (strcmp(word, profile->keywords[i]) == 0) {
                                style = HL_KEYWORD; found = true; break;
                            }
                        }
                    }
                    
                    /* Function detection: identifier followed by '(' */
                    if (!found) {
                        int s = next_stop;
                        while (s < len && isspace((unsigned char)text[s])) s++;
                        if (s < len && text[s] == '(') {
                            style = HL_FUNCTION;
                        }
                    }
                }
                add_span(out, pos, next_stop, style);
                pos = next_stop;
                continue;
            }

            /* Fallback for other characters */
            add_span(out, pos, pos + 1, HL_NORMAL);
            pos++;

        } else if (state.state == HS_BLOCK_COMMENT) {
            int idx = state.sub_id;
            const char *end_str = profile->block_comments[idx].end;
            int end_len = strlen(end_str);
            int search = pos;
            bool found_end = false;
            while (search <= len - end_len) {
                if (strncmp(text + search, end_str, end_len) == 0) {
                    found_end = true;
                    search += end_len;
                    break;
                }
                search++;
            }
            if (found_end) {
                add_span(out, pos, search, HL_COMMENT);
                pos = search;
                state.state = HS_NORMAL;
            } else {
                add_span(out, pos, len, HL_COMMENT);
                pos = len;
            }
        } else if (state.state == HS_STRING || state.state == HS_TRIPLE_STRING) {
            char delim = (char)state.sub_id;
            bool is_triple = (state.state == HS_TRIPLE_STRING);
            
            if (text[pos] == '\\') {
                int esc_len = 2;
                if (pos + 1 < len) {
                    if (text[pos+1] == 'x') {
                        esc_len = 2;
                        while (pos + esc_len < len && isxdigit((unsigned char)text[pos+esc_len]) && esc_len < 4) esc_len++;
                    } else if (isdigit((unsigned char)text[pos+1])) {
                        esc_len = 2;
                        while (pos + esc_len < len && isdigit((unsigned char)text[pos+esc_len]) && esc_len < 4) esc_len++;
                    }
                }
                add_span(out, pos, pos + esc_len, HL_ESCAPE);
                pos += esc_len;
                continue;
            }
            
            if (is_triple) {
                if (starts_with(text + pos, "\"\"\"")) {
                    add_span(out, pos, pos + 3, HL_STRING);
                    pos += 3;
                    state.state = HS_NORMAL;
                    continue;
                }
            } else {
                if (text[pos] == delim) {
                    add_span(out, pos, pos + 1, HL_STRING);
                    pos++;
                    state.state = HS_NORMAL;
                    continue;
                }
            }
            
            /* Just normal string character */
            add_span(out, pos, pos + 1, HL_STRING);
            pos++;
        }
    }
    *end = state;
}
