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
    { 0x7F, indent_cancel },            /* Backspace / Cancel Indent */
    { SPEC | 0x7F, forwdel },           /* Delete */
    { CONTROL | 'I', insert_tab },      /* Tab */
    { CONTROL | 'M', insert_newline },  /* Enter */
    { CONTROL | '@', completion_menu_command }, /* Ctrl+Space: Autocomplete */

    /* Indentation/Outdentation */
    { CONTROL | 'H', outdent_start_set },
    { CONTROL | SHIFT | 'H', outdent_end_set },
    { CONTROL | 'J', indent_start_set },
    { CONTROL | SHIFT | 'J', indent_end_set },
    { 'g', g_prefix_handler },
    { CONTROL | 'V', command_mode_activate_command }, /* Ctrl+V: Command Mode */

    /* SYSTEM */
    { SPEC | 'P', nanox_help_command },  /* F1 */

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
    { CONTROL | 'R', sed_replace_command },

    /* DATA DELETION & COPY (CUT/COPY) */
    { SPEC | 'W', cutln_start_copy },              /* F6: Start Copy */
    { SPEC | SHIFT | 'W', cutln_end_copy },        /* Shift+F6: End Copy */
    { CONTROL | 'W', cutln_start_copy },           /* Ctrl+W: Start Copy */
    { CONTROL | SHIFT | 'W', cutln_end_copy },      /* Ctrl+Shift+W: End Copy */
    
    { SPEC | 'X', cutln_start_cut },               /* F7: Start Cut */
    { SPEC | SHIFT | 'X', cutln_end_cut },         /* Shift+F7: End Cut */
    { CONTROL | 'X', cutln_start_cut },            /* Ctrl+X: Start Cut */
    { CONTROL | SHIFT | 'X', cutln_end_cut },      /* Ctrl+Shift+X: End Cut */
    
    { CONTROL | 'K', cutln_cut_current_line },     /* Ctrl+K: Cut Current Line */
    { CONTROL | SHIFT | 'K', cutln_end_cut },      /* Ctrl+Shift+K: End Cut */

    /* DATA INSERTION (PASTE) */
    { CONTROL | 'Y', yank },
    { SPEC | 'Y', yank },                /* F8 */
    { META | CONTROL | '8', yank },      /* Ctrl+Alt+8 */

    /* SLOT SWITCHING */
    { SPEC | '`', reserve_jump_1 },       /* F9 */
    { SPEC | 'a', reserve_jump_2 },       /* F10 */
    { SPEC | '{', reserve_jump_3 },       /* F11 */
    { SPEC | '}', reserve_jump_4 },       /* F12 */
    { META | CONTROL | '9', reserve_jump_fallback_1 },
    { META | CONTROL | '0', reserve_jump_fallback_2 },
    { META | CONTROL | '-', reserve_jump_fallback_3 },
    { META | CONTROL | '=', reserve_jump_fallback_4 },
    { META | CONTROL | '1', reserve_jump_numeric_mode },
    { META | CONTROL | '2', reserve_jump_numeric_mode },
    { META | CONTROL | '3', reserve_jump_numeric_mode },
    { META | CONTROL | '4', reserve_jump_numeric_mode },
    { META | CONTROL | '5', reserve_jump_numeric_mode },
    { META | CONTROL | '6', reserve_jump_numeric_mode },
    { META | CONTROL | '7', reserve_jump_numeric_mode },

    { CONTROL | 'G', gotoline },         /* Ctrl+G: Goto Line */

    /* End of table */
    { 0, NULL }
};

#endif              /* EBIND_H_ */
