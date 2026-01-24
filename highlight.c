#include "highlight.h"
#include "colorscheme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

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

void highlight_init(const char *rule_config_path)
{
	profile_count = 0;
	global_config.enable_colorscheme = true;
	mystrscpy(global_config.colorscheme_name, "nanox-dark", sizeof(global_config.colorscheme_name));

	bool loaded = false;
	FILE *f = NULL;

	if (rule_config_path && *rule_config_path)
		f = fopen(rule_config_path, "r");

	if (f) {
		char line[512];
		HighlightProfile *curr = NULL;

		while (fgets(line, sizeof(line), f)) {
			char *p = trim(line);
			if (*p == 0 || *p == ';' || *p == '#')
				continue;

			if (*p == '[') {
				char *end = strchr(p, ']');
				if (end) {
					*end = 0;
					char *sect = p + 1;
					if (strcasecmp(sect, "highlight") == 0) {
						curr = NULL; /* Switch to global context */
					} else {
						if (profile_count < MAX_PROFILES) {
							curr = &profiles[profile_count++];
							profile_init(curr, sect);
						} else {
							curr = NULL; /* Overflow */
						}
					}
				}
				continue;
			}

			char *eq = strchr(p, '=');
			if (!eq)
				continue;
			*eq = 0;
			char *key = trim(p);
			char *val = trim(eq + 1);

			if (!curr) {
				/* Global Config */
				if (strcmp(key, "enable_colorscheme") == 0)
					global_config.enable_colorscheme = (strcasecmp(val, "true") == 0);
				else if (strcmp(key, "colorscheme") == 0)
					mystrscpy(global_config.colorscheme_name, val, sizeof(global_config.colorscheme_name));
				continue;
			}

			/* Profile Config */
			if (strcmp(key, "extensions") == 0) {
				char *tok = strtok(val, ",");
				while (tok && curr->ext_count < MAX_EXTS) {
					mystrscpy(curr->extensions[curr->ext_count++], trim(tok), MAX_EXT_LEN);
					tok = strtok(NULL, ",");
				}
			} else if (strcmp(key, "line_comment_tokens") == 0) {
				char *tok = strtok(val, ",");
				while (tok && curr->line_comment_count < MAX_TOKENS) {
					mystrscpy(curr->line_comments[curr->line_comment_count++], trim(tok), MAX_TOKEN_LEN);
					tok = strtok(NULL, ",");
				}
			} else if (strcmp(key, "block_comment_pairs") == 0) {
				/* START END, START END */
				char *tok = strtok(val, ",");
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
				/* val is like ",',` */
				int j = 0;
				for (int i = 0; val[i]; i++) {
					if (val[i] != ',' && !isspace(val[i]) && j < MAX_TOKENS - 1) {
						curr->string_delims[j++] = val[i];
					}
				}
				curr->string_delims[j] = 0;
			} else if (strcmp(key, "keywords") == 0) {
				char *tok = strtok(val, ",");
				while (tok && curr->keyword_count < MAX_TOKENS * 8) {
					mystrscpy(curr->keywords[curr->keyword_count++], trim(tok), MAX_TOKEN_LEN);
					tok = strtok(NULL, ",");
				}
			} else if (strcmp(key, "return_keywords") == 0) {
				char *tok = strtok(val, ",");
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
		loaded = profile_count > 0;
	}

	if (global_config.enable_colorscheme) {
		colorscheme_init(global_config.colorscheme_name);
	}
	initialized = loaded;
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

void highlight_line(const char *text, int len, HighlightState start, const HighlightProfile *profile, SpanVec *out, HighlightState *end)
{
	out->count = 0;
	out->heap_spans = NULL;
	out->capacity = 0;
	*end = start;

	if (!text) len = 0;
	if (len < 0 && text) len = strlen(text);
	
	if (!profile) return; /* No highlighting if no profile */

	HighlightState state = start;
	int pos = 0;

	while (pos < len) {
		if (state.state == HS_NORMAL) {
			
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

			/* 3. Check Strings */
			if (profile->enable_triple_quotes && strncmp(text + pos, "\"\"\"", 3) == 0) {
                     state.state = HS_TRIPLE_STRING;
                     state.sub_id = '\"';
                     int search = pos + 3;
                     bool found = false;
                     while (search <= len - 3) {
                         if (strncmp(text + search, "\"\"\"", 3) == 0) {
                             found = true;
                             search += 3;
                             break;
                         }
                         if (text[search] == '\\') search++;
                         search++;
                     }
                     if (found) {
                         add_span(out, pos, search, HL_STRING);
                         pos = search;
                         state.state = HS_NORMAL;
                     } else {
                         add_span(out, pos, len, HL_STRING);
                         pos = len;
                     }
                     continue;
            }

            char c = text[pos];
            char *delim_ptr = strchr(profile->string_delims, c);
            if (delim_ptr && *delim_ptr) {
                state.state = HS_STRING;
                state.sub_id = c;
                
                int search = pos + 1;
                bool found = false;
                while (search < len) {
                    if (text[search] == '\\') {
                        search += 2;
                        continue;
                    }
                    if (text[search] == c) {
                        found = true;
                        search++;
                        break;
                    }
                    search++;
                }
                
                if (found) {
                    add_span(out, pos, search, HL_STRING);
                    pos = search;
                    state.state = HS_NORMAL;
                } else {
                    add_span(out, pos, len, HL_STRING);
                    pos = len;
                }
                continue;
            }

            /* 4. Numbers */
            if (profile->enable_number_highlight && (isdigit(c) || (c == '.' && isdigit(text[pos+1])))) {
                int search = pos;
                bool is_hex = false;
                if (c == '0' && (text[pos+1] == 'x' || text[pos+1] == 'X')) {
                    is_hex = true;
                    search += 2;
                }
                while (search < len) {
                    char nc = text[search];
                    if (is_hex) {
                        if (!isxdigit(nc)) break;
                    } else {
                        if (!isdigit(nc) && nc != '.' && nc != 'e' && nc != 'E') break;
                    }
                    search++;
                }
                add_span(out, pos, search, HL_NUMBER);
                pos = search;
                continue;
            }

            /* 5. Punctuation and Operators */
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

            /* Scan to next token */
            int next_stop = pos;
            while (next_stop < len && (isalnum(text[next_stop]) || text[next_stop] == '_')) {
                next_stop++;
            }

            if (next_stop > pos) {
                /* Potential keyword */
                char word[MAX_TOKEN_LEN];
                int word_len = next_stop - pos;
                if (word_len < MAX_TOKEN_LEN) {
                    memcpy(word, text + pos, word_len);
                    word[word_len] = 0;

                    bool is_kw = false;
                    for (int i = 0; i < profile->return_keyword_count; i++) {
                        if (strcmp(word, profile->return_keywords[i]) == 0) {
                            add_span(out, pos, next_stop, HL_RETURN);
                            is_kw = true;
                            break;
                        }
                    }
                    if (!is_kw) {
                        for (int i = 0; i < profile->keyword_count; i++) {
                            if (strcmp(word, profile->keywords[i]) == 0) {
                                add_span(out, pos, next_stop, HL_KEYWORD);
                                is_kw = true;
                                break;
                            }
                        }
                    }
                    if (is_kw) {
                        pos = next_stop;
                        continue;
                    }
                }
                add_span(out, pos, next_stop, HL_NORMAL);
                pos = next_stop;
                continue;
            }

            next_stop = pos + 1;
            while (next_stop < len) {
                char nc = text[next_stop];
                if (isspace(nc)) { next_stop++; continue; }
                
                bool is_comment = false;
                for (int i=0; i<profile->line_comment_count; i++) 
                   if (starts_with(text + next_stop, profile->line_comments[i])) is_comment=true;
                for (int i=0; i<profile->block_comment_count; i++) 
                   if (starts_with(text + next_stop, profile->block_comments[i].start)) is_comment=true;
                if (is_comment) break;
                
                if (strchr(profile->string_delims, nc)) break;
                if (isdigit(nc)) break;
                if (isalnum(nc) || nc == '_') break;
                if (profile->enable_bracket_highlight && (is_punct(nc) || is_operator(nc) || nc == '?' || nc == ':')) break;
                
                next_stop++;
            }
            add_span(out, pos, next_stop, HL_NORMAL);
            pos = next_stop;

		} else if (state.state == HS_BLOCK_COMMENT) {
             int idx = state.sub_id;
             const char *end_str = profile->block_comments[idx].end;
             int end_len = strlen(end_str);
             int search = pos;
             bool found = false;
             while (search <= len - end_len) {
                 if (strncmp(text + search, end_str, end_len) == 0) {
                     found = true;
                     search += end_len;
                     break;
                 }
                 search++;
             }
             if (found) {
                 add_span(out, pos, search, HL_COMMENT);
                 pos = search;
                 state.state = HS_NORMAL;
             } else {
                 add_span(out, pos, len, HL_COMMENT);
                 pos = len;
             }
        } else if (state.state == HS_STRING) {
             char delim = (char)state.sub_id;
             int search = pos;
             bool found = false;
             while (search < len) {
                 if (text[search] == '\\') {
                     search += 2;
                     continue;
                 }
                 if (text[search] == delim) {
                     found = true;
                     search++;
                     break;
                 }
                 search++;
             }
             if (found) {
                 add_span(out, pos, search, HL_STRING);
                 pos = search;
                 state.state = HS_NORMAL;
             } else {
                 add_span(out, pos, len, HL_STRING);
                 pos = len;
             }
        } else if (state.state == HS_TRIPLE_STRING) {
             int search = pos;
             bool found = false;
             while (search <= len - 3) {
                 if (strncmp(text + search, "\"\"\"", 3) == 0) {
                     found = true;
                     search += 3;
                     break;
                 }
                 if (text[search] == '\\') search++;
                 search++;
             }
             if (found) {
                 add_span(out, pos, search, HL_STRING);
                 pos = search;
                 state.state = HS_NORMAL;
             } else {
                 add_span(out, pos, len, HL_STRING);
                 pos = len;
             }
        }
	}
	*end = state;
}
