#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <strings.h>
#ifdef USE_WINDOWS
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "highlight.h"
#include "completion.h"
#include "util.h"
#include "utf8.h"
#include "scraper.h"
#include "nanox.h"

completion_state_t completion_state;
typedef struct {
    int active;
    int focused;
    int tab_primed;
    size_t prefix_len;
    int popup_row;
    int popup_col;
    int popup_width;
    int popup_height;
} completion_dropdown_state_t;

static completion_dropdown_state_t completion_dropdown_state = { 0, 0, 0, 0, 0, 0, 0, 0 };

static char completion_storage[MAX_COMPLETIONS][MAX_COMPLETION_LEN];
static int completion_scores[MAX_COMPLETIONS];
static completion_context_t completion_active_ctx = COMPLETION_CONTEXT_DEFAULT;
static char completion_active_prefix[MAX_COMPLETION_LEN];

static void completion_ensure_visible(void);
static void completion_dropdown_activate(size_t prefix_len);
static void completion_dropdown_deactivate(int commit_preview);
static void completion_dropdown_apply_selection(void);
static void completion_dropdown_refresh_geometry(void);
static void completion_draw_popup_box(void);
static void completion_draw_minibuffer_list(int row, int col);
static void completion_preview_apply_selected(void);

typedef struct {
    char **items;
    int count;
    int capacity;
    int max_items;
} completion_pool_t;

typedef struct {
    char *class_name;
    completion_pool_t members;
    int loaded;
} java_member_entry_t;

#define MAX_C_SYMBOLS 4096
#define MAX_JAVA_SYMBOLS 4096
#define MAX_JAVA_MEMBERS 512
#define MAX_C_SCAN_FILES 2048
#define MAX_C_SCAN_DEPTH 3
#define MAX_C_FILE_BYTES 32768
#define MAX_JAVA_SCAN_FILES 4096
#define MAX_JAVA_SCAN_DEPTH 6
#define MAX_SOURCE_SYMBOLS 2048
#define MAX_SOURCE_FILE_BYTES 262144

static completion_pool_t c_symbol_cache = { NULL, 0, 0, MAX_C_SYMBOLS };
static completion_pool_t c_include_paths = { NULL, 0, 0, 0 };
static int c_symbols_loaded = 0;
static int c_files_scanned = 0;

static completion_pool_t java_class_cache = { NULL, 0, 0, MAX_JAVA_SYMBOLS };
static completion_pool_t java_classpath_entries = { NULL, 0, 0, 0 };
static int java_classpath_loaded = 0;
static int java_symbols_loaded = 0;
static int java_symbols_loading = 0;
static int java_files_scanned = 0;
static char java_classpath_string[4096];
#ifndef USE_WINDOWS
static pthread_mutex_t java_async_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

typedef struct {
    int active;
    struct line *line;
    int start_offset;
    int prefix_len;
    int last_tail_len;
} completion_preview_state_t;

static completion_preview_state_t completion_preview_state = { 0 };

static java_member_entry_t *java_member_cache = NULL;
static int java_member_cache_count = 0;
static int java_member_cache_capacity = 0;

#define COMPLETION_MINIBUFFER_MAX_VISIBLE 5
#define COMPLETION_POPUP_MAX_VISIBLE 8
#define COMPLETION_POPUP_MIN_CONTENT 28
#define COMPLETION_POPUP_FIXED_CONTENT 40
#define COMPLETION_POPUP_MAX_CONTENT 64

static HighlightStyle completion_resolve_style(HighlightStyle style, HighlightStyle fallback)
{
    if (style.fg == -1)
        style.fg = fallback.fg;
    if (style.bg == -1)
        style.bg = fallback.bg;
    return style;
}

static void completion_apply_style(const HighlightStyle *style)
{
    if (!style)
        return;
    TTsetcolors(style->fg, style->bg);
    TTsetattrs(style->bold, style->underline, style->italic);
}

static int completion_display_width(const char *text)
{
    if (text == NULL)
        return 0;
    return utf8_display_width(text, (int)strlen(text));
}

/* Source-level symbol cache: structs, typedefs, enums, functions from
 * the current buffer and file-reserve slot files. */
static completion_pool_t source_symbol_cache = { NULL, 0, 0, MAX_SOURCE_SYMBOLS };
static char source_symbol_last_fname[NFILEN]; /* fname of last scanned buffer */

static const char *common_keywords[] = {
    "if", "else", "while", "for", "do", "return", "break", "continue", "switch", "case", "default",
    "int", "char", "float", "double", "void", "struct", "union", "enum", "typedef", "static", "extern",
    "include", "define", "ifdef", "ifndef", "endif", "import", "from", "as", "def", "class", "try", "except",
    "finally", "with", "yield", "lambda", "assert", "pass", "None", "True", "False", "and", "or", "not", "is", "in",
    "let", "const", "var", "function", "async", "await", "promise", "then", "catch", "export",
    "fn", "mut", "match", "use", "mod", "pub", "impl", "trait", "type", "where", "crate", "self", "super",
    "sizeof", "alignas", "alignof", "bool", "static_assert", "thread_local", "template", "typename", "mutable", "virtual", "override"
};

static int file_exists(const char *path);
static void add_path_entry(completion_pool_t *paths, const char *entry);
static void parse_path_list(const char *value, completion_pool_t *paths);
static int has_extension(const char *name, const char *const *exts, size_t count);
static int prev_char_start(struct line *lp, int pos);
#ifndef USE_WINDOWS
static void *java_class_symbols_loader(void *arg);
static void start_async_java_class_symbols_load(void);
#endif

static void completion_consider_candidate(const char *candidate, const char *prefix);
static int completion_fuzzy_score(const char *candidate, const char *query);
static int completion_should_use_lsp(void);
static void completion_preview_reset(void)
{
    completion_preview_state.active = 0;
    completion_preview_state.line = NULL;
    completion_preview_state.start_offset = 0;
    completion_preview_state.prefix_len = 0;
    completion_preview_state.last_tail_len = 0;
}

static void completion_preview_begin(struct line *line, int start, int prefix_len)
{
    completion_preview_state.active = 1;
    completion_preview_state.line = line;
    completion_preview_state.start_offset = start;
    completion_preview_state.prefix_len = prefix_len;
    completion_preview_state.last_tail_len = 0;
}

static void completion_preview_delete_tail(void)
{
    if (!completion_preview_state.active || completion_preview_state.last_tail_len <= 0 || !curwp)
        return;

    curwp->w_dotp = completion_preview_state.line;
    curwp->w_doto = completion_preview_state.start_offset + completion_preview_state.prefix_len;
    ldelete((long)completion_preview_state.last_tail_len, FALSE);
    completion_preview_state.line = curwp->w_dotp;
    curwp->w_flag |= WFMOVE;
    completion_preview_state.last_tail_len = 0;
}

static void completion_preview_abort(void)
{
    completion_preview_delete_tail();
    completion_preview_reset();
}

static void completion_preview_commit(void)
{
    completion_preview_state.active = 0;
    completion_preview_state.last_tail_len = 0;
}

static void completion_preview_apply_match(const char *match)
{
    if (!completion_preview_state.active || !match || !curwp)
        return;

    size_t match_len = strlen(match);
    if (match_len < (size_t)completion_preview_state.prefix_len)
        return;
    if (strncmp(match, completion_active_prefix, (size_t)completion_preview_state.prefix_len) != 0)
        return;

    const char *tail = match + completion_preview_state.prefix_len;
    int tail_len = (int)strlen(tail);

    completion_preview_delete_tail();

    curwp->w_dotp = completion_preview_state.line;
    curwp->w_doto = completion_preview_state.start_offset + completion_preview_state.prefix_len;

    if (tail_len > 0) {
        linsert_block((char *)tail, tail_len);
        curwp->w_doto = completion_preview_state.start_offset + completion_preview_state.prefix_len + tail_len;
    }

    completion_preview_state.line = curwp->w_dotp;
    completion_preview_state.last_tail_len = tail_len;
    curwp->w_flag |= WFMOVE;
}

static void completion_preview_apply_selected(void)
{
    if (!completion_preview_state.active)
        return;
    const char *match = completion_get_selected();
    if (!match)
        return;
    completion_preview_apply_match(match);
}

static void completion_write_utf8_clipped(const char *text, int max_width)
{
    int used = 0;
    int len = (int)strlen(text ? text : "");
    int idx = 0;
    while (idx < len && used < max_width) {
        unicode_t uc;
        int bytes = utf8_to_unicode((unsigned char *)text, idx, len, &uc);
        if (bytes <= 0)
            break;
        int char_width = mystrnlen_raw_w(uc);
        if (used + char_width > max_width)
            break;
        TTputc(uc);
        used += char_width;
        idx += bytes;
    }
    while (used < max_width) {
        TTputc(' ');
        used++;
    }
}

static int completion_write_utf8_ellipsized(const char *text, int max_width)
{
    int used = 0;
    int len = (int)strlen(text ? text : "");
    int idx = 0;
    int truncated = 0;
    int body_width = max_width;

    if (max_width <= 0)
        return 0;

    if (completion_display_width(text) > max_width && max_width >= 3)
        body_width = max_width - 3;

    while (idx < len && used < body_width) {
        unicode_t uc;
        int bytes = utf8_to_unicode((unsigned char *)text, idx, len, &uc);
        if (bytes <= 0)
            break;
        int char_width = mystrnlen_raw_w(uc);
        if (used + char_width > body_width)
            break;
        TTputc(uc);
        used += char_width;
        idx += bytes;
    }

    if (idx < len && max_width >= 3) {
        TTputc('.');
        TTputc('.');
        TTputc('.');
        used += 3;
        truncated = 1;
    }

    while (used < max_width) {
        TTputc(' ');
        used++;
    }

    return truncated;
}

static void pool_add(completion_pool_t *pool, const char *value)
{
    if (pool == NULL || value == NULL || *value == '\0')
        return;
    if (pool->max_items > 0 && pool->count >= pool->max_items)
        return;
    for (int i = 0; i < pool->count; i++) {
        if (pool->items[i] != NULL && strcmp(pool->items[i], value) == 0)
            return;
    }
    if (pool->count == pool->capacity) {
        int new_capacity = pool->capacity ? pool->capacity * 2 : 64;
        char **tmp = realloc(pool->items, (size_t)new_capacity * sizeof(char *));
        if (!tmp)
            return;
        pool->items = tmp;
        pool->capacity = new_capacity;
    }
    char *copy = strdup(value);
    if (!copy)
        return;
    pool->items[pool->count++] = copy;
}

