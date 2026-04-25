/*  LOCK.C
 *
 *  File locking command routines
 *
 *  written by Daniel Lawrence
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "util.h"
#include "platform.h"

#include <sys/errno.h>

extern char *lname[NLOCKS];         /* names of all locked files */
extern int lowned[NLOCKS];          /* owned (TRUE) or overriden (FALSE) */
extern int numlocks;                /* # of current locks active */

/*
 * lockchk:
 *  check a file for locking and add it to the list
 *
 * char *fname;         file to check for a lock
 */
int lockchk(char *fname)
{
    int i;                  /* loop indexes */
    int status;             /* return status */

    /* check to see if that file is already locked here */
    if (numlocks > 0)
        for (i = 0; i < numlocks; ++i)
            if (strcmp(fname, lname[i]) == 0)
                return TRUE;

    /* if we have a full locking table, bitch and leave */
    if (numlocks == NLOCKS) {
        mlwrite("LOCK ERROR: Lock table full");
        return ABORT;
    }

    /* next, try to lock it */
    status = lock(fname);
    if (status == ABORT)            /* file is locked, no override */
        return ABORT;

    /* we have now locked or overriden it, add it to our table */
    lname[numlocks] = (char *)malloc(strlen(fname) + 1);
    if (lname[numlocks] == NULL) {  /* malloc failure */
        if (status == TRUE) undolock(fname);        /* free the lock if we own it */
        mlwrite("Cannot lock, out of memory");
        return ABORT;
    }

    /* everthing is cool, add it to the table */
    mystrscpy(lname[numlocks], fname, strlen(fname) + 1);
    lowned[numlocks] = (status == TRUE);
    numlocks++;
    return TRUE;
}

/*
 * lockrel:
 *  release all the file locks so others may edit
 */
int lockrel(void)
{
    int i;                  /* loop index */
    int status;             /* status of locks */
    int s;                  /* status of one unlock */
    int pcount = nanox_process_count();

    status = TRUE;
    if (numlocks > 0) {
        for (i = 0; i < numlocks; ++i) {
            /* 
             * Delete if we own it (standard behavior)
             * OR if we are the only nanox process (cleanup stale locks as requested)
             */
            if (lowned[i] || pcount <= 1) {
                if ((s = unlock(lname[i])) != TRUE)
                    status = s;
            }
            free(lname[i]);
        }
    }
    numlocks = 0;
    return status;
}

/*
 * lock:
 *  Check and lock a file from access by others
 *  returns TRUE = files was not locked and now is
 *      FALSE = file was locked and overridden
 *      ABORT = file was locked, abort command
 *
 * char *fname;     file name to lock
 */
int lock(char *fname)
{
    char *locker;               /* lock error message */
    int status;             /* return status */
    char msg[NSTRING];          /* message string */

    /* attempt to lock the file */
    locker = dolock(fname);
    if (locker == NULL)         /* we win */
        return TRUE;

    /* file failed...abort */
    if (strncmp(locker, "LOCK", 4) == 0) {
        lckerror(locker);
        return ABORT;
    }

    /* someone else has it....override? */
    snprintf(msg, sizeof(msg), "File in use by %s, override?", locker);
    status = mlyesno(msg);          /* ask them */
    if (status == TRUE)
        return FALSE;
    else
        return ABORT;
}

/*
 * unlock:
 *  Unlock a file
 *  this only warns the user if it fails
 *
 * char *fname;     file to unlock
 */
int unlock(char *fname)
{
    char *locker;               /* undolock return string */

    /* unclock and return */
    locker = undolock(fname);
    if (locker == NULL)
        return TRUE;

    /* report the error and come back */
    lckerror(locker);
    return FALSE;
}

/*
 * report a lock error
 *
 * char *errstr;    lock error string to print out
 */
void lckerror(char *errstr)
{
    char obuf[NSTRING];         /* output buffer for error message */

    snprintf(obuf, sizeof(obuf), "%s - %s", errstr, strerror(errno));
    mlwrite(obuf);
}
