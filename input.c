/*  input.c
 *
 *  Various input routines
 *
 *  written by Daniel Lawrence 5/9/86
 *  modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "wrapper.h"

#include "util.h"

extern struct name_bind names[];
extern struct terminal  term;

/*
 * Ask a yes or no question in the message line. Return either TRUE, FALSE, or
 * ABORT. The ABORT status is returned if the user bumps out of the question
 * with a ^G. Used any time a confirmation is required.
 */
int mlyesno(char *prompt)
{
    char c;                 /* input character */
    char buf[NPAT];             /* prompt to user */

    for (;;) {
        /* build and prompt the user */
        mystrscpy(buf, prompt, sizeof(buf));
        strcat(buf, " (y/n)? ");
        mlwrite(buf);

        /* get the responce */
        c = tgetc();

        if (c == 'y' || c == 'Y')
            return TRUE;

        if (c == 'n' || c == 'N')
            return FALSE;
    }
}

/*
 * Write a prompt into the message line, then read back a response. Keep
 * track of the physical position of the cursor. If we are in a keyboard
 * macro throw the prompt away, and return the remembered response. This
 * lets macros run at full speed. The reply is always terminated by a carriage
 * return. Handle erase, kill, and abort keys.
 */

int mlreply(char *prompt, char *buf, int nbuf)
{
  movecursor(term.t_nrow, 0);
  TTputc('\x1b'); TTputc('['); TTputc('K');
  write(1, "\033[K", 3);
    return nextarg(prompt, buf, nbuf, ctoec('\n'));
}

int mlreplyt(char *prompt, char *buf, int nbuf, int eolchar)
{
  movecursor(term.t_nrow, 0);
  write(1, "\033[K", 3);
    return nextarg(prompt, buf, nbuf, eolchar);
}

/*
 * ectoc:
 *  expanded character to character
 *  collapse the CONTROL and SPEC flags back into an ascii code
 */
int ectoc(int c)
{
    if (c & CONTROL)
        c = c & ~(CONTROL | 0x40);
    if (c & SPEC)
        c = c & 255;
    return c;
}

/*
 * ctoec:
 *  character to extended character
 *  pull out the CONTROL and SPEC prefixes (if possible)
 */
int ctoec(int c)
{
    if (c >= 0x00 && c <= 0x1F)
        c = CONTROL | (c + '@');
    return c;
}

/*
 * get a command name from the command line. Command completion means
 * that pressing a <SPACE> will attempt to complete an unfinished command
 * name if it is unique.
 */
