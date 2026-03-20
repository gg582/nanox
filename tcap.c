/* tcap.c
 *
 * Unix V7 SysV and BS4 Termcap video driver
 * Modified for standalone fallback (No ncurses dependency)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Note: Since we are using fallbacks, we don't strictly need curses.h/term.h
 * but keep them if other parts of the project require their definitions.
 */
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

#define MARGIN  8
#define SCRSIZ  64
#define NPAUSE  2
#define BEL     0x07
#define ESC     0x1B

/* Internal Buffer for Termcap Data */
static const char *current_tcap_ptr = NULL;
#define TCAPSLEN 315
static char tcapbuf[TCAPSLEN];
static char *UP, PC, *CM, *CE, *CL, *SO, *SE, *ZH, *ZR;
static char *TI, *TE;

/* Forward Declarations */
static void putpad(char *str);
static void tcapitalic(int state);
static void tcap_set_colors(int fg, int bg);
static void tcap_set_attrs(int bold, int underline, int italic);

/* Terminal Structure Definition */
struct terminal tcap_term = {
    0, 0, 0, 0, /* Set dynamically at open time */
    MARGIN,
    SCRSIZ,
    NPAUSE,
    tcapopen,
    tcapclose,
    tcapkopen,
    tcapkclose,
    ttgetc,
    ttputc,
    ttflush,
    tcapmove,
    tcapeeol,
    tcapeeop,
    tcapbeep,
    tcaprev,
    tcapitalic,
    tcap_set_colors,
    tcap_set_attrs,
    tcapcres,
};

/* --- Fallback Implementations Start --- */

/**
 * tgetent - Loads terminal entry.
 * If system termcap is missing, provides a default ANSI entry.
 */
int tgetent(char *bp, const char *name) {
    /* Default ANSI termcap string */
    static const char *default_ansi =
        "ansi:co#80:li#24:am:cl=\\E[H\\E[J:cm=\\E[%i%d;%dH:nd=\\E[C:up=\\E[A:ce=\\E[K:ho=\\E[H:so=\\E[7m:se=\\E[m:";

    if (!bp) return -1;

    /* Force use our default ANSI string for maximum compatibility */
    strcpy(bp, default_ansi);
    current_tcap_ptr = bp;

    return 1; /* Success */
}

/**
 * tgetnum - Retrieves numeric capabilities (co, li).
 */
int tgetnum(const char *id) {
    if (!current_tcap_ptr || !id) return -1;
    const char *ptr = current_tcap_ptr;
    size_t id_len = strlen(id);

    while (*ptr) {
        ptr = strchr(ptr, ':');
        if (!ptr) break;
        ptr++;
        if (strncmp(ptr, id, id_len) == 0 && ptr[id_len] == '#') {
            return atoi(ptr + id_len + 1);
        }
    }
    return -1;
}

/**
 * tgetstr - Retrieves string capabilities (cl, cm, etc.).
 * Handles \E escape sequences.
 */
char *tgetstr(const char *id, char **area) {
    if (!current_tcap_ptr || !id) return NULL;
    const char *ptr = current_tcap_ptr;
    size_t id_len = strlen(id);

    while (*ptr) {
        ptr = strchr(ptr, ':');
        if (!ptr) break;
        ptr++;
        if (strncmp(ptr, id, id_len) == 0 && ptr[id_len] == '=') {
            char *ret = *area;
            const char *src = ptr + id_len + 1;
            while (*src && *src != ':') {
                if (*src == '\\' && *(src + 1) == 'E') {
                    *(*area)++ = ESC;
                    src += 2;
                } else {
                    *(*area)++ = *src++;
                }
            }
            *(*area)++ = '\0';
            return ret;
        }
    }
    return NULL;
}

/**
 * tgoto - Decodes cursor motion for ANSI terminals.
 */
char *tgoto(const char *cap, int col, int row) {
    static char buf[64];
    /* Simplified for ANSI standard: row and col are 1-based in ANSI */
    sprintf(buf, "\033[%d;%dH", row + 1, col + 1);
    return buf;
}

/**
 * tputs - Outputs a string to the terminal using the provided function.
 */
int tputs(const char *str, int affcnt, int (*putc_func)(int)) {
    if (!str) return 0;
    while (*str) {
        putc_func((unsigned char)*str++);
    }
    return 0;
}

static void putpad(char *str) {
    tputs(str, 1, ttputc);
}

/* --- Fallback Implementations End --- */

