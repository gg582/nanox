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
#include <regex.h>

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

typedef struct {
    bool compiled;
    regex_t regex;
} CompiledFileMatch;

static CompiledFileMatch profile_file_matches[MAX_PROFILES][MAX_FILE_MATCHES];

#define MAX_MD_LANG_ALIASES 256

typedef struct {
    char standard[MAX_TOKEN_LEN];
    char nanox[MAX_TOKEN_LEN];
} MarkdownLangAlias;

static MarkdownLangAlias markdown_lang_aliases[MAX_MD_LANG_ALIASES];
static int markdown_lang_alias_count = 0;

static int profile_index_from_ptr(const HighlightProfile *p)
{
    if (!p)
        return -1;
    ptrdiff_t idx = p - profiles;
    if (idx < 0 || idx >= MAX_PROFILES)
        return -1;
    return (int)idx;
}

static int profile_index_by_name(const char *name)
{
    if (!name || !*name)
        return -1;
    for (int i = 0; i < profile_count; i++) {
        if (strcasecmp(profiles[i].name, name) == 0)
            return i;
    }
    return -1;
}

static void clear_profile_file_matches(int profile_index)
{
    if (profile_index < 0 || profile_index >= MAX_PROFILES)
        return;
    for (int i = 0; i < MAX_FILE_MATCHES; i++) {
        if (profile_file_matches[profile_index][i].compiled) {
            regfree(&profile_file_matches[profile_index][i].regex);
            profile_file_matches[profile_index][i].compiled = false;
        }
    }
}

static void clear_all_profile_file_matches(void)
{
    for (int i = 0; i < MAX_PROFILES; i++)
        clear_profile_file_matches(i);
}

static bool compile_profile_file_match(int profile_index, int slot, const char *pattern, const char *profile_name)
{
    if (profile_index < 0 || profile_index >= MAX_PROFILES || slot < 0 || slot >= MAX_FILE_MATCHES)
        return false;

    CompiledFileMatch *entry = &profile_file_matches[profile_index][slot];
    int ret = regcomp(&entry->regex, pattern, REG_EXTENDED | REG_NOSUB | REG_ICASE);
    if (ret != 0) {
        char errbuf[128];
        regerror(ret, &entry->regex, errbuf, sizeof(errbuf));
        fprintf(stderr, "nanox: invalid file_matches regex '%s' for profile '%s': %s\n",
                pattern, profile_name ? profile_name : "(unknown)", errbuf);
        entry->compiled = false;
        return false;
    }
    entry->compiled = true;
    return true;
}

static bool profile_matches_filename(int profile_index, const char *basename)
{
    if (profile_index < 0 || profile_index >= profile_count || !basename || !*basename)
        return false;

    for (int i = 0; i < profiles[profile_index].file_match_count; i++) {
        if (!profile_file_matches[profile_index][i].compiled)
            continue;
        if (regexec(&profile_file_matches[profile_index][i].regex, basename, 0, NULL, 0) == 0)
            return true;
    }
    return false;
}

