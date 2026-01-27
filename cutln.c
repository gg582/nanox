/*
 * cutln.c
 *
 * CutLn functionality (F7 integration)
 */

#include <stdio.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "paste_slot.h"

/*
 * F7: CutLn Trigger
 * First press: Sets mark (Cut Start)
 * Second press: Kills region (Cut End)
 */
int cutln_trigger(int f, int n)
{
    if (cutln_active) {
        /* Second press: Perform Cut */
        int s = killregion(f, n);
        cutln_active = FALSE;
        return s;
    } else {
        /* First press: Set Mark */
        int s = setmark(f, n);
        if (s == TRUE) {
            cutln_active = TRUE;
            mlwrite("CutLn start set. Press F7 to cut, Shift+F7 to copy.");
        }
        return s;
    }
}

/*
 * Shift+F7: CutLn Copy
 * Only works if CutLn is active (start point set via F7)
 */
int cutln_copy(int f, int n)
{
    if (cutln_active) {
        /* Perform Copy */
        int s = copyregion(f, n);
        cutln_active = FALSE;
        return s;
    } else {
        mlwrite("No CutLn start set. Press F7 first.");
        return FALSE;
    }
}

/*
 * Ctrl+F7: Paste Menu
 * Activates the paste slot window (same as Ctrl+Shift+V logic)
 */
int cutln_paste_menu(int f, int n)
{
    paste_slot_set_active(1);
    paste_slot_display();
    return TRUE;
}
