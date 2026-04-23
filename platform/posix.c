/*  posix.c
 *
 *      The functions in this file negotiate with the operating system for
 *      characters, and write characters in a barely buffered fashion on the
 *      display. All operating systems.
 *
 *  modified by Petri Kutvonen
 *
 *  based on termio.c, with all the old cruft removed, and
 *  fixed for termios rather than the old termio.. Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "utf8.h"
#include "paste_slot.h"

extern struct terminal *term;

/* Forward declaration */
static int handle_bracketed_paste(void);

static int kbdflgs;             /* saved keyboard fd flags      */
static int kbdpoll;             /* in O_NDELAY mode             */
static int tty_is_raw = FALSE;

static struct termios otermios;         /* original terminal characteristics */
static struct termios ntermios;         /* charactoristics to use inside */

#define TBUFSIZ 128
static char tobuf[TBUFSIZ];         /* terminal output buffer */

static void ttrestore_terminal(void)
{
    if (!tty_is_raw)
        return;

    /* Disable bracketed paste mode before restoring cooked terminal state. */
    write(1, "\033[?2004l", 8);
    tcsetattr(0, TCSADRAIN, &otermios);
    tty_is_raw = FALSE;
}

static void ttfatal_exit(int status)
{
    ttrestore_terminal();
    _exit(status);
}

/*
 * This function is called once to set up the terminal device streams.
 * On VMS, it translates TT until it finds the terminal, then assigns
 * a channel to it and sets it raw. On CPM it is a no-op.
 */
void ttopen(void)
{
    tcgetattr(0, &otermios);        /* save old settings */

    /*
     * base new settings on old ones - don't change things
     * we don't know about
     */
    ntermios = otermios;

    /* Fully raw UTF-8-safe input handling */
    ntermios.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP
                  | INLCR | IGNCR | ICRNL | IXON | IXOFF);

    /* raw CR/NR etc output handling */
    ntermios.c_oflag &= ~(OPOST | ONLCR | OLCUC | OCRNL | ONOCR | ONLRET);

    /* No signal handling, no echo etc */
    ntermios.c_lflag &= ~(ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK
                  | ECHONL | NOFLSH | TOSTOP | ECHOCTL |
                  ECHOPRT | ECHOKE | FLUSHO | PENDIN | IEXTEN);

    /* one character, no timeout */
    ntermios.c_cc[VMIN] = 1;
    ntermios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSADRAIN, &ntermios); /* and activate them */
    tty_is_raw = TRUE;
    atexit(ttrestore_terminal);

    /*
     * provide a smaller terminal output buffer so that
     * the type ahead detection works better (more often)
     */
    setbuffer(stdout, &tobuf[0], TBUFSIZ);

    kbdflgs = fcntl(0, F_GETFL, 0);
    kbdpoll = FALSE;

    /* on all screens we are not sure of the initial position
       of the cursor                                        */
    ttrow = 999;
    ttcol = 999;

    /* Enable bracketed paste mode for paste slot window */
    write(1, "\033[?2004h", 8);
}

/*
 * This function gets called just before we go back home to the command
 * interpreter. On VMS it puts the terminal back in a reasonable state.
 * Another no-operation on CPM.
 */
void ttclose(void)
{
    ttrestore_terminal();
}

/*
 * Write a character to the display. On VMS, terminal output is buffered, and
 * we just put the characters in the big array, after checking for overflow.
 * On CPM terminal I/O unbuffered, so we just write the byte out. Ditto on
 * MS-DOS (use the very very raw console output routine).
 */
int ttputc(int c)
{
    char utf8[6];
    int bytes;

    bytes = unicode_to_utf8(c, utf8);
    fwrite(utf8, 1, bytes, stdout);
    return 0;
}

/*
 * Flush terminal buffer. Does real work where the terminal output is buffered
 * up. A no-operation on systems where byte at a time terminal I/O is done.
 */
void ttflush(void)
{
/*
 * Add some terminal output success checking, sometimes an orphaned
 * process may be left looping on SunOS 4.1.
 *
 * How to recover here, or is it best just to exit and lose
 * everything?
 *
 * jph, 8-Oct-1993
 * Jani Jaakkola suggested using select after EAGAIN but let's just wait a bit
 *
 */
    int status;

    status = fflush(stdout);
    while (status < 0 && errno == EAGAIN) {
        sleep(1);
        status = fflush(stdout);
    }
    if (status < 0)
        ttfatal_exit(15);
}

/*
 * Small tty input buffer
 */
static struct {
    int nr;
    char buf[32];
} TT;

/* Pause for x*.1 second lag or until keypress */
static void pause_read(int pause)
{
    int n;

    ntermios.c_cc[VMIN] = 0;
    ntermios.c_cc[VTIME] = pause;
    tcsetattr(0, TCSANOW, &ntermios);

    n = read(0, TT.buf + TT.nr, sizeof(TT.buf) - TT.nr);

    /* Undo timeout */
    ntermios.c_cc[VMIN] = 1;
    ntermios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &ntermios);

    if (n > 0)
        TT.nr += n;
}

void ttpause(void)
{
    if (term->t_pause && !TT.nr)
        pause_read(term->t_pause);
}

/*
 * Read a character from the terminal, performing no editing and doing no echo
 * at all. More complex in VMS that almost anyplace else, which figures. Very
 * simple on CPM, because the system can do exactly what you want.
 */