static void profile_init(HighlightProfile *p, const char *name)
{
    int profile_index = profile_index_from_ptr(p);
    if (profile_index >= 0)
        clear_profile_file_matches(profile_index);

    memset(p, 0, sizeof(*p));
    mystrscpy(p->name, name, sizeof(p->name));
    p->enable_number_highlight = true;
    p->enable_bracket_highlight = true;
    p->enable_triple_quotes = false;
    p->completion_end_line_char = '\0';
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
        } else if (strcmp(key, "file_matches") == 0) {
            int profile_index = profile_index_from_ptr(curr);
            if (profile_index >= 0)
                clear_profile_file_matches(profile_index);
            curr->file_match_count = 0;
            char *tok = strtok(val, ",");
            while (tok && curr->file_match_count < MAX_FILE_MATCHES) {
                char *pattern = trim(tok);
                if (!*pattern) {
                    tok = strtok(NULL, ",");
                    continue;
                }
                char pattern_buf[MAX_FILE_MATCH_PATTERN];
                mystrscpy(pattern_buf, pattern, sizeof(pattern_buf));
                bool compiled = true;
                if (profile_index >= 0)
                    compiled = compile_profile_file_match(profile_index, curr->file_match_count, pattern_buf, curr->name);
                if (compiled) {
                    mystrscpy(curr->file_match_patterns[curr->file_match_count], pattern_buf, MAX_FILE_MATCH_PATTERN);
                    curr->file_match_count++;
                }
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
        } else if (strcmp(key, "preproc_include") == 0) {
            char *tok = strtok(val, ",");
            curr->preproc_include_keyword_count = 0;
            while (tok && curr->preproc_include_keyword_count < MAX_TOKENS * 4) {
                mystrscpy(curr->preproc_include_keywords[curr->preproc_include_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "preproc_define") == 0) {
            char *tok = strtok(val, ",");
            curr->preproc_define_keyword_count = 0;
            while (tok && curr->preproc_define_keyword_count < MAX_TOKENS * 4) {
                mystrscpy(curr->preproc_define_keywords[curr->preproc_define_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "preproc_flow") == 0) {
            char *tok = strtok(val, ",");
            curr->preproc_flow_keyword_count = 0;
            while (tok && curr->preproc_flow_keyword_count < MAX_TOKENS * 4) {
                mystrscpy(curr->preproc_flow_keywords[curr->preproc_flow_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "return_keywords") == 0) {
            char *tok = strtok(val, ",");
            curr->return_keyword_count = 0;
            while (tok && curr->return_keyword_count < MAX_TOKENS) {
                mystrscpy(curr->return_keywords[curr->return_keyword_count++], trim(tok), MAX_TOKEN_LEN);
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(key, "completion_end_line_char") == 0) {
            char *trimmed = trim(val);
            curr->completion_end_line_char = (trimmed && *trimmed) ? *trimmed : '\0';
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

static bool add_markdown_lang_alias(const char *standard, const char *nanox)
{
    if (!standard || !*standard || !nanox || !*nanox)
        return false;

    for (int i = 0; i < markdown_lang_alias_count; i++) {
        if (strcasecmp(markdown_lang_aliases[i].standard, standard) == 0) {
            mystrscpy(markdown_lang_aliases[i].nanox, nanox, sizeof(markdown_lang_aliases[i].nanox));
            return true;
        }
    }

    if (markdown_lang_alias_count >= MAX_MD_LANG_ALIASES)
        return false;

    mystrscpy(markdown_lang_aliases[markdown_lang_alias_count].standard, standard,
              sizeof(markdown_lang_aliases[markdown_lang_alias_count].standard));
    mystrscpy(markdown_lang_aliases[markdown_lang_alias_count].nanox, nanox,
              sizeof(markdown_lang_aliases[markdown_lang_alias_count].nanox));
    markdown_lang_alias_count++;
    return true;
}

static bool load_markdown_lang_alias_file(const char *path)
{
    if (!path || !*path)
        return false;

    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[256];
    bool in_alias_section = false;
    bool loaded = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == 0 || *p == ';' || *p == '#')
            continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end)
                continue;
            *end = 0;
            in_alias_section = (strcasecmp(p + 1, "aliases") == 0);
            continue;
        }

        if (!in_alias_section)
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *standard = trim(p);
        char *nanox = trim(eq + 1);
        if (add_markdown_lang_alias(standard, nanox))
            loaded = true;
    }

    fclose(f);
    return loaded;
}

static void load_markdown_lang_aliases(const char *rule_config_path)
{
    markdown_lang_alias_count = 0;

    char alias_path[PATH_MAX];
    char dir[PATH_MAX];

    if (rule_config_path && *rule_config_path) {
        mystrscpy(dir, rule_config_path, sizeof(dir));
        char *sep = strrchr(dir, '/');
#ifdef USE_WINDOWS
        char *bsep = strrchr(dir, '\\');
        if (!sep || (bsep && bsep > sep))
            sep = bsep;
#endif
        if (sep) {
            *sep = 0;
            nanox_path_join(alias_path, sizeof(alias_path), dir, "markdown_lang_map.ini");
            load_markdown_lang_alias_file(alias_path);
        }
    }

    nanox_get_user_config_dir(dir, sizeof(dir));
    if (dir[0]) {
        nanox_path_join(alias_path, sizeof(alias_path), dir, "markdown_lang_map.ini");
        load_markdown_lang_alias_file(alias_path);
    }

    nanox_get_user_data_dir(dir, sizeof(dir));
    if (dir[0]) {
        nanox_path_join(alias_path, sizeof(alias_path), dir, "markdown_lang_map.ini");
        load_markdown_lang_alias_file(alias_path);
    }

    load_markdown_lang_alias_file("configs/nanox/markdown_lang_map.ini");
}

static int resolve_markdown_fence_profile_index(const char *standard_name)
{
    if (!standard_name || !*standard_name)
        return -1;

    for (int i = 0; i < markdown_lang_alias_count; i++) {
        if (strcasecmp(markdown_lang_aliases[i].standard, standard_name) == 0)
            return profile_index_by_name(markdown_lang_aliases[i].nanox);
    }

    return -1;
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
    clear_all_profile_file_matches();
    profile_count = 0;
    global_config.enable_colorscheme = true;
    mystrscpy(global_config.colorscheme_name, "nanox-dark", sizeof(global_config.colorscheme_name));

    bool loaded_any = false;

    if (rule_config_path && *rule_config_path)
        loaded_any |= load_config_file(rule_config_path, true);

    loaded_any |= load_external_langs(rule_config_path);
    load_markdown_lang_aliases(rule_config_path);

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

    const char *base = filename;
    const char *slash = strrchr(base, '/');
#ifdef USE_WINDOWS
    const char *bslash = strrchr(base, '\\');
    if (!slash || (bslash && bslash > slash))
        slash = bslash;
#endif
    if (slash && *(slash + 1))
        base = slash + 1;

    const char *ext = strrchr(base, '.');
    if (ext)
        ext++; /* Skip dot */

    for (int i = 0; i < profile_count; i++) {
        if (profile_matches_filename(i, base))
            return &profiles[i];
    }

    if (!ext)
        return NULL;

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

static bool is_keyword_boundary_char(char c)
{
    if (c == 0)
        return true;
    if (isspace((unsigned char)c))
        return true;

    switch (c) {
    case '(':
    case ')':
    case '{':
    case '}':
    case '[':
    case ']':
    case ',':
    case ';':
    case ':':
    case '?':
    case '!':
    case '%':
    case '+':
    case '-':
    case '*':
    case '/':
    case '&':
    case '|':
    case '^':
    case '~':
    case '=':
    case '<':
    case '>':
    case '\'':
    case '"':
        return true;
    default:
        break;
    }
    return false;
}

static bool starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool is_control(unsigned char c)
{
    return (c < 32 && c != '\t' && c != '\n' && c != '\r') || c == 127;
}

/* Check if file extension matches markdown */
static bool is_markdown_profile(const HighlightProfile *profile)
{
    if (!profile) return false;
    return (strcasecmp(profile->name, "markdown") == 0);
}

/* Check if file extension matches HTML */
static bool is_html_profile(const HighlightProfile *profile)
{
    if (!profile) return false;
    return (strcasecmp(profile->name, "html") == 0);
}

static bool profile_supports_at_annotations(const HighlightProfile *profile)
{
    if (!profile || profile->name[0] == '\0')
        return false;
    const char *name = profile->name;
    return (strcasecmp(name, "java") == 0 ||
            strcasecmp(name, "kotlin") == 0 ||
            strcasecmp(name, "scala") == 0 ||
            strcasecmp(name, "groovy") == 0);
}

/* Find closing delimiter for markdown formatting
 * Returns end position (after closing delimiter) or -1 if not found
 */
static int find_md_closing(const char *text, int len, int start, const char *delim, int delim_len)
{
    int pos = start;
    while (pos <= len - delim_len) {
        if (strncmp(text + pos, delim, delim_len) == 0) {
            /* Check it's not escaped */
            int backslash_count = 0;
            int check = pos - 1;
            while (check >= start && text[check] == '\\') {
                backslash_count++;
                check--;
            }
            if (backslash_count % 2 == 0) {
                return pos + delim_len;
            }
        }
        pos++;
    }
    return -1;
}

/* Parse hex digit value (0-15) or -1 if invalid */
static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/* Check if position is a hex color code (#RGB or #RRGGBB)
 * Returns length of color code (4 or 7) or 0 if not a color
 */
static int is_hex_color(const char *text, int len, int pos)
{
    if (pos >= len || text[pos] != '#') return 0;
    
    /* Check #RRGGBB (7 chars) */
    if (pos + 7 <= len) {
        bool valid = true;
        for (int i = 1; i <= 6; i++) {
            if (hex_digit_value(text[pos + i]) < 0) {
                valid = false;
                break;
            }
        }
        if (valid) {
            /* Make sure not followed by more hex digits OR identifier characters */
            if (pos + 7 >= len) return 7;
            
            char next = text[pos + 7];
            if (hex_digit_value(next) >= 0) return 7; /* Followed by hex digit, handled by greedy check later? No, usually breaks */
            if (isalnum((unsigned char)next) || next == '_') return 0; /* Part of a word */
            
            return 7;
        }
    }
    
    /* Check #RGB (4 chars) */
    if (pos + 4 <= len) {
        bool valid = true;
        for (int i = 1; i <= 3; i++) {
            if (hex_digit_value(text[pos + i]) < 0) {
                valid = false;
                break;
            }
        }
        if (valid) {
            /* Make sure not followed by more hex digits OR identifier characters */
            if (pos + 4 >= len) return 4;
            
            char next = text[pos + 4];
            if (hex_digit_value(next) >= 0) return 0; /* If followed by hex, it might be 6-digit or invalid 5-digit */
            if (isalnum((unsigned char)next) || next == '_') return 0; /* Part of a word */
            
            return 4;
        }
    }
    
    return 0;
}

/* Check if position is an rgb() or rgba() color code
 * Returns length of color code or 0 if not a color
 */
static int is_rgb_color(const char *text, int len, int pos)
{
    /* Check for rgb( or rgba( */
    int start_paren;
    
    if (pos + 4 <= len && strncmp(text + pos, "rgb(", 4) == 0) {
        start_paren = pos + 4;
    } else if (pos + 5 <= len && strncmp(text + pos, "rgba(", 5) == 0) {
        start_paren = pos + 5;
    } else {
        return 0;
    }
    
    /* Find closing parenthesis */
    int search = start_paren;
    int paren_depth = 1;
    while (search < len && paren_depth > 0) {
        if (text[search] == '(') paren_depth++;
        else if (text[search] == ')') paren_depth--;
        search++;
    }
    
    if (paren_depth == 0) {
        return search - pos;
    }
    return 0;
}

/* Check if position starts an hsl() or hsla() color code */
static int is_hsl_color(const char *text, int len, int pos)
{
    int start_paren;
    
    if (pos + 4 <= len && strncmp(text + pos, "hsl(", 4) == 0) {
        start_paren = pos + 4;
    } else if (pos + 5 <= len && strncmp(text + pos, "hsla(", 5) == 0) {
        start_paren = pos + 5;
    } else {
        return 0;
    }
    
    /* Find closing parenthesis */
    int search = start_paren;
    int paren_depth = 1;
    while (search < len && paren_depth > 0) {
        if (text[search] == '(') paren_depth++;
        else if (text[search] == ')') paren_depth--;
        search++;
    }
    
    if (paren_depth == 0) {
        return search - pos;
    }
    return 0;
}

/* Parse a hex color code and extract RGB values */
static bool parse_hex_color(const char *text, int pos, int len, int *r, int *g, int *b)
{
    if (len == 7) {
        /* #RRGGBB */
        *r = hex_digit_value(text[pos + 1]) * 16 + hex_digit_value(text[pos + 2]);
        *g = hex_digit_value(text[pos + 3]) * 16 + hex_digit_value(text[pos + 4]);
        *b = hex_digit_value(text[pos + 5]) * 16 + hex_digit_value(text[pos + 6]);
        return true;
    } else if (len == 4) {
        /* #RGB -> expand to #RRGGBB */
        int rv = hex_digit_value(text[pos + 1]);
        int gv = hex_digit_value(text[pos + 2]);
        int bv = hex_digit_value(text[pos + 3]);
        *r = rv * 16 + rv;
        *g = gv * 16 + gv;
        *b = bv * 16 + bv;
        return true;
    }
    return false;
}

/* Parse rgb(r, g, b) or rgba(r, g, b, a) and extract RGB values */
static bool parse_rgb_color(const char *text, int pos, int color_len, int *r, int *g, int *b)
{
    /* Find the opening parenthesis */
    int paren = pos;
    while (paren < pos + color_len && text[paren] != '(') paren++;
    if (paren >= pos + color_len) return false;
    paren++; /* skip '(' */
    
    int values[4] = {0, 0, 0, 255};
    int value_count = 0;
    int current_value = 0;
    bool in_number = false;
    bool has_percent = false;
    
    while (paren < pos + color_len && text[paren] != ')' && value_count < 4) {
        char c = text[paren];
        if (c >= '0' && c <= '9') {
            current_value = current_value * 10 + (c - '0');
            in_number = true;
        } else if (c == '%') {
            has_percent = true;
        } else if (c == ',' || c == ' ' || c == '/') {
            if (in_number) {
                if (has_percent) {
                    current_value = current_value * 255 / 100;
                    has_percent = false;
                }
                values[value_count++] = current_value;
                current_value = 0;
                in_number = false;
            }
        } else if (c == '.') {
            /* Handle decimal (for alpha), just skip the decimal part */
            paren++;
            while (paren < pos + color_len && text[paren] >= '0' && text[paren] <= '9') paren++;
            continue;
        }
        paren++;
    }
    
    /* Get last value */
    if (in_number && value_count < 4) {
        if (has_percent) {
            current_value = current_value * 255 / 100;
        }
        values[value_count++] = current_value;
    }
    
    if (value_count >= 3) {
        *r = values[0] > 255 ? 255 : values[0];
        *g = values[1] > 255 ? 255 : values[1];
        *b = values[2] > 255 ? 255 : values[2];
        return true;
    }
    return false;
}

static inline void normalize_state(HighlightState *state)
{
    if (!state)
        return;
    if (state->depth < 0 || state->depth > HL_STATE_STACK_MAX)
        state->depth = 0;
}

static inline StateID current_state(const HighlightState *state)
{
    if (!state || state->depth <= 0 || state->depth > HL_STATE_STACK_MAX)
        return HS_NORMAL;
    return state->stack[state->depth - 1].state;
}

static inline HighlightStackEntry *state_top(HighlightState *state)
{
    if (!state || state->depth <= 0 || state->depth > HL_STATE_STACK_MAX)
        return NULL;
    return &state->stack[state->depth - 1];
}

static inline void pop_state(HighlightState *state)
{
    if (state && state->depth > 0)
        state->depth--;
}

static bool push_block_comment(HighlightState *state, int idx)
{
    if (!state || idx < 0)
        return false;
    if (state->depth >= HL_STATE_STACK_MAX)
        return false;
    state->stack[state->depth].state = HS_BLOCK_COMMENT;
    state->stack[state->depth].sub_id = idx;
    state->stack[state->depth].string_delim = 0;
    state->depth++;
    return true;
}

static bool push_string_state(HighlightState *state, bool triple, char delim)
{
    if (!state)
        return false;
    if (state->depth >= HL_STATE_STACK_MAX)
        return false;
    state->stack[state->depth].state = triple ? HS_TRIPLE_STRING : HS_STRING;
    state->stack[state->depth].sub_id = 0;
    state->stack[state->depth].string_delim = delim;
    state->depth++;
    return true;
}

void highlight_line(const char *text, int len, HighlightState start, const HighlightProfile *profile, SpanVec *out, HighlightState *end)
{
    if (out) {
        out->count = 0;
        out->heap_spans = NULL;
        out->capacity = 0;
    }
    *end = start;

    // Prevent null pointer access
    if (text == NULL) return;
    if (len < 0 && text) len = strlen(text);

    // No highlight when empty line
    if(len == 0) return;
    
    /* Check for markdown or HTML for special handling */
    bool is_md = is_markdown_profile(profile);
    bool is_html = is_html_profile(profile);
    
    /* If no profile, still process color codes for all files */
    if (!profile) {
        /* Scan for color codes even without a profile */
        int pos = 0;
        while (pos < len) {
            int color_len = is_hex_color(text, len, pos);
            if (color_len == 0) color_len = is_rgb_color(text, len, pos);
            if (color_len == 0) color_len = is_hsl_color(text, len, pos);
            
            if (color_len > 0) {
                if (out) add_span(out, pos, pos + color_len, HL_NUMBER);
                pos += color_len;
            } else {
                pos++;
            }
        }
        return;
    }

    HighlightState state = start;
    normalize_state(&state);

    if (is_md && current_state(&state) == HS_MD_FENCE) {
        int first_non_ws = 0;
        while (first_non_ws < len && (text[first_non_ws] == ' ' || text[first_non_ws] == '\t'))
            first_non_ws++;

        if (first_non_ws + 3 <= len && strncmp(text + first_non_ws, "```", 3) == 0) {
            if (out)
                add_span(out, first_non_ws, len, HL_PREPROC);
            pop_state(&state);
            *end = state;
            return;
        }

        HighlightStackEntry *frame = state_top(&state);
        if (frame && frame->sub_id >= 0 && frame->sub_id < profile_count &&
            strcasecmp(profiles[frame->sub_id].name, "markdown") != 0) {
            HighlightState inner_end = {0};
            highlight_line(text, len, (HighlightState){0}, &profiles[frame->sub_id], out, &inner_end);
            *end = state;
            return;
        }

        *end = state;
        return;
    }

    int pos = 0;

    /* Enhanced Syntax Highlighting for include/import statements */
    if (current_state(&state) == HS_NORMAL) {
        int first_non_ws = 0;
        while (first_non_ws < len && isspace((unsigned char)text[first_non_ws]))
            first_non_ws++;

        if (first_non_ws < len) {
            char first_word[MAX_TOKEN_LEN];
            int fw_idx = 0;
            int p = first_non_ws;
            /* Support both 'import' and '#import' or '#include' */
            while (p < len && (isalnum((unsigned char)text[p]) || text[p] == '_' || (p == first_non_ws && text[p] == '#')) && fw_idx < MAX_TOKEN_LEN - 1) {
                first_word[fw_idx++] = text[p++];
            }
            first_word[fw_idx] = '\0';

            bool is_include_style = (first_word[0] == '#' && (strcasecmp(first_word + 1, "import") == 0 || strcasecmp(first_word + 1, "include") == 0));
            bool is_import = (strcasecmp(first_word, "import") == 0 || strcasecmp(first_word, "include") == 0 || is_include_style);

            if (is_import) {
                if (out) {
                    /* Indentation */
                    if (first_non_ws > 0)
                        add_span(out, 0, first_non_ws, HL_NORMAL);
                    /* Keyword */
                    add_span(out, first_non_ws, p, is_include_style ? HL_PREPROC_INCLUDE : HL_KEYWORD);
                    
                    /* Scan the rest of the line for comments */
                    int comment_start = -1;
                    
                    for (int i = 0; i < profile->line_comment_count; i++) {
                        const char *tok = profile->line_comments[i];
                        int tok_len = (int)strlen(tok);
                        if (tok_len == 0) continue;
                        
                        for (int s = p; s <= len - tok_len; s++) {
                            if (strncmp(text + s, tok, (size_t)tok_len) == 0) {
                                if (comment_start == -1 || s < comment_start)
                                    comment_start = s;
                            }
                        }
                    }
                    
                    if (comment_start != -1) {
                        if (comment_start > p)
                            add_span(out, p, comment_start, is_include_style ? HL_PREPROC_INCLUDE : HL_PREPROC);
                        add_span(out, comment_start, len, HL_COMMENT);
                    } else {
                        add_span(out, p, len, is_include_style ? HL_PREPROC_INCLUDE : HL_PREPROC);
                    }
                }
                pos = len;
            }
        }
    }

    while (pos < len) {
        unsigned char c = (unsigned char)text[pos];

        StateID active = current_state(&state);

        if (active == HS_NORMAL) {
            /* 0. Control characters */
            if (is_control(c)) {
                if (out) add_span(out, pos, pos + 1, HL_CONTROL);
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
                    if (out) add_span(out, pos, len, HL_CONTROL);
                    pos = len;
                    continue;
                }
            }
            
            /* 0c. Color codes (for all file types) */
            int color_len = is_hex_color(text, len, pos);
            if (color_len == 0) color_len = is_rgb_color(text, len, pos);
            if (color_len == 0) color_len = is_hsl_color(text, len, pos);
            if (color_len > 0) {
                if (out) add_span(out, pos, pos + color_len, HL_NUMBER);
                pos += color_len;
                continue;
            }
            
            /* 0c-2. Markdown Headers */
            if (is_md && (pos == 0 || (pos < 3 && isspace((unsigned char)text[0])))) {
                int first_non_ws = 0;
                while (first_non_ws < len && (text[first_non_ws] == ' ' || text[first_non_ws] == '\t'))
                    first_non_ws++;
                if (first_non_ws + 3 <= len && strncmp(text + first_non_ws, "```", 3) == 0) {
                    int fence_end = first_non_ws + 3;
                    if (out)
                        add_span(out, first_non_ws, fence_end, HL_PREPROC);

                    int p = fence_end;
                    while (p < len && isspace((unsigned char)text[p]))
                        p++;
                    int lang_start = p;
                    while (p < len && !isspace((unsigned char)text[p]) && text[p] != '{')
                        p++;
                    int lang_end = p;
                    int mapped_profile = -1;

                    if (lang_end > lang_start) {
                        char standard[MAX_TOKEN_LEN];
                        int lang_len = lang_end - lang_start;
                        if (lang_len >= MAX_TOKEN_LEN)
                            lang_len = MAX_TOKEN_LEN - 1;
                        memcpy(standard, text + lang_start, lang_len);
                        standard[lang_len] = 0;

                        mapped_profile = resolve_markdown_fence_profile_index(standard);
                        if (out) {
                            add_span(out, lang_start, lang_end,
                                     (mapped_profile >= 0) ? HL_TYPE : HL_CONTROL);
                        }
                    }

                    if (state.depth < HL_STATE_STACK_MAX) {
                        state.stack[state.depth].state = HS_MD_FENCE;
                        state.stack[state.depth].sub_id = mapped_profile;
                        state.stack[state.depth].string_delim = 0;
                        state.depth++;
                    }

                    pos = len;
                    continue;
                }

                /* Check for #, ##, ###... followed by space */
                int h = pos;
                while (h < len && text[h] == '#') h++;
                if (h > pos && h <= pos + 6 && h < len && isspace((unsigned char)text[h])) {
                    /* Only highlight the '#' markers, not the text after them */
                    if (out) add_span(out, pos, h, HL_HEADER);
                    pos = len; // Treat rest of line as default text
                    continue;
                }
            }

            /* 0d. Markdown formatting (bold, italic) */
            if (is_md) {
                /* Check for bold (**text** or __text__) */
                if ((pos + 4 <= len) && 
                    ((text[pos] == '*' && text[pos+1] == '*') ||
                     (text[pos] == '_' && text[pos+1] == '_'))) {
                    char delim[3] = {text[pos], text[pos], 0};
                    int end_pos = find_md_closing(text, len, pos + 2, delim, 2);
                    if (end_pos > 0) {
                        if (out) add_span(out, pos, end_pos, HL_MD_BOLD);
                        pos = end_pos;
                        continue;
                    }
                }
                
                /* Check for italic (*text* or _text_) - must not be bold */
                if ((pos + 2 <= len) && (text[pos] == '*' || text[pos] == '_')) {
                    /* Make sure it's not double (bold) */
                    if (pos + 1 >= len || text[pos+1] != text[pos]) {
                        char delim[2] = {text[pos], 0};
                        int end_pos = find_md_closing(text, len, pos + 1, delim, 1);
                        if (end_pos > 0) {
                            if (out) add_span(out, pos, end_pos, HL_MD_ITALIC);
                            pos = end_pos;
                            continue;
                        }
                    }
                }
            }
            
            /* 0e. HTML formatting (<u>, <b>, <i>) for HTML and Markdown */
            if (is_html || is_md) {
                /* Underline */
                if (pos + 3 <= len && strncasecmp(text + pos, "<u>", 3) == 0) {
                    int search = pos + 3;
                    while (search + 4 <= len) {
                        if (strncasecmp(text + search, "</u>", 4) == 0) {
                            if (out) add_span(out, pos, search + 4, HL_MD_UNDERLINE);
                            pos = search + 4;
                            break;
                        }
                        search++;
                    }
                    if (pos == search + 4) continue;
                }
                
                /* Bold */
                if (pos + 3 <= len && strncasecmp(text + pos, "<b>", 3) == 0) {
                    int search = pos + 3;
                    while (search + 4 <= len) {
                        if (strncasecmp(text + search, "</b>", 4) == 0) {
                            if (out) add_span(out, pos, search + 4, HL_MD_BOLD);
                            pos = search + 4;
                            break;
                        }
                        search++;
                    }
                    if (pos == search + 4) continue;
                }
                
                /* Italic */
                if (pos + 3 <= len && strncasecmp(text + pos, "<i>", 3) == 0) {
                    int search = pos + 3;
                    while (search + 4 <= len) {
                        if (strncasecmp(text + search, "</i>", 4) == 0) {
                            if (out) add_span(out, pos, search + 4, HL_MD_ITALIC);
                            pos = search + 4;
                            break;
                        }
                        search++;
                    }
                    if (pos == search + 4) continue;
                }
            }

            /* 1. Check Block Comments */
            bool matched_block = false;
            for (int i = 0; i < profile->block_comment_count; i++) {
                const char *start_tok = profile->block_comments[i].start;
                int start_len = strlen(start_tok);
                if (start_len && starts_with(text + pos, start_tok)) {
                    int start_pos = pos;
                    if (out) add_span(out, start_pos, start_pos + start_len, HL_COMMENT);
                    pos += start_len;
                    if (!push_block_comment(&state, i)) {
                        /* Stack overflow fallback: highlight rest as comment */
                        if (out) add_span(out, pos, len, HL_COMMENT);
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
                    if (out) add_span(out, pos, len, HL_COMMENT);
                    pos = len;
                    matched_line = true;
                    break;
                }
            }
            if (matched_line) continue;

            /* 3. Preprocessor (C-style #, ASM-style %) */
            if ((c == '#' || c == '%') && (pos == 0 || isspace((unsigned char)text[pos-1]))) {
                int search = pos + 1;
                while (search < len && (isalnum((unsigned char)text[search]) || text[search] == '_' || text[search] == '%')) {
                    search++;
                }
                
                HighlightStyleID preproc_style = HL_PREPROC;
                if (search > pos + 1) {
                    char word[MAX_TOKEN_LEN];
                    int word_len = search - (pos + 1);
                    if (word_len < MAX_TOKEN_LEN) {
                        memcpy(word, text + pos + 1, word_len);
                        word[word_len] = 0;
                        
                        /* Check for specific preprocessor types (with prefix if present in word) */
                        bool found_spec = false;
                        for (int i = 0; i < profile->preproc_include_keyword_count; i++) {
                            if (strcmp(word, profile->preproc_include_keywords[i]) == 0) {
                                preproc_style = HL_PREPROC_INCLUDE; found_spec = true; break;
                            }
                        }
                        if (!found_spec) {
                            for (int i = 0; i < profile->preproc_define_keyword_count; i++) {
                                if (strcmp(word, profile->preproc_define_keywords[i]) == 0) {
                                    preproc_style = HL_PREPROC_DEFINE; found_spec = true; break;
                                }
                            }
                        }
                        if (!found_spec) {
                            for (int i = 0; i < profile->preproc_flow_keyword_count; i++) {
                                if (strcmp(word, profile->preproc_flow_keywords[i]) == 0) {
                                    preproc_style = HL_PREPROC_FLOW; found_spec = true; break;
                                }
                            }
                        }
                        
                        /* If not found, try matching with prefix included */
                        if (!found_spec) {
                            char word_with_prefix[MAX_TOKEN_LEN + 1];
                            word_with_prefix[0] = (char)c;
                            memcpy(word_with_prefix + 1, word, (size_t)word_len);
                            word_with_prefix[word_len + 1] = 0;
                            
                            for (int i = 0; i < profile->preproc_include_keyword_count; i++) {
                                if (strcmp(word_with_prefix, profile->preproc_include_keywords[i]) == 0) {
                                    preproc_style = HL_PREPROC_INCLUDE; found_spec = true; break;
                                }
                            }
                            /* ... (other categories) ... */
                            if (!found_spec) {
                                for (int i = 0; i < profile->preproc_define_keyword_count; i++) {
                                    if (strcmp(word_with_prefix, profile->preproc_define_keywords[i]) == 0) {
                                        preproc_style = HL_PREPROC_DEFINE; found_spec = true; break;
                                    }
                                }
                            }
                            if (!found_spec) {
                                for (int i = 0; i < profile->preproc_flow_keyword_count; i++) {
                                    if (strcmp(word_with_prefix, profile->preproc_flow_keywords[i]) == 0) {
                                        preproc_style = HL_PREPROC_FLOW; found_spec = true; break;
                                    }
                                }
                            }
                        }

                        /* Backward compatibility: check general preproc_keywords */
                        if (preproc_style == HL_PREPROC) {
                            for (int i = 0; i < profile->preproc_keyword_count; i++) {
                                if (strcmp(word, profile->preproc_keywords[i]) == 0) {
                                    preproc_style = HL_PREPROC; break;
                                }
                            }
                        }
                    }
                }
                
                if (out) add_span(out, pos, search, preproc_style);
                pos = search;
                continue;
            }

            /* 4. Check Strings */
            if (profile->enable_triple_quotes && strncmp(text + pos, "\"\"\"", 3) == 0) {
                if (push_string_state(&state, true, '"')) {
                    if (out) add_span(out, pos, pos + 3, HL_STRING);
                } else if (out) {
                    add_span(out, pos, pos + 3, HL_STRING);
                }
                pos += 3;
                continue;
            }

            char *delim_ptr = strchr(profile->string_delims, c);
            if (delim_ptr && *delim_ptr) {
                if (push_string_state(&state, false, c)) {
                    if (out) add_span(out, pos, pos + 1, HL_STRING);
                } else if (out) {
                    add_span(out, pos, pos + 1, HL_STRING);
                }
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
                
                if (out) add_span(out, pos, search, HL_NUMBER);
                pos = search;
                continue;
            }

            /* 6. Punctuation and Operators */
            if (profile->enable_bracket_highlight && (c == '?' || c == ':')) {
                if (out) add_span(out, pos, pos + 1, HL_TERNARY);
                pos++;
                continue;
            }
            if (profile_supports_at_annotations(profile) && c == '@') {
                int start = pos + 1;
                int end = start;
                while (end < len && (isalnum((unsigned char)text[end]) || text[end] == '_' || text[end] == '.'))
                    end++;
                if (end > start) {
                    if (out) add_span(out, pos, end, HL_PREPROC);
                    pos = end;
                    continue;
                }
            }
            if (profile->enable_bracket_highlight && is_punct(c)) {
                if (out) add_span(out, pos, pos + 1, HL_BRACKET);
                pos++;
                continue;
            }
            if (profile->enable_bracket_highlight && is_operator(c)) {
                if (out) add_span(out, pos, pos + 1, HL_OPERATOR);
                pos++;
                continue;
            }

            /* 7. Words (Keywords, Types, Flow, Functions) */
            int next_stop = pos;
            while (next_stop < len && (isalnum((unsigned char)text[next_stop]) || text[next_stop] == '_')) {
                next_stop++;
            }

            if (next_stop > pos) {
                if (out) {
                    char word[MAX_TOKEN_LEN];
                    int word_len = next_stop - pos;
                    HighlightStyleID style = HL_NORMAL;
                    bool found = false;
                    char prev_char = (pos > 0) ? text[pos - 1] : '\0';
                    char next_char = (next_stop < len) ? text[next_stop] : '\0';
                    bool allow_keyword_match = is_keyword_boundary_char(prev_char) &&
                                               is_keyword_boundary_char(next_char);

                    if (word_len < MAX_TOKEN_LEN && allow_keyword_match) {
                        memcpy(word, text + pos, word_len);
                        word[word_len] = 0;

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
                    }
                    
                    /* Function detection: identifier followed by '(' */
                    if (!found) {
                        int s = next_stop;
                        while (s < len && isspace((unsigned char)text[s])) s++;
                        if (s < len && text[s] == '(') {
                            style = HL_FUNCTION;
                        }
                    }
                    add_span(out, pos, next_stop, style);
                }
                pos = next_stop;
                continue;
            }

            /* Fallback for other characters */
            if (out) add_span(out, pos, pos + 1, HL_NORMAL);
            pos++;
        }
        
        else if (active == HS_BLOCK_COMMENT) {
            HighlightStackEntry *frame = state_top(&state);
            if (!frame) {
                pop_state(&state);
                continue;
            }
            const BlockCommentPair *pair = &profile->block_comments[frame->sub_id];
            const char *end_str = pair->end;
            int end_len = strlen(end_str);
            int chunk_start = pos;
            int close_pos = -1;

            if (end_len > 0) {
                for (int scan = pos; scan <= len - end_len; ++scan) {
                    if (strncmp(text + scan, end_str, end_len) == 0) {
                        close_pos = scan;
                        break;
                    }
                }
            }

            if (close_pos >= 0) {
                if (out && close_pos > chunk_start)
                    add_span(out, chunk_start, close_pos, HL_COMMENT);
                if (out)
                    add_span(out, close_pos, close_pos + end_len, HL_COMMENT);
                pos = close_pos + end_len;
                pop_state(&state);
                continue;
            }

            if (out)
                add_span(out, chunk_start, len, HL_COMMENT);
            pos = len;

        } else if (active == HS_STRING || active == HS_TRIPLE_STRING) {
            HighlightStackEntry *frame = state_top(&state);
            if (!frame) {
                pop_state(&state);
                continue;
            }
            char delim = frame->string_delim;
            bool is_triple = (active == HS_TRIPLE_STRING);
            
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
                if (out) add_span(out, pos, pos + esc_len, HL_ESCAPE);
                pos += esc_len;
                continue;
            }
            
            if (is_triple) {
                if (pos + 3 <= len &&
                    text[pos] == delim &&
                    text[pos+1] == delim &&
                    text[pos+2] == delim) {
                    if (out) add_span(out, pos, pos + 3, HL_STRING);
                    pos += 3;
                    pop_state(&state);
                    continue;
                }
            } else {
                if (text[pos] == delim) {
                    if (out) add_span(out, pos, pos + 1, HL_STRING);
                    pos++;
                    pop_state(&state);
                    continue;
                }
            }
            
            /* Just normal string character */
            if (out) add_span(out, pos, pos + 1, HL_STRING);
            pos++;
        }
    }
    *end = state;
}

/* Scan a line for color codes and return count of colors found */
int highlight_find_colors(const char *text, int len, ColorInfo *colors, int max_colors)
{
    if (!text || len <= 0 || !colors || max_colors <= 0)
        return 0;
    
    int count = 0;
    int pos = 0;
    
    while (pos < len && count < max_colors) {
        int color_len = 0;
        int r = 0, g = 0, b = 0;
        
        /* Check for hex color (#RGB or #RRGGBB) */
        color_len = is_hex_color(text, len, pos);
        if (color_len > 0) {
            if (parse_hex_color(text, pos, color_len, &r, &g, &b)) {
                colors[count].start = pos;
                colors[count].end = pos + color_len;
                colors[count].r = r;
                colors[count].g = g;
                colors[count].b = b;
                count++;
            }
            pos += color_len;
            continue;
        }
        
        /* Check for rgb() or rgba() */
        color_len = is_rgb_color(text, len, pos);
        if (color_len > 0) {
            if (parse_rgb_color(text, pos, color_len, &r, &g, &b)) {
                colors[count].start = pos;
                colors[count].end = pos + color_len;
                colors[count].r = r;
                colors[count].g = g;
                colors[count].b = b;
                count++;
            }
            pos += color_len;
            continue;
        }
        
        /* Check for hsl() or hsla() - convert to RGB */
        color_len = is_hsl_color(text, len, pos);
        if (color_len > 0) {
            /* For simplicity, we'll just show it as a color indicator without full HSL parsing */
            /* Mark it but leave r,g,b as 0 which will show as black preview */
            colors[count].start = pos;
            colors[count].end = pos + color_len;
            colors[count].r = 128;  /* Show as gray for unimplemented HSL */
            colors[count].g = 128;
            colors[count].b = 128;
            count++;
            pos += color_len;
            continue;
        }
        
        pos++;
    }
    
    return count;
}