fn_t getname(void)
{
    int cpos;               /* current column on screen output */
    int c;
    char *sp;               /* pointer to string for output */
    struct name_bind *ffp;          /* first ptr to entry in name binding table */
    struct name_bind *cffp;         /* current ptr to entry in name binding table */
    struct name_bind *lffp;         /* last ptr to entry in name binding table */
    char buf[NSTRING];          /* buffer to hold tentative command name */

    /* starting at the beginning of the string buffer */
    cpos = 0;

    /* if we are executing a command line get the next arg and match it */
    if (clexec) {
        if (macarg(buf) != TRUE)
            return NULL;
        return fncmatch(&buf[0]);
    }

    /* build a name string from the keyboard */
    while (TRUE) {
        c = tgetc();

        /* if we are at the end, just match it */
        if (c == 0x0d) {
            buf[cpos] = 0;

            /* and match it off */
            return fncmatch(&buf[0]);
        } else if (c == 0x7F || c == 0x08) {    /* rubout/erase */
            if (cpos != 0) {
                TTputc('\b');
                TTputc(' ');
                TTputc('\b');
                --ttcol;
                --cpos;
                TTflush();
            }

        } else if (c == 0x15) {     /* C-U, kill */
            while (cpos != 0) {
                TTputc('\b');
                TTputc(' ');
                TTputc('\b');
                --cpos;
                --ttcol;
            }

            TTflush();

        } else if (c == ' ' || c == 0x1b || c == 0x09) {
/* <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< */
            /* attempt a completion */
            buf[cpos] = 0;      /* terminate it for us */
            ffp = &names[0];    /* scan for matches */
            while (ffp->n_func != NULL) {
                if (strncmp(buf, ffp->n_name, strlen(buf))
                    == 0) {
                    /* a possible match! More than one? */
                    if ((ffp + 1)->n_func == NULL ||
                        (strncmp(buf, (ffp + 1)->n_name, strlen(buf)) != 0)) {
                        /* no...we match, print it */
                        sp = ffp->n_name + cpos;
                        while (*sp)
                            TTputc(*sp++);
                        TTflush();
                        return ffp->n_func;
                    } else {
/* << << << << << << << << << << << << << << << << << */
                        /* try for a partial match against the list */

                        /* first scan down until we no longer match the current input */
                        lffp = (ffp + 1);
                        while ((lffp + 1)->n_func != NULL) {
                            if (strncmp
                                (buf, (lffp + 1)->n_name, strlen(buf))
                                != 0)
                                break;
                            ++lffp;
                        }

                        /* and now, attempt to partial complete the string, char at a time */
                        while (TRUE) {
                            /* add the next char in */
                            buf[cpos] = ffp->n_name[cpos];

                            /* scan through the candidates */
                            cffp = ffp + 1;
                            while (cffp <= lffp) {
                                if (cffp->n_name[cpos]
                                    != buf[cpos])
                                    goto onward;
                                ++cffp;
                            }

                            /* add the character */
                            TTputc(buf[cpos++]);
                        }
/* << << << << << << << << << << << << << << << << << */
                    }
                }
                ++ffp;
            }

            /* no match.....beep and onward */
            TTbeep();
 onward:        ;
            TTflush();
/* <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< */
        } else {
            if (cpos < NSTRING - 1 && c > ' ') {
                buf[cpos++] = c;
                TTputc(c);
            }

            ++ttcol;
            TTflush();
        }
    }
}

/*  tgetc:  Get a key from the terminal driver, resolve any keyboard
        macro action                    */

int tgetc(void)
{
    int c;                  /* fetched character */

    /* if we are playing a keyboard macro back, */
    if (kbdmode == PLAY) {

        /* if there is some left... */
        if (kbdptr < kbdend)
            return (int)*kbdptr++;

        /* at the end of last repitition? */
        if (--kbdrep < 1) {
            kbdmode = STOP;
            /* force a screen update after all is done */
            update(FALSE);
        } else {

            /* reset the macro to the begining for the next rep */
            kbdptr = &kbdm[0];
            return (int)*kbdptr++;
        }
    }

    /* fetch a character from the terminal driver */
    c = TTgetc();

    /* record it for $lastkey */
    lastkey = c;

    /* save it if we need to */
    if (kbdmode == RECORD) {
        *kbdptr++ = c;
        kbdend = kbdptr;

        /* don't overrun the buffer */
        if (kbdptr == &kbdm[NKBDM - 1]) {
            kbdmode = STOP;
            TTbeep();
        }
    }

    /* and finally give the char back */
    return c;
}

/*  GET1KEY:    Get one keystroke. The only prefixs legal here
            are the SPEC and CONTROL prefixes.
                                */

int get1key(void)
{
    int c;

    /* get a keystroke */
    c = tgetc();

    if (c >= 0x00 && c <= 0x1F)     /* C0 control -> C-     */
        c = CONTROL | (c + '@');
    return c;
}

static int apply_modifier_bits(int modifier, int cmask)
{
    if (modifier == 3 || modifier == 4 || modifier == 7 || modifier == 8)
        cmask |= META;
    if (modifier == 5 || modifier == 6 || modifier == 7 || modifier == 8)
        cmask |= CONTROL;
    return cmask;
}

