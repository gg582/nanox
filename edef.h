/* edef.h
 *
 * Global variable definitions.
 *
 * Written by Dave G. Conroy.
 * Modified by Petri Kutvonen
 */

typedef int (*fn_t)(int, int);

#ifdef    maindef

extern int ttgetc(void);
extern int ttputc(int);
extern void ttflush(void);

/* for MAIN.C */

/* initialized global definitions */

int flen = 0;                   /* current length of fline */
struct kill *kbufp = NULL;          /* current kill buffer chunk pointer    */
int fillcol = 72;        /* Current fill column          */
int kbdm[NKBDM];         /* Macro (Type changed to int)  */
char *execstr = NULL;        /* Pointer to string to execute */
char golabel[NPAT] = "";    /* current goto label            */
int execlevel = 0;        /* execution IF level            */
char *patmatch = NULL;        /* string that matched          */
int eolexist = TRUE;        /* does clear to EOL exist?     */
int revexist = FALSE;        /* does reverse video exist?    */
int flickcode = FALSE;        /* do flicker supression?       */
char *modename[] = {        /* mode names                   */
    "Wrap", "Cmode", "Spell", "Exact", "View", "Over",
    "Magic", "Crypt", "Asave"
};

char modecode[] = "WCSEVOMYA";    /* letters to represent modes   */
int numlocks = 0;        /* number of locks active       */
char *lname[NLOCKS];        /* names of locked files        */
int gflags = GFREAD;        /* global control flag          */
int rval = 0;            /* return value of a subprocess */
int overlap = 0;        /* overlap on next/prev page    */
int scrollcount = 1;        /* number of lines to scroll    */

void tcapkopen(void);
void tcapkclose(void);
void tcapmove(int, int);
void tcapeeol(void);
void tcapeeop(void);
void tcapbeep(void);
void tcaprev(int);
int tcapcres(char *);

void tcapopen(void);
void tcapclose(void);

/* uninitialized global definitions */

int currow;            /* Cursor row                    */
int curcol;            /* Cursor column                 */
int thisflag;            /* Flags, this command          */
int lastflag;            /* Flags, last command          */
int curgoal;            /* Goal for C-P, C-N            */
struct window *curwp;        /* Cursor window                */
struct buffer *curbp;        /* Cursor buffer                */
struct window *wheadp;        /* Head of list of windows      */
struct buffer *bheadp;        /* Head of list of buffers      */
struct buffer *blistp;        /* Buffer for C-X C-B           */
int *kbdptr;            /* current position in keyboard buf (int*) */
int *kbdend;            /* ptr to end of the keyboard (int*) */
int kbdmode;            /* current keyboard macro mode  */
int kbdrep;            /* number of times to execute   */
int restflag;            /* restricted use?              */
int lastkey;            /* last keystoke                */
int seed;            /* random number seed           */
long envram;            /* # of bytes current in use by malloc */
int macbug;            /* macro debug flag             */
int cmdstatus;            /* last command status          */
char pat[NPAT];            /* Search pattern               */
char tap[NPAT];            /* Reversed pattern array.      */
char rpat[NPAT];        /* replacement pattern          */
unsigned int matchlen;  /* Type changed to unsigned int */
unsigned int mlenold;   /* Type changed to unsigned int */
struct magic mcpat[NPAT];    /* parsed magic pattern         */
struct magic tapcm[NPAT];    /* parsed reversed magic pattern */
int discmd;            /* display command flag         */
int disinp;            /* display input events         */
int nullflag;            /* nulls allowed                */
extern struct terminal term; /* Terminal information.        */
int gasave;            /* global ASAVE size            */
int gacount;            /* global ASAVE count           */
int gmode;            /* global editor mode           */
char ttbuf[NSTRING];        /* buffer for tgetc             */
int ttrow;            /* Row location of HW cursor    */
int ttcol;            /* Column location of HW cursor */
int ttoff;            /* Offset of HW cursor          */
int taboff;            /* Tab offset for display       */
int vtrow;            /* Row location of virtual cursor */
int vtcol;            /* Column location of virtual cursor */
int lbound;            /* Left bound for horizontal scroll */
int sgarbf = FALSE;     /* Screen garbage flag (Duplicate fixed) */
int mpresf = FALSE;     /* Message present flag (Duplicate fixed) */
int gfcolor;                /* global forgrnd color (white) */
int gbcolor;                /* global backgrnd color (black) */
int quotec;                /* quote char during mlreply() */
int tab_width;                /* tabulator width */
int abortc;                /* abort char */
int metac;                /* meta char */
int ctlxc;                /* ctl-x char */
int reptc;                /* repeat char */
int justflag = FALSE;            /* justify flag */
int clexec = FALSE;            /* command line execution flag */
int boguss = FALSE;            /* Bogus flag */
int flowc = FALSE;            /* Flow control? */
int kleen = FALSE;            /* Clean? */
char errorm[] = "ERROR";            /* Error literal changed to array */
char truem[] = "TRUE";            /* True literal changed to array */
char falsem[] = "FALSE";            /* False literal changed to array */
struct kill *kbufh = NULL;        /* Kill buffer head */
int kused = 0;                /* Kill buffer used (long -> int) */
char sres[NSTRING] = "NORMAL";        /* Screen resolution */
char palstr[NSTRING] = "";        /* Palette string */
int saveflag = 0;            /* Temp store for lastflag */
int mstore = FALSE;            /* storing macro */
struct buffer *bstore = NULL;        /* buffer to store macro */
char *fline = NULL;            /* dynamic file reading buffer */