static void add_matches_from_pool(const completion_pool_t *pool, const char *prefix)
{
    if (pool == NULL || prefix == NULL)
        return;
    for (int i = 0; i < pool->count; i++) {
        if (pool->items[i] == NULL)
            continue;
        completion_consider_candidate(pool->items[i], prefix);
        if (completion_state.count >= MAX_COMPLETIONS)
            break;
    }
}

static void completion_reset_state(void)
{
    completion_state.count = 0;
    completion_state.selected_index = 0;
    completion_state.is_visible = 0;
    completion_state.scroll_offset = 0;
    memset(completion_scores, 0, sizeof(completion_scores));
}

static int completion_word_exists(const char *word)
{
    for (int i = 0; i < completion_state.count; i++) {
        if (completion_state.matches[i] != NULL && strcmp(completion_state.matches[i], word) == 0)
            return TRUE;
    }
    return FALSE;
}

static void completion_add_match_with_score(const char *word, int score)
{
    if (word == NULL || *word == '\0')
        return;
    if (completion_state.count >= MAX_COMPLETIONS)
        return;
    if (completion_word_exists(word))
        return;
    int insert = completion_state.count;
    while (insert > 0) {
        int prev = insert - 1;
        const char *prev_word = completion_state.matches[prev];
        if (completion_scores[prev] > score)
            break;
        if (completion_scores[prev] == score && prev_word && strcmp(prev_word, word) <= 0)
            break;
        if (insert < MAX_COMPLETIONS) {
            mystrscpy(completion_storage[insert], completion_storage[prev], MAX_COMPLETION_LEN);
            completion_state.matches[insert] = completion_storage[insert];
            completion_scores[insert] = completion_scores[prev];
        }
        insert--;
    }
    mystrscpy(completion_storage[insert], word, MAX_COMPLETION_LEN);
    completion_state.matches[insert] = completion_storage[insert];
    completion_scores[insert] = score;
    completion_state.count++;
}

static int is_identifier_char(unicode_t uc)
{
    if (uc == '_')
        return TRUE;
    if (uc >= 0x80)
        return TRUE;
    if (iswalnum((wint_t)uc))
        return TRUE;
    return FALSE;
}

static int is_path_char(unicode_t uc)
{
    if (uc >= 0x80)
        return TRUE;
    if (iswalnum((wint_t)uc))
        return TRUE;
    switch (uc) {
    case '_':
    case '-':
    case '.':
    case '/':
    case '~':
    case '+':
    case ':':
        return TRUE;
    default:
        return FALSE;
    }
}

static int completion_fuzzy_score(const char *candidate, const char *query)
{
    if (!candidate || !query || !*query)
        return -1;

    size_t c_len = strlen(candidate);
    size_t q_len = strlen(query);
    if (q_len > c_len)
        return -1;

    if (completion_active_ctx == COMPLETION_CONTEXT_PATH) {
        if (strncmp(candidate, query, q_len) != 0)
            return -1;
        return 10000 - (int)(c_len - q_len);
    }

    int score = 0;
    size_t qi = 0;
    int consecutive = 0;
    for (size_t i = 0; i < c_len && qi < q_len; i++) {
        unsigned char cc = (unsigned char)candidate[i];
        unsigned char qc = (unsigned char)query[qi];
        if (tolower(cc) == tolower(qc)) {
            score += 10;
            if (cc == qc)
                score += 4;
            if (i == 0)
                score += 8;
            if (consecutive > 0)
                score += 6;
            consecutive++;
            qi++;
        } else {
            consecutive = 0;
        }
    }
    if (qi != q_len)
        return -1;
    if (strncasecmp(candidate, query, q_len) == 0)
        score += 50;
    score -= (int)(c_len - q_len);
    return score;
}

static void completion_consider_candidate(const char *candidate, const char *prefix)
{
    if (candidate == NULL || prefix == NULL)
        return;
    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0)
        return;
    if (strcmp(candidate, prefix) == 0)
        return;
    int score = completion_fuzzy_score(candidate, prefix);
    if (score < 0)
        return;
    completion_add_match_with_score(candidate, score);
}

static void collect_keyword_array(const char entries[][MAX_TOKEN_LEN], int count, const char *prefix)
{
    for (int i = 0; i < count && completion_state.count < MAX_COMPLETIONS; i++) {
        completion_consider_candidate(entries[i], prefix);
        if (completion_state.count >= MAX_COMPLETIONS)
            break;
    }
}

static void collect_language_keywords(const char *prefix)
{
    if (curbp == NULL)
        return;

    const HighlightProfile *profile = highlight_get_profile(curbp->b_fname);
    if (profile == NULL)
        return;

    collect_keyword_array(profile->keywords, profile->keyword_count, prefix);
    collect_keyword_array(profile->type_keywords, profile->type_keyword_count, prefix);
    collect_keyword_array(profile->flow_keywords, profile->flow_keyword_count, prefix);
    collect_keyword_array(profile->preproc_keywords, profile->preproc_keyword_count, prefix);
    collect_keyword_array(profile->return_keywords, profile->return_keyword_count, prefix);
}

static void collect_common_keywords(const char *prefix)
{
    for (size_t i = 0; i < sizeof(common_keywords) / sizeof(common_keywords[0]); i++) {
        const char *word = common_keywords[i];
        completion_consider_candidate(word, prefix);
    }
}

static void collect_buffer_words(const char *prefix)
{
    if (curbp == NULL)
        return;

    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0)
        return;

    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep && completion_state.count < MAX_COMPLETIONS) {
        int len = llength(lp);
        int i = 0;
        while (i < len && completion_state.count < MAX_COMPLETIONS) {
            unicode_t uc = 0;
            int bytes = utf8_to_unicode(lp->l_text, (unsigned)i, (unsigned)len, &uc);
            if (bytes <= 0)
                bytes = 1;

            if (is_identifier_char(uc)) {
                int start = i;
                i += bytes;
                while (i < len) {
                    unicode_t next = 0;
                    int consumed = utf8_to_unicode(lp->l_text, (unsigned)i, (unsigned)len, &next);
                    if (consumed <= 0)
                        consumed = 1;
                    if (!is_identifier_char(next))
                        break;
                    i += consumed;
                }
                int word_len = i - start;
                if (word_len >= MAX_COMPLETION_LEN)
                    word_len = MAX_COMPLETION_LEN - 1;

                char tmp[MAX_COMPLETION_LEN];
                memcpy(tmp, &lp->l_text[start], (size_t)word_len);
                tmp[word_len] = '\0';

                if ((size_t)word_len >= prefix_len)
                    completion_consider_candidate(tmp, prefix);
            } else {
                i += bytes;
            }
        }
        lp = lforw(lp);
    }
}

static void add_env_paths(const char *env_name, completion_pool_t *paths)
{
    const char *value = getenv(env_name);
    if (value && *value)
        parse_path_list(value, paths);
}

static void ensure_c_include_paths(void)
{
    static const char *defaults[] = {
        "/usr/include",
        "/usr/local/include",
        "/opt/homebrew/include",
        "/opt/local/include"
    };

    if (c_include_paths.count > 0)
        return;

    add_env_paths("C_INCLUDE_PATH", &c_include_paths);
    add_env_paths("CPLUS_INCLUDE_PATH", &c_include_paths);
    add_env_paths("CXX_INCLUDE_PATH", &c_include_paths);
    add_env_paths("CPATH", &c_include_paths);
    add_env_paths("INCLUDE", &c_include_paths);

    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        if (file_exists(defaults[i]))
            add_path_entry(&c_include_paths, defaults[i]);
    }
}

static void add_symbol_token(completion_pool_t *pool, const char *token, size_t len)
{
    char tmp[MAX_COMPLETION_LEN];
    if (pool == NULL || token == NULL)
        return;
    if (len < 3)
        return;
    if (len >= MAX_COMPLETION_LEN)
        len = MAX_COMPLETION_LEN - 1;
    memcpy(tmp, token, len);
    tmp[len] = '\0';
    pool_add(pool, tmp);
}

static void scan_c_header_file(const char *path)
{
    FILE *fp;
    char buf[4096];
    size_t total = 0;
    size_t nread;
    char token[MAX_COMPLETION_LEN];
    size_t token_len = 0;

    if (path == NULL || c_files_scanned >= MAX_C_SCAN_FILES)
        return;

    fp = fopen(path, "r");
    if (!fp)
        return;

    c_files_scanned++;

    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < nread; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (isalnum(ch) || ch == '_') {
                if (token_len < sizeof(token) - 1)
                    token[token_len++] = (char)ch;
            } else if (token_len > 0) {
                add_symbol_token(&c_symbol_cache, token, token_len);
                token_len = 0;
                if (c_symbol_cache.max_items > 0 && c_symbol_cache.count >= c_symbol_cache.max_items)
                    break;
            }
        }
        total += nread;
        if ((c_symbol_cache.max_items > 0 && c_symbol_cache.count >= c_symbol_cache.max_items) ||
            total >= MAX_C_FILE_BYTES)
            break;
    }

    if (token_len > 0)
        add_symbol_token(&c_symbol_cache, token, token_len);

    fclose(fp);
}

static void scan_c_include_dir(const char *path, int depth)
{
#ifndef USE_WINDOWS
    struct stat st;
#endif
    DIR *dp;
    struct dirent *dent;
    char child[NFILEN];
    static const char *header_exts[] = { ".h", ".hpp", ".hh", ".hxx", ".hp", ".inc" };

    if (path == NULL || *path == '\0' || depth > MAX_C_SCAN_DEPTH)
        return;
    if (c_files_scanned >= MAX_C_SCAN_FILES ||
        (c_symbol_cache.max_items > 0 && c_symbol_cache.count >= c_symbol_cache.max_items))
        return;

    dp = opendir(path);
    if (!dp)
        return;

    while ((dent = readdir(dp)) != NULL) {
        if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, dent->d_name);
#ifdef USE_WINDOWS
        struct stat wst;
        if (stat(child, &wst) != 0)
            continue;
        if (S_ISDIR(wst.st_mode)) {
            if (depth + 1 <= MAX_C_SCAN_DEPTH)
                scan_c_include_dir(child, depth + 1);
        } else if (S_ISREG(wst.st_mode)) {
            if (has_extension(dent->d_name, header_exts, sizeof(header_exts) / sizeof(header_exts[0])))
                scan_c_header_file(child);
        }
#else
        if (stat(child, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth + 1 <= MAX_C_SCAN_DEPTH)
                scan_c_include_dir(child, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (has_extension(dent->d_name, header_exts, sizeof(header_exts) / sizeof(header_exts[0])))
                scan_c_header_file(child);
        }
#endif

        if (c_files_scanned >= MAX_C_SCAN_FILES ||
            (c_symbol_cache.max_items > 0 && c_symbol_cache.count >= c_symbol_cache.max_items))
            break;
    }

    closedir(dp);
}