static int map_csi_function(int code)
{
    switch (code) {
    /* Navigation keys (CSI 1~ to CSI 6~) */
    case 1:
        return 'H';     /* Home */
    case 2:
        return 'L';     /* Insert */
    case 3:
        return 127;     /* Delete - map to DEL */
    case 4:
        return 'F';     /* End */
    case 5:
        return '5';     /* Page Up */
    case 6:
        return '6';     /* Page Down */
    /* VT100/xterm function keys F1-F4 (some terminals send CSI 11~ to 14~) */
    case 11:
        return 'P';     /* F1 */
    case 12:
        return 'Q';     /* F2 */
    case 13:
        return 'R';     /* F3 */
    case 14:
        return 'S';     /* F4 */
    /* Function keys F5-F12 (CSI 15~ to CSI 24~, with gaps) */
    case 15:
        return 'U';     /* F5 */
    case 17:
        return 'W';     /* F6 */
    case 18:
        return 'X';     /* F7 */
    case 19:
        return 'Y';     /* F8 */
    case 20:
        return '`';     /* F9 */
    case 21:
        return 'a';     /* F10 */
    case 23:
        return '{';     /* F11 */
    case 24:
        return '}';     /* F12 */
    default:
        return 0;
    }
}

static int decode_csi_sequence(int cmask)
{
    int params[3] = { 0, 0, 0 };
    int count = 0;
    int value = 0;
    int have_value = 0;
    int ch;

    while (1) {
        ch = get1key();
        if (ch >= '0' && ch <= '9') {
            value = value * 10 + (ch - '0');
            have_value = 1;
            continue;
        }
        if (ch == ';') {
            if (count < 3)
                params[count++] = have_value ? value : 0;
            value = 0;
            have_value = 0;
            continue;
        }
        break;
    }
    if (have_value && count < 3)
        params[count++] = value;

    if (ch == '~') {
        int modifier = 1;
        int spec;
        if (count == 0)
            return 0;
        if (count > 1)
            modifier = params[1];
        spec = map_csi_function(params[0]);
        if (!spec)
            return 0;
        return SPEC | spec | apply_modifier_bits(modifier, cmask);
    }

    if (ch >= 'A' && ch <= 'D') {
        int modifier = (count > 0) ? params[count - 1] : 1;
        return SPEC | ch | apply_modifier_bits(modifier, cmask);
    }

    if (ch >= 'E' && ch <= 'z' && ch != 'i' && ch != 'c') {
        int modifier = (count > 0) ? params[count - 1] : 1;
        if (ch == 'u') {
            int key = params[0];
            if (key < 128) {
                int code = key;
                if (code >= 'a' && code <= 'z')
                    code -= 0x20;
                return code | apply_modifier_bits(modifier, cmask);
            }
        }
        return SPEC | ch | apply_modifier_bits(modifier, cmask);
    }

    return 0;
}

/*  GETCMD: Get a command from the keyboard. Process all applicable
        prefix keys
                            */
int getcmd(void)
{

    int cmask = 0;
    int c;
    /* get initial character */
    c = get1key();

proc_metac:
    if (c == 128 + 27) {            /* CSI */
        int code = decode_csi_sequence(cmask);
        if (code)
            return code;
    }
    /* process META prefix (ESC key = ^[ in VT100) */
    if (c == (CONTROL | '[')) {
        /*
         * VT100 compatibility: Check if more characters are waiting.
         * If not, this is a standalone ESC key press (^[).
         * If characters are waiting, treat ESC as META prefix.
         * Note: typahead() returns 0 on ioctl failure, which safely
         * treats ESC as standalone in edge cases.
         */
        if (!typahead()) {
            /* Standalone ESC key - return as ^[ */
            return c;
        }
        c = get1key();
        if (c == '[') {
            int code = decode_csi_sequence(cmask);
            if (code)
                return code;
            c = get1key();
        }
        if (c == 'O') {
            int code = get1key();
            return SPEC | code | cmask;
        }
        if (c == (CONTROL | '[')) {
            cmask = META;
            goto proc_metac;
        }
        if (islower(c))         /* Force to upper */
            c ^= DIFCASE;
        if (c >= 0x00 && c <= 0x1F) /* control key */
            c = CONTROL | (c + '@');
        return META | c;
    } else if (c == metac) {
        c = get1key();
        if (c == (CONTROL | '[')) {
            cmask = META;
            goto proc_metac;
        }
        if (islower(c))         /* Force to upper */
            c ^= DIFCASE;
        if (c >= 0x00 && c <= 0x1F) /* control key */
            c = CONTROL | (c + '@');
        return META | c;
    }

    /* process CTLX prefix */
    if (c == ctlxc) {
        c = get1key();
        if (c == (CONTROL | '[')) {
            cmask = CTLX;
            goto proc_metac;
        }
        if (c >= 'a' && c <= 'z')   /* Force to upper */
            c -= 0x20;
        if (c >= 0x00 && c <= 0x1F) /* control key */
            c = CONTROL | (c + '@');
        return CTLX | c;
    }

    /* otherwise, just return it */
    return c;
}