int ttgetc(void)
{
    unicode_t c;
    int count, bytes = 1, expected;

    count = TT.nr;
    if (!count) {
        count = read(0, TT.buf, sizeof(TT.buf));
        if (count <= 0)
            return 0;
        TT.nr = count;
    }

    /* Check for bracketed paste mode */
    if (handle_bracketed_paste())
        return 0;  /* Paste was handled, return dummy value */

    c = (unsigned char)TT.buf[0];
    if (c != 27 && c < 128)
        goto done;

    /*
     * Lazy. We don't bother calculating the exact
     * expected length. We want at least two characters
     * for the special character case (ESC+[) and for
     * the normal short UTF8 sequence that starts with
     * the 110xxxxx pattern.
     *
     * But if we have any of the other patterns, just
     * try to get more characters. At worst, that will
     * just result in a barely perceptible 0.1 second
     * delay for some *very* unusual utf8 character
     * input.
     */
    expected = 2;
    if ((c & 0xe0) == 0xe0)
        expected = 6;

    /* Special character - try to re-fill the buffer */
    if (count < expected)
        pause_read(1);

    if (c == 27 && TT.nr > 1) {
        unsigned char second = TT.buf[1];

        /* If we have an escape sequence (ESC [ or ESC O), try to read the full sequence */
        if (second == '[' || second == 'O') {
            int timeout = 3; /* Wait up to 0.3s for the rest of the sequence */
            while (timeout--) {
                unsigned char last = TT.buf[TT.nr - 1];
                /* CSI sequences (ESC [ ...) end with a character in 0x40-0x7E range */
                if (second == '[' && TT.nr >= 3 && last >= 0x40 && last <= 0x7E)
                    break;
                /* SS3 sequences (ESC O ...) are typically 3 bytes long */
                if (second == 'O' && TT.nr >= 3)
                    break;
                pause_read(1);
            }

            if (second == '[' || (second == 'O' && TT.nr >= 3)) {
                /* Turn ESC+'[' or ESC+'O' into CSI (Single byte 155) */
                bytes = 2;
                c = 128 + 27;
                goto done;
            }
            /* For ESC O without a third byte, we return ESC and leave O in the buffer.
               getcmd() will handle this because typahead() will be true. */
        }
    }
    bytes = utf8_to_unicode(TT.buf, 0, TT.nr, &c);

    /* Hackety hack! Turn no-break space into regular space */
    if (c == 0xa0)
        c = ' ';
 done:
    TT.nr -= bytes;
    memmove(TT.buf, TT.buf + bytes, TT.nr);
    return c;
}

/* typahead:    Check to see if any characters are already in the
        keyboard TT.buf
*/

int typahead(void)
{
    int x;

    if (ioctl(0, FIONREAD, &x) < 0)
        x = 0;
    return x + TT.nr;
}

/*
 * Handle bracketed paste mode - redirects to paste slot window
 * Returns TRUE if bracketed paste was handled, FALSE otherwise
 */
int handle_bracketed_paste(void)
{
    extern void paste_slot_init(void);
    extern int paste_slot_add_char(char c);
    extern void paste_slot_clear(void);
    extern void paste_slot_set_active(int active);
    extern void paste_slot_display(void);
    
    /* Check if we have enough bytes for the paste start sequence */
    if (TT.nr < 6)
        return 0;
    
    /* Check for ESC[200~ (paste start) */
    if (TT.buf[0] != 27 || TT.buf[1] != '[' || 
        TT.buf[2] != '2' || TT.buf[3] != '0' || 
        TT.buf[4] != '0' || TT.buf[5] != '~')
        return 0;
    
    /* Consume the paste start sequence */
    TT.nr -= 6;
    memmove(TT.buf, TT.buf + 6, TT.nr);
    
    /* Initialize and clear paste slot */
    paste_slot_init();
    paste_slot_clear();
    
    /* Read characters until we see ESC[201~ (paste end) */
    int paste_active = 1;
    while (paste_active) {
        /* Ensure we have data - non-blocking read with timeout */
        if (TT.nr == 0) {
            /* Set non-blocking mode */
            fcntl(0, F_SETFL, kbdflgs | O_NONBLOCK);
            
            /* Try to read with small delay */
            struct timespec ts = {0, 10000000};  /* 10ms */
            nanosleep(&ts, NULL);
            
            int count = read(0, TT.buf + TT.nr, sizeof(TT.buf) - TT.nr);
            
            /* Restore blocking mode */
            fcntl(0, F_SETFL, kbdflgs);
            
            if (count <= 0) {
                /* No more data, assume paste ended */
                paste_active = 0;
                break;
            }
            TT.nr += count;
        }
        
        if (TT.nr == 0)
            break;
        
        /* Check for paste end sequence */
        if (TT.nr >= 6 && TT.buf[0] == 27 && TT.buf[1] == '[' &&
            TT.buf[2] == '2' && TT.buf[3] == '0' && 
            TT.buf[4] == '1' && TT.buf[5] == '~') {
            /* Consume the paste end sequence */
            TT.nr -= 6;
            memmove(TT.buf, TT.buf + 6, TT.nr);
            paste_active = 0;
            break;
        }
        
        /* Read one byte and add to paste slot buffer */
        unsigned char c = TT.buf[0];
        TT.nr--;
        memmove(TT.buf, TT.buf + 1, TT.nr);
        
        paste_slot_add_char(c);
    }
    
    /* Activate paste slot window */
    paste_slot_set_active(1);
    paste_slot_display();
    
    return 1;
}
