/*  tcap.c
 *
 *  Unix V7 SysV and BS4 Termcap video driver
 *
 *  modified by Petri Kutvonen
 */

/*
 * Defining this to 1 breaks tcapopen() - it doesn't check if the
 * sceen size has changed.
 *  -lbt
 */

#include <curses.h>
#include <stdio.h>
#include <term.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"

#include <stdlib.h>
#include <string.h>

#define MARGIN  8
#define SCRSIZ  64
#define NPAUSE  2               /* Pause in 0.1 seconds */
#define BEL     0x07
#define ESC     0x1B

// void tcapkopen(void);
// void tcapkclose(void);
// void tcapmove(int, int);
// void tcapeeol(void);
// void tcapeeop(void);
// void tcapbeep(void);
// void tcaprev(int);
// int tcapcres(char *);
static void putpad(char *str);

// void tcapopen(void);
// void tcapclose(void);

#define TCAPSLEN 315
static char tcapbuf[TCAPSLEN];
static char *UP, PC, *CM, *CE, *CL, *SO, *SE, *ZH, *ZR;

static char *TI, *TE;

static void tcapitalic(int state);
static void tcap_set_colors(int fg, int bg);
static void tcap_set_attrs(int bold, int underline, int italic);

struct terminal tcap_term = {
    0,                  /* These four values are set dynamically at open time. */
    0,
    0,
    0,
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

void tcapopen(void)
{
    char *t, *p;
    char tcbuf[1024];
    char *tv_stype;
    char err_str[256];
    int int_col, int_row;

    if ((tv_stype = getenv("TERM")) == NULL) {
        puts("Environment variable TERM not defined!");
        exit(1);
    }

    if ((tgetent(tcbuf, tv_stype)) != 1) {
        snprintf(err_str, sizeof(err_str), "Unknown terminal type %s!", tv_stype);
        puts(err_str);
        exit(1);
    }

    /* Get screen size from system, or else from termcap.  */
    getscreensize(&int_col, &int_row);
    tcap_term.t_nrow = int_row - 1;
    tcap_term.t_ncol = int_col;

    if ((tcap_term.t_nrow <= 0)
        && (tcap_term.t_nrow = (short)tgetnum("li") - 1) == -1) {
        puts("termcap entry incomplete (lines)");
        exit(1);
    }

    if ((tcap_term.t_ncol <= 0)
        && (tcap_term.t_ncol = (short)tgetnum("co")) == -1) {
        puts("Termcap entry incomplete (columns)");
        exit(1);
    }
    tcap_term.t_mrow = MAXROW;
    tcap_term.t_mcol = MAXCOL;
    p = tcapbuf;
    t = tgetstr("pc", &p);
    if (t)
        PC = *t;
    else
        PC = 0;

    CL = tgetstr("cl", &p);
    CM = tgetstr("cm", &p);
    CE = tgetstr("ce", &p);
    UP = tgetstr("up", &p);
    SE = tgetstr("se", &p);
    SO = tgetstr("so", &p);
    ZH = tgetstr("ZH", &p);
    ZR = tgetstr("ZR", &p);
    if (SO != NULL)
        revexist = TRUE;

    if (tgetnum("sg") > 0) {        /* can reverse be used? P.K. */
        revexist = FALSE;
        SE = NULL;
        SO = NULL;
    }
    TI = tgetstr("ti", &p);         /* terminal init and exit */
    TE = tgetstr("te", &p);

    if (CL == NULL || CM == NULL || UP == NULL) {
        puts("Incomplete termcap entry\n");
        exit(1);
    }

    if (CE == NULL)             /* will we be able to use clear to EOL? */
        eolexist = FALSE;

    if (p >= &tcapbuf[TCAPSLEN]) {
        puts("Terminal description too big!\n");
        exit(1);
    }
    ttopen();
}

void tcapclose(void)
{
    putpad(tgoto(CM, 0, tcap_term.t_nrow));
    putpad(TE);
    ttflush();
    ttclose();
}

void tcapkopen(void)
{
    putpad(TI);
    ttflush();
    ttrow = 999;
    ttcol = 999;
    sgarbf = TRUE;
    strcpy(sres, "NORMAL");
}

void tcapkclose(void)
{
    putpad(TE);
    ttflush();
}

void tcapmove(int row, int col)
{
    putpad(tgoto(CM, col, row));
}

void tcapeeol(void)
{
    putpad(CE);
}

void tcapeeop(void)
{
    putpad(CL);
}

/*
 * Change reverse video status
 *
 * @state: FALSE = normal video, TRUE = reverse video.
 */
void tcaprev(int state)
{
    if (state) {
        if (SO != NULL)
            putpad(SO);
    } else if (SE != NULL)
        putpad(SE);
}

/*
 * Change italic video status
 *
 * @state: FALSE = normal video, TRUE = italic video.
 */
static void tcapitalic(int state)
{
    if (state) {
        if (ZH != NULL)
            putpad(ZH);
        else
            putpad("\033[3m");
    } else {
        if (ZR != NULL)
            putpad(ZR);
        else
            putpad("\033[23m");
    }
}

/* Change screen resolution. */
int tcapcres(char *res)
{
    return TRUE;
}

/* Change color status */
static void tcap_set_colors(int fg, int bg)
{
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

/* Change attribute status */
static void tcap_set_attrs(int bold, int underline, int italic)
{
    if (bold) { ttputc(ESC); ttputc('['); ttputc('1'); ttputc('m'); }
    else { ttputc(ESC); ttputc('['); ttputc('2'); ttputc('2'); ttputc('m'); }
    
    if (underline) { ttputc(ESC); ttputc('['); ttputc('4'); ttputc('m'); }
    else { ttputc(ESC); ttputc('['); ttputc('2'); ttputc('4'); ttputc('m'); }
    
    tcapitalic(italic);
}

void tcapbeep(void)
{
    ttputc(BEL);
}

static void putpad(char *str)
{
    tputs(str, 1, ttputc);
}