/*  A more generalized prompt/reply function allowing the caller
    to specify the proper terminator. If the terminator is not
    a return ('\n') it will echo as "<NL>"
                            */
int getstring(char *prompt, char *buf, int nbuf, int eolchar)
{
    int cpos;               /* current character position in string */
    int c = 0;
    int c_int;
    unsigned char c_byte;
    int quotef;             /* are we quoting the next char? */
    int ffile, ocpos, nskip = 0, didtry = 0;

    static char tmp[] = "/tmp/meXXXXXX";
    FILE *tmpf = NULL;

    ffile = (strcmp(prompt, "Find file: ") == 0
         || strcmp(prompt, "View file: ") == 0
         || strcmp(prompt, "Insert file: ") == 0
         || strcmp(prompt, "Write file: ") == 0
         || strcmp(prompt, "Read file: ") == 0 || strcmp(prompt, "File to execute: ") == 0);

    cpos = 0;
    quotef = FALSE;

    /* prompt the user for the input string */
    mlwrite(prompt);

    for (;;) {
        if (!didtry)
            nskip = -1;
        didtry = 0;
        c_int = get1key();
        c_byte = (unsigned char)c_int;        /* If it is a <ret>, change it to a <NL> */
        if (c == (CONTROL | 0x4d) && !quotef)
            c = CONTROL | 0x40 | '\n';

        /* if they hit the line terminate, wrap it up */
        if (c == eolchar && quotef == FALSE) {
            buf[cpos++] = 0;

            /* clear the message line */
            mlwrite("");
            TTflush();

            /* if we default the buffer, return FALSE */
            if (buf[0] == 0)
                return FALSE;

            return TRUE;
        }

        /* change from command form back to character form */
        c = ectoc(c);

        if ((c == 0x7F || c == 0x08) && quotef == FALSE) {
            /* rubout/erase */
            if (cpos != 0) {
                /* Find the start of the character to delete */
                int start_pos = cpos;
                do {
                    start_pos--;
                } while (start_pos > 0 && !is_beginning_utf8((unsigned char)buf[start_pos]));
                
                /* Calculate width of the character */
                unicode_t uc;
                utf8_to_unicode((unsigned char*)buf, start_pos, cpos - start_pos, &uc);
                int width = unicode_width(uc);
                
                /* Backspace properly on screen */
                for (int i = 0; i < width; i++) {
                    outstring("\b \b");
                    --ttcol;
                }
                
                /* Adjust cpos to new end */
                cpos = start_pos;
                
                TTflush();
            }

        } else if (c == 0x15 && quotef == FALSE) {
            /* C-U, kill */
            while (cpos != 0) {
                /* Find the start of the character to delete */
                int start_pos = cpos;
                do {
                    start_pos--;
                } while (start_pos > 0 && !is_beginning_utf8((unsigned char)buf[start_pos]));
                
                /* Calculate width of the character */
                unicode_t uc;
                utf8_to_unicode((unsigned char*)buf, start_pos, cpos - start_pos, &uc);
                int width = unicode_width(uc);
                
                /* Backspace properly on screen */
                for (int i = 0; i < width; i++) {
                    outstring("\b \b");
                    --ttcol;
                }
                cpos = start_pos;
            }
            TTflush();

        } else if ((c == 0x09 || c == ' ') && quotef == FALSE && ffile) {
            /* TAB, complete file name */
            char ffbuf[255];
            int n, iswild = 0;

            didtry = 1;
            ocpos = cpos;
            while (cpos != 0) {
                outstring("\b \b");
                --ttcol;

                if (buf[--cpos] < 0x20) {
                    outstring("\b \b");
                    --ttcol;
                }
                if (buf[cpos] == '\n') {
                    outstring("\b\b  \b\b");
                    ttcol -= 2;
                }
                if (buf[cpos] == '*' || buf[cpos] == '?')
                    iswild = 1;
            }
            TTflush();
            if (nskip < 0) {
                buf[ocpos] = 0;
                if (tmpf != NULL)
                    fclose(tmpf);
                strcpy(tmp, "/tmp/meXXXXXX");
                strcpy(ffbuf, "echo ");
                strcat(ffbuf, buf);
                if (!iswild)
                    strcat(ffbuf, "*");
                strcat(ffbuf, " >");
                xmkstemp(tmp);
                strcat(ffbuf, tmp);
                strcat(ffbuf, " 2>&1");
                system(ffbuf);
                tmpf = fopen(tmp, "r");
                nskip = 0;
            }
            c = ' ';
            for (n = nskip; n > 0; n--)
                while ((c = getc(tmpf)) != EOF && c != ' ') ;
            nskip++;

            if (c != ' ') {
                TTbeep();
                nskip = 0;
            }

            while ((c = getc(tmpf)) != EOF && c != '\n' && c != ' ' && c != '*') {
                if (cpos < nbuf - 1)
                    buf[cpos++] = c;
            }

            if (c == '*')
                TTbeep();

            for (n = 0; n < cpos; n++) {
                c = buf[n];
                if ((c < ' ') && (c != '\n')) {
                    outstring("^");
                    ++ttcol;
                    c ^= 0x40;
                }

                if (c != '\n') {
                    if (disinp)
                        TTputc(c);
                } else {    /* put out <NL> for <ret> */
                    outstring("<NL>");
                    ttcol += 3;
                }
                ++ttcol;
            }
            TTflush();
            rewind(tmpf);
            unlink(tmp);

        } else if ((c == quotec || c == 0x16) && quotef == FALSE) {
            quotef = TRUE;
        } else { // Normal character input
            quotef = FALSE;
            if (cpos < nbuf - 1) {
                // Store the unsigned byte value directly.
                buf[cpos++] = c_byte;

                // Output logic adapted for unsigned char c_byte
                if (disinp) { // If input should be echoed
                    if (c_byte < 0x20 && c_byte != '\n') { // Control character representation (e.g., ^A)
                         outstring("^");
                         TTputc(c_byte + '@'); // Map 0x00-0x1F to 0x40-0x5F
                         ttcol += 2; // For '^' and the character
                    } else if (c_byte == '\n') { // Newline representation
                         outstring("<NL>");
                         ttcol += 3;
                    } else { // Printable ASCII or UTF-8 byte
                         // Fix double-encoding by writing raw byte
                         write(1, &c_byte, 1);
                         
                         // Update ttcol only if we completed a UTF-8 sequence
                         // Find start of current char
                         int start_pos = cpos - 1;
                         while (start_pos > 0 && !is_beginning_utf8((unsigned char)buf[start_pos]))
                             start_pos--;
                         
                         // Check if the sequence ending at cpos-1 is valid/complete
                         unicode_t uc;
                         unsigned len = utf8_to_unicode((unsigned char*)buf, start_pos, cpos - start_pos, &uc);
                         
                         // utf8_to_unicode returns 1 for invalid/partial if it can't decode, 
                         // or the length. 
                         // But we need to know if it *is* the full character.
                         // If cpos - start_pos == len, it implies we have the full bytes for that char.
                         
                         if ((cpos - start_pos) == len) {
                             ttcol += unicode_width(uc);
                         }
                    }
                }
            }
            TTflush();
        }
    }
}

/*
 * output a string of characters
 *
 * char *s;     string to output
 */
void outstring(char *s)
{
    if (disinp)
        while (*s)
            TTputc(*s++);
}

/*
 * output a string of output characters
 *
 * char *s;     string to output
 */
void ostring(char *s)
{
    if (discmd)
        while (*s)
            TTputc(*s++);
}
