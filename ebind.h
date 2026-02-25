/*  ebind.h
 *
 *  Initial default key to function bindings
 *
 *  Modified by Petri Kutvonen
 */

#ifndef EBIND_H_
#define EBIND_H_

#include "line.h"

/*
 * Command table.
 * This table  is *roughly* in ASCII order, left to right across the
 * characters of the command. This explains the funny location of the
 * control-X commands.
 */
struct key_tab keytab[NBINDS] = {
    /* Basic Navigation */
    { SPEC | 'A', backline },           /* Up Arrow */
    { SPEC | 'B', forwline },           /* Down Arrow */
    { SPEC | 'C', forwchar },           /* Right Arrow */
    { SPEC | 'D', backchar },           /* Left Arrow */
    { SPEC | 'H', gotobob },            /* Home */
    { SPEC | 'F', gotoeob },            /* End */
    { SPEC | '5', backpage },           /* Page Up */
    { SPEC | '6', forwpage },           /* Page Down */
    { SPEC | 'L', insspace },           /* Insert */
    { 0x7F, backdel },                  /* Backspace */
    { SPEC | 0x7F, forwdel },           /* Delete */
    { CONTROL | 'I', insert_tab },      /* Tab */
    { CONTROL | 'M', insert_newline },  /* Enter */

    /* SYSTEM */
    { SPEC | 'P', nanox_help_command },  /* F1 */
    { CONTROL | 'H', nanox_help_command },

    /* FILE CONTROL */
    { SPEC | 'Q', filesave },            /* F2 */
    { CONTROL | 'S', filesave },
    { SPEC | 'R', filefind },            /* F3 */
    { CONTROL | 'O', filefind },

    /* PROCESS */
    { SPEC | 'S', quit },                /* F4 */
    { CONTROL | 'Q', quit },

    /* DATA EXPLORATION & SEARCH */
    { SPEC | 'U', nanox_search_engine },  /* F5 */
    { CONTROL | 'F', nanox_search_engine },

    /* DATA DELETION (CUT) */
    { SPEC | 'X', cutln_trigger },        /* F7 */
    { CONTROL | 'X', cutln_trigger },
    { CONTROL | 'K', cutln_trigger },

    /* DATA INSERTION (PASTE) */
    { SPEC | 'Y', yank },                 /* F8 */
    { CONTROL | 'V', yank },
    { CONTROL | 'Y', yank },

    /* SLOT SWITCHING */
    { SPEC | '`', reserve_jump_1 },       /* F9 */
    { CONTROL | '1', reserve_jump_1 },
    { SPEC | 'a', reserve_jump_2 },       /* F10 */
    { CONTROL | '2', reserve_jump_2 },
    { SPEC | '{', reserve_jump_3 },       /* F11 */
    { CONTROL | '3', reserve_jump_3 },
    { SPEC | '}', reserve_jump_4 },       /* F12 */
    { CONTROL | '4', reserve_jump_4 },

    /* End of table */
    { 0, NULL }
};

#endif              /* EBIND_H_ */