static void ensure_c_symbols_loaded(void)
{
    if (c_symbols_loaded)
        return;

    ensure_c_include_paths();
    for (int i = 0; i < c_include_paths.count; i++) {
        scan_c_include_dir(c_include_paths.items[i], 0);
        if (c_symbol_cache.max_items > 0 && c_symbol_cache.count >= c_symbol_cache.max_items)
            break;
    }
    c_symbols_loaded = 1;
}

static int class_name_from_relative(const char *relative_path, char *out, size_t outsz)
{
    const char *path;
    const char *first_sep;
    size_t len;
    const char *dot;
    if (relative_path == NULL || out == NULL || outsz == 0)
        return FALSE;

    path = relative_path;
    first_sep = strpbrk(path, "/\\");
    if (first_sep != NULL && first_sep > path) {
        int module_like = FALSE;
        for (const char *p = path; p < first_sep; p++) {
            if (*p == '.') {
                module_like = TRUE;
                break;
            }
        }
        if (module_like && first_sep[1] != '\0')
            path = first_sep + 1;
    }

    dot = strrchr(path, '.');
    if (dot == NULL || dot == path)
        return FALSE;
    len = (size_t)(dot - path);
    if (len >= outsz)
        len = outsz - 1;
    for (size_t i = 0; i < len; i++) {
        char ch = path[i];
        if (ch == '/' || ch == '\\')
            ch = '.';
        out[i] = ch;
    }
    out[len] = '\0';
    if (out[0] == '\0' || strstr(out, "module-info") != NULL)
        return FALSE;
    return TRUE;
}

static void scan_java_dir(const char *root, size_t root_len, const char *path, int depth);

static void add_java_dir_entry(const char *root, size_t root_len, const char *path)
{
    const char *rel;
    char class_name[MAX_COMPLETION_LEN];
    if (root == NULL || path == NULL)
        return;
    if (strlen(path) <= root_len)
        return;
    rel = path + root_len;
    while (*rel == '/' || *rel == '\\')
        rel++;
    if (*rel == '\0')
        return;
    if (!class_name_from_relative(rel, class_name, sizeof(class_name)))
        return;
    pool_add(&java_class_cache, class_name);
}

static void scan_java_dir(const char *root, size_t root_len, const char *path, int depth)
{
    DIR *dp;
    struct dirent *dent;
    char child[NFILEN];
    struct stat st;
    static const char *class_exts[] = { ".class", ".java" };

    if (path == NULL || *path == '\0' || depth > MAX_JAVA_SCAN_DEPTH)
        return;
    if (java_files_scanned >= MAX_JAVA_SCAN_FILES ||
        (java_class_cache.max_items > 0 && java_class_cache.count >= java_class_cache.max_items))
        return;

    dp = opendir(path);
    if (!dp)
        return;

    while ((dent = readdir(dp)) != NULL) {
        if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, dent->d_name);
        if (stat(child, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth + 1 <= MAX_JAVA_SCAN_DEPTH)
                scan_java_dir(root, root_len, child, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (has_extension(dent->d_name, class_exts, sizeof(class_exts) / sizeof(class_exts[0]))) {
                java_files_scanned++;
                add_java_dir_entry(root, root_len, child);
            }
        }
        if (java_files_scanned >= MAX_JAVA_SCAN_FILES ||
            (java_class_cache.max_items > 0 && java_class_cache.count >= java_class_cache.max_items))
            break;
    }

    closedir(dp);
}

static int build_shell_quoted(const char *input, char *output, size_t outsz)
{
    size_t pos = 0;
    if (input == NULL || output == NULL || outsz < 3)
        return FALSE;
    output[pos++] = '\'';
    for (const char *p = input; *p; p++) {
        if (pos + 4 >= outsz)
            return FALSE;
        if (*p == '\'') {
            output[pos++] = '\'';
            output[pos++] = '\\';
            output[pos++] = '\'';
            output[pos++] = '\'';
        } else {
            output[pos++] = *p;
        }
    }
    if (pos + 2 > outsz)
        return FALSE;
    output[pos++] = '\'';
    output[pos] = '\0';
    return TRUE;
}

static void process_java_jar(const char *path)
{
    char quoted[NFILEN * 2];
    char cmd[NFILEN * 2 + 16];
    FILE *fp;
    char line[512];

    if (!path || !*path)
        return;
    if (!build_shell_quoted(path, quoted, sizeof(quoted)))
        return;
    snprintf(cmd, sizeof(cmd), "jar tf %s", quoted);
    fp = popen(cmd, "r");
    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0)
            continue;
        if (strstr(line, "module-info") != NULL)
            continue;
        if (class_name_from_relative(line, line, sizeof(line)))
            pool_add(&java_class_cache, line);
        if (java_class_cache.max_items > 0 && java_class_cache.count >= java_class_cache.max_items)
            break;
    }

    pclose(fp);
}

static void ensure_java_classpath_entries(void)
{
    const char *java_home;
    char buf[NFILEN];

    if (java_classpath_loaded)
        return;

    add_env_paths("CLASSPATH", &java_classpath_entries);
    add_path_entry(&java_classpath_entries, ".");
    add_path_entry(&java_classpath_entries, "./lib");
    add_path_entry(&java_classpath_entries, "./build/classes");
    add_path_entry(&java_classpath_entries, "./out/production");
    add_path_entry(&java_classpath_entries, "~/.m2/repository");

    java_home = getenv("JAVA_HOME");
    if (java_home && *java_home) {
        snprintf(buf, sizeof(buf), "%s/lib", java_home);
        if (file_exists(buf))
            add_path_entry(&java_classpath_entries, buf);
        snprintf(buf, sizeof(buf), "%s/lib/rt.jar", java_home);
        if (file_exists(buf))
            add_path_entry(&java_classpath_entries, buf);
        snprintf(buf, sizeof(buf), "%s/lib/src.zip", java_home);
        if (file_exists(buf))
            add_path_entry(&java_classpath_entries, buf);
    }

    java_classpath_loaded = 1;
}

