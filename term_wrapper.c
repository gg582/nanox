#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include "estruct.h"
#include "edef.h"

extern struct terminal *term;
extern struct terminal tcap_term;
#ifdef USE_NCURSES
extern struct terminal ncurses_term;
#endif

static int driver_failed = 0;

/* Helper to check if terminal is responding to ANSI queries */
static int terminal_is_sane(void) {
    /* Send a cursor position request */
    write(1, "\033[6n", 4);
    
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    
    /* Give it 50ms to respond */
    tv.tv_sec = 0;
    tv.tv_usec = 50000;
    
    int retval = select(1, &rfds, NULL, NULL, &tv);
    if (retval > 0) {
        /* Terminal responded! Consume the response. */
        char buf[32];
        read(0, buf, sizeof(buf));
        return 1;
    }
    return 0;
}

void vttopen(void) {
    if (term->t_open) term->t_open();

    /* Validation loop with timeout */
    int retries = 3;
    while (retries-- > 0) {
        if (term->t_nrow > 0 && term->t_ncol > 0) {
            /* Basic sanity check: dimensions must be positive */
            break;
        }
        
        /* Wait a bit and try to get screen size again */
        usleep(10000); /* 10ms */
        
        if (term->t_rez) term->t_rez(NULL);
    }
    
    if (term->t_nrow <= 0 || term->t_ncol <= 0) {
        /* Dimensions still invalid. Fallback. */
        driver_failed = 1;
    }

    if (driver_failed) {
        /* If current term failed, try falling back to tcap */
        if (term->t_close) term->t_close();
        term = &tcap_term;
        if (term->t_open) term->t_open();
    }
}

void vttclose(void) {
    if (term->t_close) term->t_close();
}

void vttkopen(void) {
    if (term->t_kopen) term->t_kopen();
}

void vttkclose(void) {
    if (term->t_kclose) term->t_kclose();
}

int vttgetc(void) {
    if (term->t_getchar) return term->t_getchar();
    return 0;
}

int vttputc(int c) {
    if (term->t_putchar) return term->t_putchar(c);
    return 0;
}

void vttflush(void) {
    if (term->t_flush) term->t_flush();
}

void vttmove(int row, int col) {
    if (term->t_move) term->t_move(row, col);
}

void vtteeol(void) {
    if (term->t_eeol) term->t_eeol();
}

void vtteeop(void) {
    if (term->t_eeop) term->t_eeop();
}

void vttbeep(void) {
    /* Beep disabled by request */
}

void vttrev(int state) {
    if (term->t_rev) term->t_rev(state);
}

void vttitalic(int state) {
    if (term->t_italic) term->t_italic(state);
}

void vttsetcolors(int fg, int bg) {
    if (term->t_set_colors) term->t_set_colors(fg, bg);
}

void vttsetattrs(int bold, int underline, int italic) {
    if (term->t_set_attrs) term->t_set_attrs(bold, underline, italic);
}

int vttrez(char *res) {
    if (term->t_rez) return term->t_rez(res);
    return TRUE;
}