struct line *matchline = NULL;      /* match line pointer */
int matchoff = 0;                  /* match offset */

#else

/* for all the other .C files */

/* initialized global external declarations */

extern int fillcol;        /* Current fill column          */
extern int kbdm[];         /* Macro (short -> int)         */
extern char *execstr;        /* Pointer to string to execute */
extern char golabel[];        /* current goto label            */
extern int execlevel;        /* execution IF level            */
extern char *patmatch;        /* string that matched          */
extern int eolexist;        /* does clear to EOL exist?     */
extern int revexist;        /* does reverse video exist?    */
extern int flickcode;        /* do flicker supression?       */
extern char *modename[];    /* mode names                   */
extern char modecode[];        /* letters to represent modes   */
extern int numlocks;        /* number of locks active       */
extern char *lname[];        /* names of locked files        */
extern int gflags;        /* global control flag          */
extern int rval;        /* return value of a subprocess */
extern int overlap;        /* overlap on next/prev page    */
extern int scrollcount;        /* number of lines to scroll    */

/* uninitialized global external declarations */

extern int currow;        /* Cursor row                    */
extern int curcol;        /* Cursor column                 */
extern int thisflag;        /* Flags, this command          */
extern int lastflag;        /* Flags, last command          */
extern int curgoal;        /* Goal for C-P, C-N            */
extern struct window *curwp;    /* Cursor window                */
extern struct buffer *curbp;    /* Cursor buffer                */
extern struct window *wheadp;    /* Head of list of windows      */
extern struct buffer *bheadp;    /* Head of list of buffers      */
extern struct buffer *blistp;    /* Buffer for C-X C-B           */
extern int *kbdptr;        /* current position in keyboard buf (int*) */
extern int *kbdend;        /* ptr to end of the keyboard (int*) */
extern int kbdmode;        /* current keyboard macro mode  */
extern int kbdrep;        /* number of times to execute   */
extern int restflag;        /* restricted use?              */
extern int lastkey;        /* last keystoke                */
extern int seed;        /* random number seed           */
extern long envram;        /* # of bytes current in use by malloc */
extern int macbug;        /* macro debug flag             */
extern int cmdstatus;        /* last command status          */
extern char pat[];        /* Search pattern               */
extern char tap[];        /* Reversed pattern array.      */
extern char rpat[];        /* replacement pattern          */
extern unsigned int matchlen; /* int -> unsigned int */
extern unsigned int mlenold;  /* int -> unsigned int */
extern struct magic mcpat[];    /* parsed magic pattern         */
extern struct magic tapcm[];    /* parsed reversed magic pattern */
extern int discmd;        /* display command flag         */
extern int disinp;        /* display input events         */
extern int nullflag;        /* nulls allowed                */
extern int gasave;        /* global ASAVE size            */
extern int gacount;        /* global ASAVE count           */
extern int gmode;        /* global editor mode           */
extern char ttbuf[];        /* buffer for tgetc             */
extern int ttrow;        /* Row location of HW cursor    */
extern int ttcol;        /* Column location of HW cursor */
extern int ttoff;        /* Offset of HW cursor          */
extern int taboff;        /* Tab offset for display       */
extern int vtrow;        /* Row location of virtual cursor */
extern int vtcol;        /* Column location of virtual cursor */
extern int lbound;        /* Left bound for horizontal scroll */
extern int sgarbf;        /* Screen garbage flag */
extern int mpresf;        /* Message present flag */
extern int gfcolor;                /* global forgrnd color (white) */
extern int gbcolor;                /* global backgrnd color (black) */
extern int quotec;                /* quote char during mlreply() */
extern int tab_width;                /* tabulator width */
extern int abortc;                /* abort char */
extern int metac;                /* meta char */
extern int ctlxc;                /* ctl-x char */
extern int reptc;                /* repeat char */
extern int justflag;            /* justify flag */
extern int clexec;            /* command line execution flag */
extern int boguss;            /* Bogus flag */
extern int flowc;            /* Flow control? */
extern int kleen;            /* Clean? */
extern char errorm[];            /* Error literal changed to array */
extern char truem[];            /* True literal changed to array */
extern char falsem[];            /* False literal changed to array */
extern struct kill *kbufp;        /* Kill buffer pointer */
extern struct kill *kbufh;        /* Kill buffer head */
extern int kused;                /* Kill buffer used (long -> int) */
extern char sres[];            /* Screen resolution */
extern char palstr[];            /* Palette string */
extern int saveflag;            /* Temp store for lastflag */
extern int mstore;            /* storing macro */
extern struct buffer *bstore;        /* buffer to store macro */
extern char *fline;            /* dynamic file reading buffer */

extern struct line *matchline;
extern int matchoff;

#endif