static void ensure_java_class_symbols(void)
{
    struct stat st;

#ifdef USE_WINDOWS
    if (java_symbols_loaded)
        return;
#else
    pthread_mutex_lock(&java_async_mutex);
    if (java_symbols_loaded) {
        pthread_mutex_unlock(&java_async_mutex);
        return;
    }
    pthread_mutex_unlock(&java_async_mutex);
#endif

    ensure_java_classpath_entries();
    for (int i = 0; i < java_classpath_entries.count; i++) {
        const char *entry = java_classpath_entries.items[i];
        if (stat(entry, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            size_t root_len = strlen(entry);
            scan_java_dir(entry, root_len, entry, 0);
        } else if (S_ISREG(st.st_mode)) {
            const char *exts[] = { ".jar", ".zip" };
            if (has_extension(entry, exts, sizeof(exts) / sizeof(exts[0])))
                process_java_jar(entry);
        }
        if (java_class_cache.max_items > 0 && java_class_cache.count >= java_class_cache.max_items)
            break;
    }
#ifdef USE_WINDOWS
    java_symbols_loaded = 1;
#else
    pthread_mutex_lock(&java_async_mutex);
    java_symbols_loaded = 1;
    pthread_mutex_unlock(&java_async_mutex);
#endif
}

#ifndef USE_WINDOWS
static void *java_class_symbols_loader(void *arg)
{
    (void)arg;
    ensure_java_class_symbols();
    pthread_mutex_lock(&java_async_mutex);
    java_symbols_loading = 0;
    pthread_mutex_unlock(&java_async_mutex);
    return NULL;
}

static void start_async_java_class_symbols_load(void)
{
    pthread_t tid;
    int should_start = FALSE;

    pthread_mutex_lock(&java_async_mutex);
    if (!java_symbols_loaded && !java_symbols_loading) {
        java_symbols_loading = 1;
        should_start = TRUE;
    }
    pthread_mutex_unlock(&java_async_mutex);

    if (!should_start)
        return;

    if (pthread_create(&tid, NULL, java_class_symbols_loader, NULL) == 0) {
        (void)pthread_detach(tid);
    } else {
        pthread_mutex_lock(&java_async_mutex);
        java_symbols_loading = 0;
        pthread_mutex_unlock(&java_async_mutex);
    }
}
#endif

static const char *get_java_classpath_string(void)
{
    size_t used = 0;
    if (java_classpath_string[0] != '\0')
        return java_classpath_string;

    ensure_java_classpath_entries();
    for (int i = 0; i < java_classpath_entries.count; i++) {
        const char *entry = java_classpath_entries.items[i];
        size_t len = strlen(entry);
        if (used + len + 2 >= sizeof(java_classpath_string))
            break;
        if (used > 0)
            java_classpath_string[used++] = ':';
        memcpy(java_classpath_string + used, entry, len);
        used += len;
    }
    java_classpath_string[used] = '\0';
    if (java_classpath_string[0] == '\0')
        strcpy(java_classpath_string, ".");
    return java_classpath_string;
}

static int is_c_like_file(const char *fname)
{
    static const char *exts[] = {
        ".c", ".h", ".hpp", ".hh", ".hxx", ".cxx", ".cc", ".cpp", ".ino"
    };
    if (fname == NULL || *fname == '\0')
        return FALSE;
    return has_extension(fname, exts, sizeof(exts) / sizeof(exts[0]));
}

static int is_java_file(const char *fname)
{
    static const char *exts[] = { ".java" };
    if (fname == NULL || *fname == '\0')
        return FALSE;
    return has_extension(fname, exts, sizeof(exts) / sizeof(exts[0]));
}

static int is_python_file(const char *fname)
{
    static const char *exts[] = { ".py" };
    if (fname == NULL || *fname == '\0')
        return FALSE;
    return has_extension(fname, exts, sizeof(exts) / sizeof(exts[0]));
}

static int is_node_file(const char *fname)
{
    static const char *exts[] = { ".js", ".jsx", ".ts", ".tsx", ".mjs", ".cjs" };
    if (fname == NULL || *fname == '\0')
        return FALSE;
    return has_extension(fname, exts, sizeof(exts) / sizeof(exts[0]));
}

static void add_language_specific_matches(const char *prefix, completion_context_t ctx)
{
    if (ctx == COMPLETION_CONTEXT_PATH)
        return;
    if (curbp == NULL || prefix == NULL || *prefix == '\0')
        return;
    if (is_c_like_file(curbp->b_fname)) {
        ensure_c_symbols_loaded();
        add_matches_from_pool(&c_symbol_cache, prefix);
    } else if (is_java_file(curbp->b_fname)) {
#ifdef USE_WINDOWS
        ensure_java_class_symbols();
#else
        int ready = FALSE;
        pthread_mutex_lock(&java_async_mutex);
        ready = java_symbols_loaded;
        pthread_mutex_unlock(&java_async_mutex);
        if (ready) {
            add_matches_from_pool(&java_class_cache, prefix);
        } else {
            start_async_java_class_symbols_load();
        }
#endif
    }
    if (completion_state.count > 0)
        completion_state.is_visible = 1;
}

/* -----------------------------------------------------------------------
 * Source-level symbol extraction:
 * Reads structs, typedefs, enums, unions, and function names from C/C++
 * source text, Python text, and JS/TS text.  Results are stored in the
 * source_symbol_cache pool and offered as tab completions.
 * ----------------------------------------------------------------------- */

/* Keywords that are NOT symbol names even when they appear before '(' */
static const char *c_control_keywords[] = {
    "if", "else", "while", "for", "do", "switch", "return", "sizeof",
    "alignof", "typeof", "static_assert", NULL
};

static int is_c_control_kw(const char *tok)
{
    for (int i = 0; c_control_keywords[i]; i++) {
        if (strcmp(tok, c_control_keywords[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

/* Extract struct/union/enum names, typedef names, and function names from
 * a block of C/C++ source text. */
static void extract_c_symbols_from_text(const char *text, size_t text_len,
                                        completion_pool_t *pool)
{
    enum { ST_NORMAL, ST_LINE_CMT, ST_BLOCK_CMT, ST_STRING, ST_CHAR } state = ST_NORMAL;

    /* Sliding window of last two complete tokens */
    char tok[MAX_COMPLETION_LEN];
    size_t tok_len = 0;
    char prev1[MAX_COMPLETION_LEN];   /* token before current */
    prev1[0] = '\0';

    /* typedef tracking */
    int in_typedef = 0;
    char last_ident[MAX_COMPLETION_LEN];
    last_ident[0] = '\0';

    int brace_depth = 0;

    for (size_t i = 0; i < text_len; i++) {
        unsigned char c = (unsigned char)text[i];
        unsigned char next = (i + 1 < text_len) ? (unsigned char)text[i + 1] : 0;

        switch (state) {
        case ST_NORMAL:
            if (c == '/' && next == '/') {
                /* Flush pending token */
                state = ST_LINE_CMT;
                i++;
                goto flush_token;
            } else if (c == '/' && next == '*') {
                state = ST_BLOCK_CMT;
                i++;
                goto flush_token;
            } else if (c == '"') {
                state = ST_STRING;
                goto flush_token;
            } else if (c == '\'') {
                state = ST_CHAR;
                goto flush_token;
            } else if (isalnum(c) || c == '_') {
                if (tok_len < sizeof(tok) - 1)
                    tok[tok_len++] = (char)c;
            } else {
                /* Non-identifier character: flush token if any */
flush_token:
                if (tok_len > 0) {
                    tok[tok_len] = '\0';
                    /* Pattern: struct/union/enum <NAME> */
                    if (strcmp(prev1, "struct") == 0 ||
                        strcmp(prev1, "union")  == 0 ||
                        strcmp(prev1, "enum")   == 0) {
                        /* tok is the tag name (might be followed by { or ;) */
                        pool_add(pool, tok);
                    }
                    /* Pattern: typedef → track last identifier at top level only */
                    if (in_typedef && brace_depth == 0) {
                        mystrscpy(last_ident, tok, sizeof(last_ident));
                    }
                    if (strcmp(tok, "typedef") == 0)
                        in_typedef = 1;
                    /* Advance sliding window */
                    mystrscpy(prev1, tok, sizeof(prev1));
                    tok_len = 0;
                }
                /* Pattern: IDENT '(' at top-level → function name */
                if ((char)c == '(' && brace_depth == 0 && prev1[0] != '\0') {
                    if (!is_c_control_kw(prev1) &&
                        strcmp(prev1, "struct") != 0 &&
                        strcmp(prev1, "union")  != 0 &&
                        strcmp(prev1, "enum")   != 0 &&
                        strcmp(prev1, "typedef") != 0) {
                        pool_add(pool, prev1);
                    }
                }
                /* End of typedef on ';' at top level only */
                if ((char)c == ';' && in_typedef && brace_depth == 0) {
                    if (last_ident[0])
                        pool_add(pool, last_ident);
                    in_typedef = 0;
                    last_ident[0] = '\0';
                }
                if ((char)c == '{') brace_depth++;
                else if ((char)c == '}' && brace_depth > 0) brace_depth--;
            }
            break;
        case ST_LINE_CMT:
            if (c == '\n') state = ST_NORMAL;
            break;
        case ST_BLOCK_CMT:
            if (c == '*' && next == '/') { state = ST_NORMAL; i++; }
            break;
        case ST_STRING:
            if (c == '\\') i++;
            else if (c == '"') state = ST_NORMAL;
            break;
        case ST_CHAR:
            if (c == '\\') i++;
            else if (c == '\'') state = ST_NORMAL;
            break;
        }
    }
    /* Flush any remaining token */
    if (state == ST_NORMAL && tok_len > 0) {
        tok[tok_len] = '\0';
        if (strcmp(prev1, "struct") == 0 ||
            strcmp(prev1, "union")  == 0 ||
            strcmp(prev1, "enum")   == 0) {
            pool_add(pool, tok);
        }
        if (in_typedef)
            pool_add(pool, tok);
    }
}

/* Extract class and function names from Python source text. */
static void extract_python_symbols_from_text(const char *text, size_t text_len,
                                             completion_pool_t *pool)
{
    enum { ST_NORMAL, ST_LINE_CMT, ST_STRING1, ST_STRING3 } state = ST_NORMAL;

    char tok[MAX_COMPLETION_LEN];
    size_t tok_len = 0;
    char prev1[MAX_COMPLETION_LEN];
    prev1[0] = '\0';
    int triple_count = 0;

    for (size_t i = 0; i < text_len; i++) {
        unsigned char c = (unsigned char)text[i];
        unsigned char next  = (i + 1 < text_len) ? (unsigned char)text[i + 1] : 0;
        unsigned char next2 = (i + 2 < text_len) ? (unsigned char)text[i + 2] : 0;

        switch (state) {
        case ST_NORMAL:
            if (c == '#') {
                state = ST_LINE_CMT;
                goto py_flush;
            } else if (c == '"' && next == '"' && next2 == '"') {
                state = ST_STRING3;
                i += 2;
                triple_count = 0;
                goto py_flush;
            } else if (c == '"' || c == '\'') {
                state = ST_STRING1;
                goto py_flush;
            } else if (isalnum(c) || c == '_') {
                if (tok_len < sizeof(tok) - 1)
                    tok[tok_len++] = (char)c;
            } else {
py_flush:
                if (tok_len > 0) {
                    tok[tok_len] = '\0';
                    /* class NAME or def NAME */
                    if (strcmp(prev1, "class") == 0 ||
                        strcmp(prev1, "def")   == 0) {
                        pool_add(pool, tok);
                    }
                    mystrscpy(prev1, tok, sizeof(prev1));
                    tok_len = 0;
                }
            }
            break;
        case ST_LINE_CMT:
            if (c == '\n') state = ST_NORMAL;
            break;
        case ST_STRING3:
            if (c == '"') {
                triple_count++;
                if (triple_count == 3) state = ST_NORMAL;
            } else {
                triple_count = 0;
            }
            break;
        case ST_STRING1:
            if (c == '\\') i++;
            else if (c == '"' || c == '\'') state = ST_NORMAL;
            break;
        }
    }
    if (state == ST_NORMAL && tok_len > 0) {
        tok[tok_len] = '\0';
        if (strcmp(prev1, "class") == 0 || strcmp(prev1, "def") == 0)
            pool_add(pool, tok);
    }
}

/* Extract function and class names from JS/TS source text. */
static void extract_js_symbols_from_text(const char *text, size_t text_len,
                                         completion_pool_t *pool)
{
    enum { ST_NORMAL, ST_LINE_CMT, ST_BLOCK_CMT, ST_STRING_SQ,
           ST_STRING_DQ, ST_TEMPLATE } state = ST_NORMAL;

    char tok[MAX_COMPLETION_LEN];
    size_t tok_len = 0;
    char prev1[MAX_COMPLETION_LEN];
    char prev2[MAX_COMPLETION_LEN];
    prev1[0] = '\0';
    prev2[0] = '\0';

    for (size_t i = 0; i < text_len; i++) {
        unsigned char c    = (unsigned char)text[i];
        unsigned char next = (i + 1 < text_len) ? (unsigned char)text[i + 1] : 0;

        switch (state) {
        case ST_NORMAL:
            if (c == '/' && next == '/') { state = ST_LINE_CMT; i++; goto js_flush; }
            else if (c == '/' && next == '*') { state = ST_BLOCK_CMT; i++; goto js_flush; }
            else if (c == '\'') { state = ST_STRING_SQ; goto js_flush; }
            else if (c == '"')  { state = ST_STRING_DQ; goto js_flush; }
            else if (c == '`')  { state = ST_TEMPLATE; goto js_flush; }
            else if (isalnum(c) || c == '_' || c == '$') {
                if (tok_len < sizeof(tok) - 1)
                    tok[tok_len++] = (char)c;
            } else {
js_flush:
                if (tok_len > 0) {
                    tok[tok_len] = '\0';
                    /* function NAME or class NAME */
                    if (strcmp(prev1, "function") == 0 ||
                        strcmp(prev1, "class")    == 0) {
                        pool_add(pool, tok);
                    }
                    mystrscpy(prev2, prev1, sizeof(prev2));
                    mystrscpy(prev1, tok, sizeof(prev1));
                    tok_len = 0;
                } else {
                    /* track '=' as a pseudo-token */
                    if ((char)c == '=') {
                        mystrscpy(prev2, prev1, sizeof(prev2));
                        mystrscpy(prev1, "=", sizeof(prev1));
                    }
                }
                /* function IDENT ( → IDENT before '(' */
                if ((char)c == '(' && prev1[0] != '\0' &&
                    strcmp(prev1, "function") != 0 &&
                    strcmp(prev1, "if") != 0 &&
                    strcmp(prev1, "while") != 0 &&
                    strcmp(prev1, "for") != 0 &&
                    strcmp(prev1, "switch") != 0) {
                    /* only add if prev2 looks like a type/keyword indicating definition */
                    if (strcmp(prev2, "function") == 0 ||
                        strcmp(prev2, "async") == 0)
                        pool_add(pool, prev1);
                }
            }
            break;
        case ST_LINE_CMT:
            if (c == '\n') state = ST_NORMAL;
            break;
        case ST_BLOCK_CMT:
            if (c == '*' && next == '/') { state = ST_NORMAL; i++; }
            break;
        case ST_STRING_SQ:
            if (c == '\\') i++;
            else if (c == '\'') state = ST_NORMAL;
            break;
        case ST_STRING_DQ:
            if (c == '\\') i++;
            else if (c == '"') state = ST_NORMAL;
            break;
        case ST_TEMPLATE:
            if (c == '\\') i++;
            else if (c == '`') state = ST_NORMAL;
            break;
        }
    }
    if (state == ST_NORMAL && tok_len > 0) {
        tok[tok_len] = '\0';
        if (strcmp(prev1, "function") == 0 || strcmp(prev1, "class") == 0)
            pool_add(pool, tok);
    }
}

/* Choose extractor based on file extension and dispatch. */
static void extract_symbols_from_text(const char *fname,
                                      const char *text, size_t text_len,
                                      completion_pool_t *pool)
{
    if (fname == NULL || text == NULL || text_len == 0)
        return;
    if (is_c_like_file(fname))
        extract_c_symbols_from_text(text, text_len, pool);
    else if (is_python_file(fname))
        extract_python_symbols_from_text(text, text_len, pool);
    else if (is_node_file(fname))
        extract_js_symbols_from_text(text, text_len, pool);
    /* Java: class/method extraction could be added; for now rely on
     * the existing java_class_cache. */
}

/* Scan a loaded buffer's lines and feed the text into the extractor. */
static void scan_buffer_for_source_symbols(struct buffer *bp,
                                           completion_pool_t *pool)
{
    if (bp == NULL || pool == NULL)
        return;
    if (bp->b_fname[0] == '\0')
        return;

    /* Accumulate lines into a temporary heap buffer */
    struct line *lp = lforw(bp->b_linep);
    size_t total = 0;
    while (lp != bp->b_linep) {
        total += (size_t)llength(lp) + 1; /* +1 for newline */
        lp = lforw(lp);
        if (total >= MAX_SOURCE_FILE_BYTES)
            break;
    }

    char *buf = malloc(total + 1);
    if (!buf)
        return;

    size_t pos = 0;
    lp = lforw(bp->b_linep);
    while (lp != bp->b_linep && pos < total) {
        int len = llength(lp);
        if (len > 0) {
            size_t copy = (size_t)len;
            if (pos + copy > total)
                copy = total - pos;
            memcpy(buf + pos, lp->l_text, copy);
            pos += copy;
        }
        if (pos < total)
            buf[pos++] = '\n';
        lp = lforw(lp);
    }
    buf[pos] = '\0';

    extract_symbols_from_text(bp->b_fname, buf, pos, pool);
    free(buf);
}

/* Read a file from disk and feed its content into the extractor. */
static void scan_file_for_source_symbols(const char *path,
                                         completion_pool_t *pool)
{
    if (path == NULL || path[0] == '\0' || pool == NULL)
        return;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return;

    char *buf = malloc(MAX_SOURCE_FILE_BYTES + 1);
    if (!buf) {
        fclose(fp);
        return;
    }

    size_t total = fread(buf, 1, MAX_SOURCE_FILE_BYTES, fp);
    fclose(fp);
    buf[total] = '\0';

    extract_symbols_from_text(path, buf, total, pool);
    free(buf);
}

/* Free all items in the source symbol cache pool (without freeing the pool struct). */
static void source_symbol_cache_clear(void)
{
    for (int i = 0; i < source_symbol_cache.count; i++) {
        free(source_symbol_cache.items[i]);
        source_symbol_cache.items[i] = NULL;
    }
    source_symbol_cache.count = 0;
}

/* Rebuild the source_symbol_cache from:
 *   1. The current buffer
 *   2. All 4 file-reserve slots (file_reserve[4])
 * Triggered when the current buffer's filename changes. */
static void refresh_source_symbols(void)
{
    const char *cur_fname = (curbp && curbp->b_fname[0]) ? curbp->b_fname : "";

    /* Only rebuild when the active file changes (or first call). */
    if (strcmp(source_symbol_last_fname, cur_fname) == 0 &&
        source_symbol_cache.count > 0)
        return;

    source_symbol_cache_clear();
    mystrscpy(source_symbol_last_fname, cur_fname, sizeof(source_symbol_last_fname));

    /* 1. Current buffer */
    if (curbp)
        scan_buffer_for_source_symbols(curbp, &source_symbol_cache);

    /* 2. Slot files: file_reserve[0..3] */
    for (int slot = 0; slot < 4; slot++) {
        if (file_reserve[slot][0] == '\0')
            continue;
        /* If the slot file is the same as the current buffer, skip
         * (already scanned). */
        if (cur_fname[0] && strcmp(file_reserve[slot], cur_fname) == 0)
            continue;

        /* Check if it is already loaded as a buffer so we can scan
         * in-memory rather than re-reading from disk. */
        struct buffer *bp = bheadp;
        int found_buf = FALSE;
        while (bp) {
            if (strcmp(bp->b_fname, file_reserve[slot]) == 0) {
                scan_buffer_for_source_symbols(bp, &source_symbol_cache);
                found_buf = TRUE;
                break;
            }
            bp = bp->b_bufp;
        }
        if (!found_buf)
            scan_file_for_source_symbols(file_reserve[slot],
                                         &source_symbol_cache);
    }
}

/* Add source-symbol matches for the given prefix. */
static void collect_source_symbol_matches(const char *prefix,
                                          completion_context_t ctx)
{
    if (ctx == COMPLETION_CONTEXT_PATH || prefix == NULL || *prefix == '\0')
        return;

    refresh_source_symbols();
    add_matches_from_pool(&source_symbol_cache, prefix);
}

typedef struct {
    const char *prefix;
} runtime_completion_ctx_t;

static void runtime_completion_callback(const char *symbol, void *data)
{
    runtime_completion_ctx_t *ctx = data;
    if (ctx && symbol)
        completion_consider_candidate(symbol, ctx->prefix);
}

static void add_runtime_module_matches(scraper_lang_t lang, const char *module, const char *prefix)
{
    if (!module || !*module || !prefix)
        return;
    runtime_completion_ctx_t ctx = { prefix };
    scraper_iterate_symbols(lang, module, runtime_completion_callback, &ctx);
}

static int get_owner_symbol_near_cursor(struct line *lp, int prefix_start, char *out, size_t outsz)
{
    int len;
    int dot_pos;
    unicode_t uc;
    int owner_start;
    int owner_end;
    if (lp == NULL || prefix_start <= 0 || out == NULL || outsz == 0)
        return FALSE;
    len = llength(lp);
    dot_pos = prev_char_start(lp, prefix_start);
    if (dot_pos <= 0)
        return FALSE;
    if (utf8_to_unicode(lp->l_text, (unsigned)dot_pos, (unsigned)len, &uc) <= 0)
        return FALSE;
    if (uc != '.')
        return FALSE;
    owner_end = dot_pos;
    owner_start = owner_end;
    while (owner_start > 0) {
        int candidate = prev_char_start(lp, owner_start);
        if (candidate == owner_start)
            break;
        if (utf8_to_unicode(lp->l_text, (unsigned)candidate, (unsigned)len, &uc) <= 0)
            break;
        if (!is_identifier_char(uc))
            break;
        owner_start = candidate;
    }
    if (owner_start == owner_end)
        return FALSE;
    int copy_len = owner_end - owner_start;
    if (copy_len >= (int)outsz)
        copy_len = (int)outsz - 1;
    memcpy(out, &lp->l_text[owner_start], (size_t)copy_len);
    out[copy_len] = '\0';
    return TRUE;
}

static int resolve_java_class_name(const char *owner, char *out, size_t outsz)
{
    struct line *lp;
    char linebuf[256];

    if (owner == NULL || out == NULL || outsz == 0)
        return FALSE;

    if (strchr(owner, '.')) {
        mystrscpy(out, owner, outsz);
        return TRUE;
    }

    if (curbp == NULL)
        return FALSE;

    lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        int len = llength(lp);
        if (len >= (int)sizeof(linebuf))
            len = sizeof(linebuf) - 1;
        memcpy(linebuf, lp->l_text, (size_t)len);
        linebuf[len] = '\0';
        char *p = linebuf;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "import ", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t')
                p++;
            char *semi = strchr(p, ';');
            if (semi)
                *semi = '\0';
            char *star = strstr(p, ".*");
            if (star) {
                *star = '\0';
                snprintf(out, outsz, "%s.%s", p, owner);
                return TRUE;
            } else {
                char *simple = strrchr(p, '.');
                if (simple && strcmp(simple + 1, owner) == 0) {
                    mystrscpy(out, p, outsz);
                    return TRUE;
                }
            }
        }
        lp = lforw(lp);
    }

    snprintf(out, outsz, "java.lang.%s", owner);
    return TRUE;
}

static java_member_entry_t *find_java_member_entry(const char *class_name)
{
    if (class_name == NULL)
        return NULL;
    for (int i = 0; i < java_member_cache_count; i++) {
        if (java_member_cache[i].class_name != NULL &&
            strcmp(java_member_cache[i].class_name, class_name) == 0)
            return &java_member_cache[i];
    }
    return NULL;
}

static java_member_entry_t *get_java_member_entry(const char *class_name)
{
    java_member_entry_t *entry = find_java_member_entry(class_name);
    if (entry)
        return entry;
    if (class_name == NULL || *class_name == '\0')
        return NULL;
    if (java_member_cache_count == java_member_cache_capacity) {
        int new_capacity = java_member_cache_capacity ? java_member_cache_capacity * 2 : 16;
        java_member_entry_t *tmp = realloc(java_member_cache, (size_t)new_capacity * sizeof(java_member_entry_t));
        if (!tmp)
            return NULL;
        java_member_cache = tmp;
        java_member_cache_capacity = new_capacity;
    }
    entry = &java_member_cache[java_member_cache_count++];
    entry->class_name = strdup(class_name);
    if (entry->class_name == NULL) {
        java_member_cache_count--;
        return NULL;
    }
    entry->members.items = NULL;
    entry->members.count = 0;
    entry->members.capacity = 0;
    entry->members.max_items = MAX_JAVA_MEMBERS;
    entry->loaded = 0;
    return entry;
}

static void load_javap_members(java_member_entry_t *entry)
{
    char quoted_cp[sizeof(java_classpath_string) * 2];
    char quoted_class[512];
    char cmd[sizeof(java_classpath_string) * 2 + 512];
    FILE *fp;
    char line[1024];

    if (entry == NULL || entry->loaded)
        return;

    if (!build_shell_quoted(get_java_classpath_string(), quoted_cp, sizeof(quoted_cp))) {
        entry->loaded = 1;
        return;
    }
    if (!build_shell_quoted(entry->class_name, quoted_class, sizeof(quoted_class))) {
        entry->loaded = 1;
        return;
    }
    mystrscpy(cmd, "javap -classpath ", sizeof(cmd));
    size_t pos = strlen(cmd);
    if (pos + strlen(quoted_cp) + 1 >= sizeof(cmd)) {
        entry->loaded = 1;
        return;
    }
    memcpy(cmd + pos, quoted_cp, strlen(quoted_cp));
    pos += strlen(quoted_cp);
    if (pos + 1 >= sizeof(cmd)) {
        entry->loaded = 1;
        return;
    }
    cmd[pos++] = ' ';
    if (pos + strlen(quoted_class) + 1 >= sizeof(cmd)) {
        entry->loaded = 1;
        return;
    }
    memcpy(cmd + pos, quoted_class, strlen(quoted_class));
    pos += strlen(quoted_class);
    cmd[pos] = '\0';
    fp = popen(cmd, "r");
    if (!fp) {
        entry->loaded = 1;
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '{' || *p == '}' || strncmp(p, "Compiled from", 13) == 0)
            continue;
        char *paren = strchr(p, '(');
        if (paren) {
            char *name_start = paren;
            while (name_start > p &&
                   (isalnum((unsigned char)name_start[-1]) || name_start[-1] == '_' || name_start[-1] == '$'))
                name_start--;
            if (name_start < paren)
                add_symbol_token(&entry->members, name_start, (size_t)(paren - name_start));
        } else {
            char *semi = strchr(p, ';');
            if (semi) {
                char *name_end = semi;
                while (name_end > p && isspace((unsigned char)name_end[-1]))
                    name_end--;
                char *name_start = name_end;
                while (name_start > p &&
                       (isalnum((unsigned char)name_start[-1]) || name_start[-1] == '_' || name_start[-1] == '$'))
                    name_start--;
                if (name_start < name_end)
                    add_symbol_token(&entry->members, name_start, (size_t)(name_end - name_start));
            }
        }
        if (entry->members.max_items > 0 && entry->members.count >= entry->members.max_items)
            break;
    }

    pclose(fp);
    entry->loaded = 1;
}

static void add_java_member_matches(const char *class_name, const char *prefix)
{
    java_member_entry_t *entry;
    if (class_name == NULL || prefix == NULL || *prefix == '\0')
        return;
    entry = get_java_member_entry(class_name);
    if (!entry)
        return;
    if (!entry->loaded)
        load_javap_members(entry);
    if (entry->members.count == 0)
        return;
    add_matches_from_pool(&entry->members, prefix);
}


static void expand_tilde(const char *input, char *output, size_t outsz)
{
    if (input && input[0] == '~') {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(output, outsz, "%s%s", home, input + 1);
            return;
        }
    }
    mystrscpy(output, input ? input : "", outsz);
}

static int file_exists(const char *path)
{
    struct stat st;
    if (path == NULL || *path == '\0')
        return FALSE;
    return stat(path, &st) == 0;
}

static int executable_in_path(const char *name)
{
    if (!name || !*name)
        return FALSE;
#ifdef USE_WINDOWS
    return TRUE;
#else
    const char *path = getenv("PATH");
    if (!path || !*path)
        return FALSE;
    char *dup = strdup(path);
    if (!dup)
        return FALSE;
    int found = FALSE;
    char *seg = dup;
    while (*seg) {
        char *end = seg;
        while (*end && *end != ':')
            end++;
        char save = *end;
        *end = '\0';
        char buf[NFILEN];
        snprintf(buf, sizeof(buf), "%s/%s", (*seg ? seg : "."), name);
        if (access(buf, X_OK) == 0) {
            found = TRUE;
            break;
        }
        if (!save)
            break;
        seg = end + 1;
    }
    free(dup);
    return found;
#endif
}

typedef struct {
    const char *ext;
    const char *server_cmd;
} lsp_dep_entry_t;

static int completion_should_use_lsp(void)
{
    if (!nanox_cfg.autocomplete || !nanox_cfg.use_lsp || !curbp || !curbp->b_fname[0])
        return FALSE;
    static const lsp_dep_entry_t deps[] = {
        { ".c", "clangd" }, { ".h", "clangd" }, { ".cpp", "clangd" }, { ".hpp", "clangd" },
        { ".cc", "clangd" }, { ".cxx", "clangd" }, { ".m", "clangd" }, { ".mm", "clangd" },
        { ".py", "pylsp" }, { ".py", "pyright-langserver" },
        { ".js", "typescript-language-server" }, { ".jsx", "typescript-language-server" },
        { ".ts", "typescript-language-server" }, { ".tsx", "typescript-language-server" },
        { ".go", "gopls" }, { ".rs", "rust-analyzer" }, { ".java", "jdtls" }
    };
    for (size_t i = 0; i < sizeof(deps) / sizeof(deps[0]); i++) {
        if (has_extension(curbp->b_fname, &deps[i].ext, 1) && executable_in_path(deps[i].server_cmd))
            return TRUE;
    }
    return FALSE;
}

static void normalize_path(char *path)
{
    size_t len;
    if (path == NULL)
        return;
    len = strlen(path);
    while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
        path[len - 1] = '\0';
        len--;
    }
}

static int is_path_delim(char ch)
{
    return ch == ':' || ch == ';';
}

static void add_path_entry(completion_pool_t *paths, const char *entry)
{
    char expanded[NFILEN];
    if (paths == NULL)
        return;
    if (entry == NULL || *entry == '\0')
        entry = ".";
    expand_tilde(entry, expanded, sizeof(expanded));
    normalize_path(expanded);
    if (expanded[0] == '\0')
        return;
    pool_add(paths, expanded);
}

static void parse_path_list(const char *value, completion_pool_t *paths)
{
    char *dup;
    char *segment;
    char *iter;

    if (value == NULL || *value == '\0' || paths == NULL)
        return;

    dup = strdup(value);
    if (dup == NULL)
        return;

    segment = dup;
    while (*segment) {
        iter = segment;
        while (*iter && !is_path_delim(*iter))
            iter++;
        if (*iter) {
            *iter = '\0';
            add_path_entry(paths, segment);
            segment = iter + 1;
            if (*segment == '\0')
                add_path_entry(paths, ".");
        } else {
            add_path_entry(paths, segment);
            break;
        }
    }
    free(dup);
}

static int has_extension(const char *name, const char *const *exts, size_t count)
{
    const char *dot;
    if (name == NULL)
        return FALSE;
    dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0')
        return FALSE;
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(dot, exts[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

#ifndef USE_WINDOWS
static int is_dir_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return FALSE;
    return S_ISDIR(st.st_mode);
}
#endif

static void collect_path_completions(const char *prefix)
{
#ifdef USE_WINDOWS
    (void)prefix;
    /* Path completion is not yet implemented for Windows builds. */
    return;
#else
    char expanded[NFILEN];
    expand_tilde(prefix, expanded, sizeof(expanded));

    const char *orig_slash = strrchr(prefix, '/');
    size_t orig_dir_len = orig_slash ? (size_t)(orig_slash - prefix + 1) : 0;
    char orig_dir[NFILEN];
    if (orig_dir_len > 0) {
        size_t copy_len = orig_dir_len < sizeof(orig_dir) - 1 ? orig_dir_len : sizeof(orig_dir) - 1;
        memcpy(orig_dir, prefix, copy_len);
        orig_dir[copy_len] = '\0';
    } else {
        orig_dir[0] = '\0';
    }

    const char *slash = strrchr(expanded, '/');
    const char *base = slash ? slash + 1 : expanded;
    size_t dir_len = slash ? (size_t)(slash - expanded + 1) : 0;
    char dir[NFILEN];
    if (dir_len > 0) {
        size_t copy_len = dir_len < sizeof(dir) - 1 ? dir_len : sizeof(dir) - 1;
        memcpy(dir, expanded, copy_len);
        dir[copy_len] = '\0';
    } else {
        strcpy(dir, ".");
    }

    DIR *dp = opendir(dir);
    if (dp == NULL)
        return;

    size_t base_len = strlen(base);
    struct dirent *dent;
    while ((dent = readdir(dp)) != NULL && completion_state.count < MAX_COMPLETIONS) {
        if (dent->d_name[0] == '.' && base[0] != '.')
            continue;
        if (strncmp(dent->d_name, base, base_len) != 0)
            continue;

        char full_path[NFILEN];
        if (dir_len > 0 && strcmp(dir, ".") != 0) {
            mystrscpy(full_path, dir, sizeof(full_path));
            size_t used = strlen(full_path);
            if (used < sizeof(full_path) - 1)
                mystrscpy(full_path + used, dent->d_name, sizeof(full_path) - used);
        } else {
            mystrscpy(full_path, dent->d_name, sizeof(full_path));
        }

        int is_dir = is_dir_path(full_path);

        char display[MAX_COMPLETION_LEN];
        if (orig_dir_len > 0) {
            size_t copy = orig_dir_len;
            if (copy > sizeof(display) - 1)
                copy = sizeof(display) - 1;
            memcpy(display, prefix, copy);
            display[copy] = '\0';
            if (copy < sizeof(display) - 1)
                mystrscpy(display + copy, dent->d_name, sizeof(display) - copy);
        } else {
            mystrscpy(display, dent->d_name, sizeof(display));
        }
        size_t used = strlen(display);
        if (is_dir && used < sizeof(display) - 1) {
            display[used++] = '/';
            display[used] = '\0';
        }
        completion_consider_candidate(display, prefix);
    }
    closedir(dp);
#endif
}

void completion_init(void)
{
    completion_reset_state();
}

void completion_update(const char *prefix, completion_context_t ctx)
{
    if (!nanox_cfg.autocomplete) {
        completion_reset_state();
        return;
    }
    if (prefix == NULL || *prefix == '\0') {
        completion_reset_state();
        return;
    }

    completion_reset_state();
    completion_active_ctx = ctx;
    mystrscpy(completion_active_prefix, prefix, sizeof(completion_active_prefix));

    if (ctx == COMPLETION_CONTEXT_PATH) {
        collect_path_completions(prefix);
    } else {
        collect_buffer_words(prefix);
        collect_language_keywords(prefix);
        collect_common_keywords(prefix);
    }

    completion_state.is_visible = (completion_state.count > 0);
}

static int completion_visible_rows(void)
{
    int limit = completion_dropdown_state.active ? COMPLETION_POPUP_MAX_VISIBLE : COMPLETION_MINIBUFFER_MAX_VISIBLE;
    if (completion_state.count < limit)
        return completion_state.count;
    return limit;
}

static void completion_ensure_visible(void)
{
    int height = completion_visible_rows();
    if (height <= 0) {
        completion_state.scroll_offset = 0;
        return;
    }

    if (completion_state.scroll_offset < 0)
        completion_state.scroll_offset = 0;

    int max_offset = completion_state.count - height;
    if (max_offset < 0)
        max_offset = 0;
    if (completion_state.scroll_offset > max_offset)
        completion_state.scroll_offset = max_offset;

    if (completion_state.selected_index < completion_state.scroll_offset)
        completion_state.scroll_offset = completion_state.selected_index;

    if (completion_state.selected_index >= completion_state.scroll_offset + height)
        completion_state.scroll_offset = completion_state.selected_index - height + 1;

    if (completion_state.scroll_offset < 0)
        completion_state.scroll_offset = 0;
}

static void completion_draw_minibuffer_list(int row, int col)
{
    if (!completion_state.is_visible)
        return;

    completion_ensure_visible();

    int saved_row = ttrow;
    int saved_col = ttcol;
    int height = completion_visible_rows();

    for (int i = 0; i < height; i++) {
        int idx = completion_state.scroll_offset + i;
        if (idx >= completion_state.count)
            break;
        int r = row - height + i;
        if (r < 0)
            continue;

        movecursor(r, col);
        if (idx == completion_state.selected_index)
            TTrev(TRUE);

        int list_width = term->t_ncol - col;
        if (list_width < 8)
            list_width = 8;

        TTputc(' ');
        completion_write_utf8_ellipsized(completion_state.matches[idx] ? completion_state.matches[idx] : "", list_width - 2);
        TTputc(' ');
        TTeeol();

        if (idx == completion_state.selected_index)
            TTrev(FALSE);
    }
    if (saved_row < HUGE && saved_col < HUGE)
        movecursor(saved_row, saved_col);
    TTflush();
}

void completion_draw(int row, int col)
{
    if (!completion_state.is_visible)
        return;

    if (completion_dropdown_state.active) {
        completion_draw_popup_box();
        return;
    }

    completion_draw_minibuffer_list(row, col);
}

const char* completion_get_selected(void) {
    if (completion_state.is_visible &&
        completion_state.selected_index >= 0 &&
        completion_state.selected_index < completion_state.count) {
        return completion_state.matches[completion_state.selected_index];
    }
    return NULL;
}

const char *completion_get_best_hint(const char *prefix)
{
    if (completion_state.count == 0 || !prefix || !*prefix)
        return NULL;
    
    /* We only show a hint if the similarity is high (score > threshold) 
     * and the first match actually starts with the prefix. */
    const char *best = completion_state.matches[0];
    if (best && strncasecmp(best, prefix, strlen(prefix)) == 0) {
        if (strlen(best) > strlen(prefix)) {
            return best + strlen(prefix);
        }
    }
    return NULL;
}

void completion_next(void) {
    if (completion_state.count <= 0)
        return;
    if (completion_state.selected_index < 0 || completion_state.selected_index >= completion_state.count)
        completion_state.selected_index = 0;
    completion_state.selected_index = (completion_state.selected_index + 1) % completion_state.count;
    completion_ensure_visible();
}

void completion_prev(void) {
    if (completion_state.count <= 0)
        return;
    if (completion_state.selected_index < 0 || completion_state.selected_index >= completion_state.count)
        completion_state.selected_index = 0;
    completion_state.selected_index = (completion_state.selected_index - 1 + completion_state.count) % completion_state.count;
    completion_ensure_visible();
}

void completion_hide(void) {
    completion_state.is_visible = 0;
    completion_state.scroll_offset = 0;
}

static int prev_char_start(struct line *lp, int pos)
{
    if (lp == NULL || pos <= 0)
        return 0;
    do {
        pos--;
    } while (pos > 0 && !is_beginning_utf8(lp->l_text[pos]));
    return pos;
}

static int extract_word_prefix(struct line *lp, int offset, char *dest, size_t dest_sz, int *start_out)
{
    if (lp == NULL || dest == NULL || dest_sz == 0)
        return FALSE;
    int len = llength(lp);
    if (offset > len)
        offset = len;

    int start = offset;
    while (start > 0) {
        int candidate = prev_char_start(lp, start);
        unicode_t uc = 0;
        int bytes = utf8_to_unicode(lp->l_text, (unsigned)candidate, (unsigned)len, &uc);
        if (bytes <= 0)
            bytes = 1;
        if (!is_identifier_char(uc))
            break;
        start = candidate;
    }

    if (start == offset)
        return FALSE;

    int copy_len = offset - start;
    if (copy_len >= (int)dest_sz)
        copy_len = (int)dest_sz - 1;
    memcpy(dest, &lp->l_text[start], (size_t)copy_len);
    dest[copy_len] = '\0';
    if (start_out)
        *start_out = start;
    return TRUE;
}

static int extract_path_prefix(struct line *lp, int offset, char *dest, size_t dest_sz, int *start_out)
{
    if (lp == NULL || dest == NULL || dest_sz == 0)
        return FALSE;
    int len = llength(lp);
    if (offset > len)
        offset = len;

    int start = offset;
    while (start > 0) {
        int candidate = prev_char_start(lp, start);
        unicode_t uc = 0;
        int bytes = utf8_to_unicode(lp->l_text, (unsigned)candidate, (unsigned)len, &uc);
        if (bytes <= 0)
            bytes = 1;
        if (!is_path_char(uc))
            break;
        start = candidate;
    }

    if (start == offset)
        return FALSE;

    int copy_len = offset - start;
    if (copy_len >= (int)dest_sz)
        copy_len = (int)dest_sz - 1;
    memcpy(dest, &lp->l_text[start], (size_t)copy_len);
    dest[copy_len] = '\0';
    if (start_out)
        *start_out = start;

    if (strchr(dest, '/') != NULL || dest[0] == '/' || dest[0] == '~' ||
        (dest[0] == '.' && dest[1] != '\0')) {
        return TRUE;
    }
    return FALSE;
}

static size_t completion_calculate_dotted_token_delete_len(const char *match, struct line *lp, int offset, size_t delete_len)
{
    if (match == NULL || lp == NULL || strchr(match, '.') == NULL || offset <= 0)
        return delete_len;

    int len = llength(lp);
    if (offset > len)
        offset = len;

    int start = offset;
    while (start > 0) {
        int candidate = prev_char_start(lp, start);
        unicode_t uc = 0;
        int bytes = utf8_to_unicode(lp->l_text, (unsigned)candidate, (unsigned)len, &uc);
        if (bytes <= 0)
            bytes = 1;
        if (!is_identifier_char(uc) && uc != '.' && uc != '$')
            break;
        start = candidate;
    }

    if (start >= offset)
        return delete_len;

    size_t expanded = (size_t)(offset - start);
    if (expanded > delete_len)
        return expanded;
    return delete_len;
}

static int completion_is_import_include_context(struct line *lp)
{
    if (lp == NULL)
        return FALSE;

    int len = llength(lp);
    int i = 0;
    while (i < len && (lp->l_text[i] == ' ' || lp->l_text[i] == '\t'))
        i++;

    if (i >= len)
        return FALSE;

    if (i + 8 <= len && memcmp(&lp->l_text[i], "#include", 8) == 0) {
        if (i + 8 == len || isspace((unsigned char)lp->l_text[i + 8]) ||
            lp->l_text[i + 8] == '<' || lp->l_text[i + 8] == '"')
            return TRUE;
    }
    if (i + 7 <= len && memcmp(&lp->l_text[i], "include", 7) == 0) {
        if (i + 7 == len || isspace((unsigned char)lp->l_text[i + 7]) ||
            lp->l_text[i + 7] == '<' || lp->l_text[i + 7] == '"')
            return TRUE;
    }
    if (i + 6 <= len && memcmp(&lp->l_text[i], "import", 6) == 0) {
        if (i + 6 == len || isspace((unsigned char)lp->l_text[i + 6]))
            return TRUE;
    }
    return FALSE;
}

static int completion_has_terminator_ahead(struct line *lp, int offset, char terminator)
{
    if (lp == NULL || terminator == '\0')
        return TRUE;

    int len = llength(lp);
    if (offset < 0)
        offset = 0;
    if (offset > len)
        offset = len;

    for (int i = offset; i < len; i++) {
        unsigned char c = (unsigned char)lp->l_text[i];
        if (c == ' ' || c == '\t')
            continue;
        return c == (unsigned char)terminator;
    }
    return FALSE;
}

static int determine_completion_prefix(char *out, size_t out_sz,
                                       completion_context_t *ctx,
                                       struct line **line_out,
                                       int *start_out,
                                       int *end_out)
{
    if (curwp == NULL || curbp == NULL || out == NULL || out_sz == 0)
        return FALSE;

    struct line *lp = curwp->w_dotp;
    if (lp == NULL || lp == curbp->b_linep)
        return FALSE;

    int offset = curwp->w_doto;
    if (offset > llength(lp))
        offset = llength(lp);

    if (extract_path_prefix(lp, offset, out, out_sz, start_out)) {
        if (ctx)
            *ctx = COMPLETION_CONTEXT_PATH;
        if (line_out)
            *line_out = lp;
        if (end_out)
            *end_out = offset;
        return TRUE;
    }

    if (extract_word_prefix(lp, offset, out, out_sz, start_out)) {
        if (ctx)
            *ctx = COMPLETION_CONTEXT_DEFAULT;
        if (line_out)
            *line_out = lp;
        if (end_out)
            *end_out = offset;
        return TRUE;
    }

    return FALSE;
}

static void completion_insert_text(const char *text)
{
    if (text == NULL || *text == '\0')
        return;
    linsert_block((char *)text, (int)strlen(text));
}

static void completion_insert_terminator_if_needed(void)
{
    if (curbp == NULL || curwp == NULL || curwp->w_dotp == NULL)
        return;

    const HighlightProfile *profile = highlight_get_profile(curbp->b_fname);
    if (profile == NULL || profile->completion_end_line_char == '\0')
        return;
    if (!completion_is_import_include_context(curwp->w_dotp))
        return;
    if (completion_has_terminator_ahead(curwp->w_dotp, curwp->w_doto, profile->completion_end_line_char))
        return;

    linsert(1, profile->completion_end_line_char);
}

static void completion_dropdown_activate(size_t prefix_len)
{
    completion_dropdown_state.active = 1;
    completion_dropdown_state.focused = 0;
    completion_dropdown_state.tab_primed = 0;
    completion_dropdown_state.prefix_len = prefix_len;
    completion_state.selected_index = 0;
    completion_state.scroll_offset = 0;
    completion_state.is_visible = (completion_state.count > 0);
    completion_dropdown_refresh_geometry();
    completion_ensure_visible();
}

static void completion_dropdown_deactivate(int commit_preview)
{
    completion_dropdown_state.active = 0;
    completion_dropdown_state.focused = 0;
    completion_dropdown_state.tab_primed = 0;
    if (commit_preview)
        completion_preview_commit();
    else
        completion_preview_abort();
    completion_hide();
}

static void completion_dropdown_apply_selection(void)
{
    int had_preview = completion_preview_state.active;
    int applied = FALSE;
    const char *match;

    if (had_preview)
        completion_preview_abort();

    match = completion_get_selected();
    if (match != NULL && curwp && curwp->w_dotp) {
        int original_doto;

        original_doto = curwp->w_doto;
        if (original_doto < 0) {
            TTbeep();
        } else {
            size_t safe_doto = (size_t)original_doto;
            size_t delete_len = completion_dropdown_state.prefix_len;
            if (completion_is_import_include_context(curwp->w_dotp))
                delete_len = completion_calculate_dotted_token_delete_len(match, curwp->w_dotp, original_doto, delete_len);
            if (safe_doto < delete_len)
                delete_len = safe_doto;
            if (delete_len > 0) {
                curwp->w_doto = original_doto - (int)delete_len;
                ldelete((long)delete_len, FALSE);
            }
            completion_insert_text(match);
            completion_insert_terminator_if_needed();
            applied = TRUE;
        }
    }
    completion_dropdown_deactivate(applied ? 1 : 0);
}

static void completion_dropdown_refresh_geometry(void)
{
    if (!completion_dropdown_state.active)
        return;

    completion_ensure_visible();

    int visible = completion_visible_rows();
    if (visible <= 0)
        visible = 1;
    completion_dropdown_state.popup_height = visible;

    int content_width = COMPLETION_POPUP_FIXED_CONTENT;
    if (content_width < COMPLETION_POPUP_MIN_CONTENT)
        content_width = COMPLETION_POPUP_MIN_CONTENT;
    if (content_width > COMPLETION_POPUP_MAX_CONTENT)
        content_width = COMPLETION_POPUP_MAX_CONTENT;

    int popup_width = content_width + 2; /* 1-char side padding */

    int safe_cols = term->t_ncol;
    if (popup_width > safe_cols) {
        popup_width = safe_cols;
        if (popup_width < 8)
            popup_width = 8;
    }
    completion_dropdown_state.popup_width = popup_width;

    int total_height = completion_dropdown_state.popup_height;
    int safe_bottom = term->t_nrow - 1; /* reserve last line for minibuffer */
    if (safe_bottom < 0)
        safe_bottom = 0;

    int desired_row = currow + 1;
    if (desired_row + total_height - 1 > safe_bottom)
        desired_row = currow - total_height;
    if (desired_row < 0)
        desired_row = 0;
    if (desired_row + total_height - 1 > safe_bottom)
        desired_row = safe_bottom - total_height + 1;
    if (desired_row < 0)
        desired_row = 0;
    completion_dropdown_state.popup_row = desired_row;

    int desired_col = curcol;
    if (desired_col + popup_width >= safe_cols)
        desired_col = safe_cols - popup_width;
    if (desired_col < 0)
        desired_col = 0;
    completion_dropdown_state.popup_col = desired_col;
}

static void completion_draw_popup_box(void)
{
    if (!completion_dropdown_state.active || !completion_state.is_visible)
        return;

    completion_dropdown_refresh_geometry();
    completion_ensure_visible();

    int box_row = completion_dropdown_state.popup_row;
    int box_col = completion_dropdown_state.popup_col;
    int popup_width = completion_dropdown_state.popup_width;
    int visible = completion_dropdown_state.popup_height;
    if (popup_width < 8)
        popup_width = 8;
    if (visible <= 0)
        return;

    int saved_row = ttrow;
    int saved_col = ttcol;

    int text_width = popup_width - 2;
    if (text_width < 1)
        text_width = 1;

    HighlightStyle normal = completion_resolve_style(colorscheme_get(HL_NORMAL), colorscheme_get(HL_NORMAL));
    HighlightStyle selection = completion_resolve_style(colorscheme_get(HL_SELECTION), normal);
    HighlightStyle notice = completion_resolve_style(colorscheme_get(HL_NOTICE), normal);
    HighlightStyle panel_style = notice;
    if (panel_style.bg == -1)
        panel_style.bg = normal.bg;
    if (panel_style.fg == -1)
        panel_style.fg = normal.fg;
    HighlightStyle row_style = panel_style;
    HighlightStyle selected_style = selection;
    if (selected_style.bg == -1)
        selected_style.bg = panel_style.bg;
    if (selected_style.fg == -1)
        selected_style.fg = panel_style.fg;

    /* Content rows */
    for (int i = 0; i < visible; i++) {
        int idx = completion_state.scroll_offset + i;
        int line_row = box_row + i;
        const char *text = (idx < completion_state.count) ? completion_state.matches[idx] : "";
        HighlightStyle *active_style = (idx == completion_state.selected_index) ? &selected_style : &row_style;

        movecursor(line_row, box_col);
        completion_apply_style(active_style);
        TTputc(' ');
        if (idx < completion_state.count)
            completion_write_utf8_ellipsized(text ? text : "", text_width);
        else
            completion_write_utf8_clipped("", text_width);
        TTputc(' ');
    }

    TTsetcolors(-1, -1);
    TTsetattrs(0, 0, 0);
    if (saved_row < HUGE && saved_col < HUGE)
        movecursor(saved_row, saved_col);
    TTflush();
}

int completion_try_at_cursor(void)
{
    if (!nanox_cfg.autocomplete)
        return FALSE;

    char prefix[MAX_COMPLETION_LEN];
    completion_context_t ctx = COMPLETION_CONTEXT_DEFAULT;
    struct line *line = NULL;
    int prefix_start = 0;
    completion_preview_abort();

    if (!determine_completion_prefix(prefix, sizeof(prefix), &ctx, &line, &prefix_start, NULL))
        return FALSE;

    completion_update(prefix, ctx);
    int lsp_active = completion_should_use_lsp();
    add_language_specific_matches(prefix, ctx);
    collect_source_symbol_matches(prefix, ctx);

    if (lsp_active && line && ctx == COMPLETION_CONTEXT_DEFAULT) {
        char owner[MAX_COMPLETION_LEN];
        if (get_owner_symbol_near_cursor(line, prefix_start, owner, sizeof(owner))) {
            if (is_java_file(curbp ? curbp->b_fname : NULL)) {
                char resolved[256];
                if (resolve_java_class_name(owner, resolved, sizeof(resolved)))
                    add_java_member_matches(resolved, prefix);
            } else if (is_python_file(curbp ? curbp->b_fname : NULL)) {
                add_runtime_module_matches(SCRAPER_LANG_PYTHON, owner, prefix);
            } else if (is_node_file(curbp ? curbp->b_fname : NULL)) {
                add_runtime_module_matches(SCRAPER_LANG_NODE, owner, prefix);
            }
        }
    }

    if (completion_state.count == 0)
        return FALSE;

    size_t prefix_len = strlen(prefix);

    completion_dropdown_activate(prefix_len);
    if (line) {
        completion_preview_begin(line, prefix_start, (int)prefix_len);
        if (completion_dropdown_state.focused)
            completion_preview_apply_selected();
    }
    return TRUE;
}

int completion_dropdown_is_active(void)
{
    return completion_dropdown_state.active;
}

int completion_dropdown_handle_key(int key)
{
    if (!completion_dropdown_state.active)
        return 0;

    if (!completion_dropdown_state.focused) {
        if (key == (CONTROL | 'I')) {
            if (completion_dropdown_state.tab_primed) {
                completion_dropdown_state.focused = 1;
                completion_dropdown_state.tab_primed = 0;
                completion_preview_apply_selected();
            } else {
                completion_dropdown_state.tab_primed = 1;
            }
            return 1;
        }

        completion_dropdown_state.tab_primed = 0;
        if (key == (CONTROL | '[') || key == (CONTROL | 'G')) {
            completion_dropdown_deactivate(0);
            return 1;
        }
        completion_dropdown_deactivate(0);
        return 0;
    }

    switch (key) {
    case (SPEC | 'A'):
    case (CONTROL | 'P'):
    case (SPEC | 'P'):
        completion_prev();
        completion_preview_apply_selected();
        return 1;
    case (SPEC | 'B'):
    case (CONTROL | 'N'):
    case (SPEC | 'N'):
    case (CONTROL | '@'):
    case (CONTROL | 'I'):
        completion_next();
        completion_preview_apply_selected();
        return 1;
    case (CONTROL | 'M'):
    case '\n':
    case '\r':
        completion_dropdown_apply_selection();
        return 1;
    case (SPEC | 'C'):
    case (SPEC | 'D'):
    case (CONTROL | 'B'):
    case (CONTROL | 'F'):
        completion_dropdown_deactivate(0);
        return 0;
    case (CONTROL | '['):
    case (CONTROL | 'G'):
        completion_dropdown_deactivate(0);
        return 1;
    default:
        completion_dropdown_deactivate(0);
        return 0;
    }
}

void completion_dropdown_render(void)
{
    if (!completion_dropdown_state.active)
        return;
    completion_draw(-1, -1);
}
