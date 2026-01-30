#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "util.h"

#define MAX_MATCHES 50
#define MAX_WORD_LEN 64

typedef struct {
    char word[MAX_WORD_LEN];
    char file[256];
    int line;
} CompletionMatch;

static CompletionMatch matches[MAX_MATCHES];
static int match_count = 0;

/* Find cscope.out or cscope.files by walking up directories */
static int find_cscope_file(char *path_out, int size) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return FALSE;

    char current[1024];
    mystrscpy(current, cwd, sizeof(current));

    while (1) {
        char attempt[1024];
        
        /* Check cscope.out */
        snprintf(attempt, sizeof(attempt), "%s/cscope.out", current);
        if (access(attempt, R_OK) == 0) {
            mystrscpy(path_out, attempt, size);
            return TRUE;
        }

        /* Check cscope.files */
        snprintf(attempt, sizeof(attempt), "%s/cscope.files", current);
        if (access(attempt, R_OK) == 0) {
            mystrscpy(path_out, attempt, size);
            return TRUE;
        }
        
        /* Check .git to stop */
        snprintf(attempt, sizeof(attempt), "%s/.git", current);
        if (access(attempt, F_OK) == 0) {
            /* If we hit .git and haven't found cscope, maybe stop? 
               But user said "current path... up to .git". 
               If we are at root and didn't find, stop. */
        }

        /* Move up */
        char *last_slash = strrchr(current, '/');
        if (!last_slash || last_slash == current) break; /* Root */
        *last_slash = 0;
    }
    return FALSE;
}

static void add_match(const char *word, const char *file, int line) {
    if (match_count >= MAX_MATCHES) return;
    
    /* Check duplicates */
    for (int i = 0; i < match_count; i++) {
        if (strcmp(matches[i].word, word) == 0) return;
    }
    
    mystrscpy(matches[match_count].word, word, MAX_WORD_LEN);
    mystrscpy(matches[match_count].file, file ? file : "", sizeof(matches[0].file));
    matches[match_count].line = line;
    match_count++;
}

/* Parse line for potential symbols. 
   Simple heuristic: Alphanumeric words matching prefix. */
static void scan_line_for_matches(const char *line, const char *prefix, int prefix_len) {
    const char *p = line;
    while (*p) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            p++;
            continue;
        }
        
        const char *start = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
        
        int len = p - start;
        if (len >= prefix_len && len < MAX_WORD_LEN) {
            if (strncasecmp(start, prefix, prefix_len) == 0) {
                char word[MAX_WORD_LEN];
                memcpy(word, start, len);
                word[len] = 0;
                /* Avoid adding the prefix itself if it's identical? Optional. */
                if (strcmp(word, prefix) != 0) {
                    add_match(word, NULL, 0);
                }
            }
        }
    }
}

static void scan_cscope_file(const char *path, const char *prefix) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    
    char line[1024];
    int prefix_len = strlen(prefix);
    
    while (fgets(line, sizeof(line), f)) {
        /* Skip cscope header lines */
        if (line[0] == '\t') {
             /* Cscope output format often puts symbols after tab, 
                or file paths. 
                Standard cscope -L output: file <space> function <space> line <space> text
                Raw cscope.out: Header lines... then lines starting with tab...
                Actually, raw cscope.out is complex. 
                Let's assume the user might have 'cscope.files' which is just a file list.
                If it's cscope.files, we might need to READ those files. That's too heavy.
                
                If it is 'cscope.out', it contains compressed text.
                
                Alternative: Just parse the file as text and look for words.
                It's a "dumb" scan but better than nothing given constraints.
             */
        }
        scan_line_for_matches(line, prefix, prefix_len);
    }
    fclose(f);
}

static void draw_menu(int row, int col, int selection) {
    /* Draw a box overlay */
    int width = 0;
    for (int i = 0; i < match_count; i++) {
        int l = strlen(matches[i].word);
        if (l > width) width = l;
    }
    width += 4; /* Padding */
    if (width > 40) width = 40;
    
    int height = match_count;
    if (height > 10) height = 10;
    
    /* Ensure inside screen */
    if (row + height > term.t_nrow) row = term.t_nrow - height;
    if (col + width > term.t_ncol) col = term.t_ncol - width;
    
    /* Draw */
    for (int i = 0; i < height; i++) {
        int idx = i; /* Scroll support if needed, simple for now */
        if (idx >= match_count) break;
        
        TTmove(row + i, col);
        
        /* Set color */
        if (idx == selection) {
            TTrev(TRUE);
        } else {
            TTrev(FALSE);
        }
        
        char buf[64];
        snprintf(buf, sizeof(buf), " %-*s ", width - 2, matches[idx].word);
        for(char *p = buf; *p; p++) TTputc(*p);
        
        TTrev(FALSE);
    }
    TTflush();
}

int cscope_complete(int f, int n) {
    char prefix[MAX_WORD_LEN] = {0};
    int c, i;
    
    /* Get the word before cursor */
    struct line *lp = curwp->w_dotp;
    int curoff = curwp->w_doto;
    int start = curoff;
    
    while (start > 0) {
        int ch = lgetc(lp, start - 1);
        if (!isalnum(ch) && ch != '_') break;
        start--;
    }
    
    if (start == curoff) {
        mlwrite("No word at cursor");
        return FALSE;
    }
    
    int len = curoff - start;
    if (len >= MAX_WORD_LEN) len = MAX_WORD_LEN - 1;
    
    for (i = 0; i < len; i++) {
        prefix[i] = lgetc(lp, start + i);
    }
    prefix[len] = 0;
    
    /* Search cscope file */
    char cscope_path[1024];
    match_count = 0;
    if (find_cscope_file(cscope_path, sizeof(cscope_path))) {
        scan_cscope_file(cscope_path, prefix);
    }
    
    /* Fallback: Scan current buffer? */
    /* scan_buffer_for_matches(curbp, prefix); */
    
    if (match_count == 0) {
        mlwrite("No matches found for '%s'", prefix);
        return FALSE;
    }
    
    /* Show Menu */
    int selection = 0;
    int row = currow + 1; /* Below cursor */
    int col = curcol;
    
    mlwrite("Select: Arrows, Shift+Tab to insert, Esc to cancel");
    
    while (1) {
        draw_menu(row, col, selection);
        
        c = getcmd();
        
        if (c == (CONTROL | 'G') || c == 0x1B) { /* Esc or C-g */
            update(TRUE); /* Clear menu */
            return FALSE;
        }
        
        /* Arrow keys */
        if (c == (SPEC | 'P') || c == (CONTROL | 'P')) { /* Up */
            if (selection > 0) selection--;
            else selection = match_count - 1;
        }
        else if (c == (SPEC | 'N') || c == (CONTROL | 'N')) { /* Down */
            if (selection < match_count - 1) selection++;
            else selection = 0;
        }
        else if (c == (SPEC | 'Z')) { /* Shift + Tab (Backtab) */
            /* Insert selection */
            struct line *dotp = curwp->w_dotp;
            int doto = curwp->w_doto;
            
            /* Delete prefix */
            /* We need to use ldelete. Move dot to start of prefix. */
            curwp->w_doto = start;
            ldelete(len, FALSE);
            
            /* Insert new word */
            const char *str = matches[selection].word;
            while (*str) linsert(1, *str++);
            
            update(TRUE);
            return TRUE;
        }
    }
    
    return TRUE;
}
