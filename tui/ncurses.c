/*  ncurses.c
 *
 *  ncurses video driver for Nanox
 */

#define _XOPEN_SOURCE_EXTENDED 1
#include <curses.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "utf8.h"

#define MARGIN  8
#define SCRSIZ  64
#define NPAUSE  2
#define BEL     0x07

void ncurses_open(void);
void ncurses_close(void);
void ncurses_kopen(void);
void ncurses_kclose(void);
int ncurses_getchar(void);
int ncurses_putchar(int c);
void ncurses_flush(void);
void ncurses_move(int row, int col);
void ncurses_eeol(void);
void ncurses_eeop(void);
void ncurses_beep(void);
void ncurses_rev(int state);
void ncurses_italic(int state);
void ncurses_set_colors(int fg, int bg);
void ncurses_set_attrs(int bold, int underline, int italic);
int ncurses_cres(char *res);

struct terminal ncurses_term = {
    0, 0, 0, 0,
    MARGIN, SCRSIZ, NPAUSE,
    ncurses_open,
    ncurses_close,
    ncurses_kopen,
    ncurses_kclose,
    ncurses_getchar,
    ncurses_putchar,
    ncurses_flush,
    ncurses_move,
    ncurses_eeol,
    ncurses_eeop,
    ncurses_beep,
    ncurses_rev,
    ncurses_italic,
    ncurses_set_colors,
    ncurses_set_attrs,
    ncurses_cres,
};

static int current_pair = 0;
static attr_t current_attr = A_NORMAL;

static int is_true_color(int c) {
    return (c & 0xFF000000) == 0x01000000;
}

static int rgb_to_xterm256(int color) {
    int r = (color >> 16) & 0xFF;
    int g = (color >> 8) & 0xFF;
    int b = color & 0xFF;

    /* Grayscale ramp (232-255) */
    if (r == g && g == b) {
        if (r < 8) return 16;
        if (r > 248) return 231;
        return 232 + ((r - 8) * 24) / 247;
    }

    int rc = (r * 5) / 255;
    int gc = (g * 5) / 255;
    int bc = (b * 5) / 255;
    return 16 + (36 * rc) + (6 * gc) + bc;
}

/* Map color index to ncurses color */
static int map_color(int c) {
    if (c == -1) return -1;
    if (is_true_color(c))
        c = rgb_to_xterm256(c);
    if (c < 8) return c;
    if (c < 16) return c;
    if (c < COLORS) return c;
    return c % COLORS;
}

/* Pair management */
#define MAX_PAIRS 1024
static int next_pair = 1;
struct color_pair_entry {
    int fg, bg, pair;
} pair_table[MAX_PAIRS];

static int get_pair(int fg, int bg) {
    if (fg == -1 && bg == -1) return 0;
    
    for (int i = 0; i < next_pair - 1; i++) {
        if (pair_table[i].fg == fg && pair_table[i].bg == bg)
            return pair_table[i].pair;
    }
    if (next_pair < COLOR_PAIRS && next_pair < MAX_PAIRS) {
        init_pair(next_pair, map_color(fg), map_color(bg));
        pair_table[next_pair-1].fg = fg;
        pair_table[next_pair-1].bg = bg;
        pair_table[next_pair-1].pair = next_pair;
        return next_pair++;
    }
    return 0;
}

void ncurses_open(void) {
    ttopen();
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    nonl();
    keypad(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        use_default_colors();
    }
    ncurses_term.t_nrow = LINES - 1;
    ncurses_term.t_ncol = COLS;
    ncurses_term.t_mrow = MAXROW;
    ncurses_term.t_mcol = MAXCOL;
}

void ncurses_close(void) {
    endwin();
    ttclose();
}

void ncurses_kopen(void) {
}

void ncurses_kclose(void) {
}

int ncurses_getchar(void) {
    return ttgetc();
}

int ncurses_putchar(int c) {
    cchar_t wc;
    wchar_t wstr[2];
    wstr[0] = (wchar_t)c;
    wstr[1] = L'\0';
    
    if (setcchar(&wc, wstr, current_attr, current_pair, NULL) == OK) {
        add_wch(&wc);
    }
    return 0;
}

void ncurses_flush(void) {
    refresh();
}

void ncurses_move(int row, int col) {
    move(row, col);
}

void ncurses_eeol(void) {
    clrtoeol();
}

void ncurses_eeop(void) {
    clrtobot();
}

void ncurses_beep(void) {
    beep();
}

void ncurses_rev(int state) {
    if (state) current_attr |= A_REVERSE;
    else current_attr &= ~A_REVERSE;
    attr_set(current_attr, current_pair, NULL);
}

void ncurses_italic(int state) {
#ifdef A_ITALIC
    if (state) current_attr |= A_ITALIC;
    else current_attr &= ~A_ITALIC;
    attr_set(current_attr, current_pair, NULL);
#endif
}

void ncurses_set_colors(int fg, int bg) {
    if (!has_colors()) return;

    current_pair = get_pair(fg, bg);
    attr_set(current_attr, current_pair, NULL);
}

void ncurses_set_attrs(int bold, int underline, int italic) {
    if (bold) current_attr |= A_BOLD; else current_attr &= ~A_BOLD;
    if (underline) current_attr |= A_UNDERLINE; else current_attr &= ~A_UNDERLINE;
#ifdef A_ITALIC
    if (italic) current_attr |= A_ITALIC; else current_attr &= ~A_ITALIC;
#endif
    attr_set(current_attr, current_pair, NULL);
}

int ncurses_cres(char *res) {
    return TRUE;
}