void tcapopen(void) {
    char *t, *p;
    char tcbuf[1024];
    char *tv_stype;
    int int_col, int_row;

    if ((tv_stype = getenv("TERM")) == NULL) {
        tv_stype = "ansi"; /* Default to ansi if not set */
    }

    /* Load our fallback termcap entry */
    tgetent(tcbuf, tv_stype);

    /* Get screen size from system or termcap */
    getscreensize(&int_col, &int_row);

    /* If system call fails, use tgetnum fallback */
    if (int_row <= 0) int_row = tgetnum("li");
    if (int_col <= 0) int_col = tgetnum("co");

    /* Final safety defaults */
    if (int_row <= 0) int_row = 24;
    if (int_col <= 0) int_col = 80;

    tcap_term.t_nrow = int_row - 1;
    tcap_term.t_ncol = int_col;
    tcap_term.t_mrow = MAXROW;
    tcap_term.t_mcol = MAXCOL;

    p = tcapbuf;
    t = tgetstr("pc", &p);
    PC = t ? *t : 0;

    CL = tgetstr("cl", &p);
    CM = tgetstr("cm", &p);
    CE = tgetstr("ce", &p);
    UP = tgetstr("up", &p);
    SE = tgetstr("se", &p);
    SO = tgetstr("so", &p);
    ZH = tgetstr("ZH", &p);
    ZR = tgetstr("ZR", &p);

    if (SO != NULL) revexist = TRUE;
    if (tgetnum("sg") > 0) {
        revexist = FALSE;
        SE = SO = NULL;
    }

    TI = tgetstr("ti", &p);
    TE = tgetstr("te", &p);

    /* Critical capability check */
    if (CL == NULL || CM == NULL || UP == NULL) {
        puts("Incomplete internal termcap configuration!\n");
        exit(1);
    }

    if (CE == NULL) eolexist = FALSE;
    if (p >= &tcapbuf[TCAPSLEN]) {
        puts("Terminal description too big!\n");
        exit(1);
    }
    ttopen();
}

void tcapclose(void) {
    putpad(tgoto(CM, 0, tcap_term.t_nrow));
    putpad(TE);
    ttflush();
    ttclose();
}

void tcapkopen(void) {
    putpad(TI);
    ttflush();
    ttrow = 999;
    ttcol = 999;
    sgarbf = TRUE;
    strcpy(sres, "NORMAL");
}

void tcapkclose(void) {
    putpad(TE);
    ttflush();
}

void tcapmove(int row, int col) {
    putpad(tgoto(CM, col, row));
}

void tcapeeol(void) {
    putpad(CE);
}

void tcapeeop(void) {
    putpad(CL);
}

void tcaprev(int state) {
    if (state) {
        if (SO != NULL) putpad(SO);
    } else if (SE != NULL) {
        putpad(SE);
    }
}

static void tcapitalic(int state) {
    if (state) {
        if (ZH != NULL) putpad(ZH);
        else putpad("\033[3m");
    } else {
        if (ZR != NULL) putpad(ZR);
        else putpad("\033[23m");
    }
}

int tcapcres(char *res) {
    return TRUE;
}

static void tcap_set_colors(int fg, int bg) {
    char buf[64];
    if (fg == -1) {
        ttputc(ESC); ttputc('['); ttputc('3'); ttputc('9'); ttputc('m');
    } else if (fg & 0x01000000) {
        snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm",
            (fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF);
        putpad(buf);
    } else if (fg >= 8 && fg < 16) {
        snprintf(buf, sizeof(buf), "\033[%dm", 90 + (fg - 8));
        putpad(buf);
    } else {
        snprintf(buf, sizeof(buf), "\033[%dm", 30 + fg);
        putpad(buf);
    }

    if (bg == -1) {
        ttputc(ESC); ttputc('['); ttputc('4'); ttputc('9'); ttputc('m');
    } else if (bg & 0x01000000) {
        snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm",
            (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
        putpad(buf);
    } else if (bg >= 8 && bg < 16) {
        snprintf(buf, sizeof(buf), "\033[%dm", 100 + (bg - 8));
        putpad(buf);
    } else {
        snprintf(buf, sizeof(buf), "\033[%dm", 40 + bg);
        putpad(buf);
    }
}

static void tcap_set_attrs(int bold, int underline, int italic) {
    if (bold) { ttputc(ESC); ttputc('['); ttputc('1'); ttputc('m'); }
    else { ttputc(ESC); ttputc('['); ttputc('2'); ttputc('2'); ttputc('m'); }

    if (underline) { ttputc(ESC); ttputc('['); ttputc('4'); ttputc('m'); }
    else { ttputc(ESC); ttputc('['); ttputc('2'); ttputc('4'); ttputc('m'); }

    tcapitalic(italic);
}

void tcapbeep(void) {
    ttputc(BEL);
}
