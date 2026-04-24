int readin(char *fname, int lockfl)
{
    struct line *lp1;
    struct line *lp2;
    struct window *wp;
    struct buffer *bp;
    int s;
    int nline = 0;
    char mesg[NSTRING];

    if (lockfl && lockchk(fname) == ABORT) {
        s = FIOFNF;
        bp = curbp;
        strcpy(bp->b_fname, "");
        goto out;
    }
    bp = curbp;             /* Cheap.               */
    if ((s = bclear(bp)) != TRUE)       /* Might be old.        */
        return s;
    bp->b_flag &= ~(BFINVS | BFCHG);
    mystrscpy(bp->b_fname, fname, NFILEN);

    /* Set CMODE for brace-based languages */
    char *ext = strrchr(fname, '.');
    if (ext && (strcasecmp(ext, ".c") == 0 || strcasecmp(ext, ".h") == 0 ||
                strcasecmp(ext, ".cpp") == 0 || strcasecmp(ext, ".hpp") == 0 ||
                strcasecmp(ext, ".java") == 0 || strcasecmp(ext, ".js") == 0 ||
                strcasecmp(ext, ".ts") == 0 || strcasecmp(ext, ".rs") == 0 ||
                strcasecmp(ext, ".go") == 0 || strcasecmp(ext, ".php") == 0 ||
                strcasecmp(ext, ".swift") == 0)) {
        bp->b_mode |= MDCMOD;
    }

    /* Force hard tabs for Makefiles */
    const char *bname = strrchr(fname, '/');
    if (bname) bname++;
    else bname = fname;
    if (strcasecmp(bname, "makefile") == 0 || strncasecmp(bname, "makefile.", 9) == 0 ||
        (ext && (strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".markdown") == 0))) {
        bp->b_tabsize = 0;
        if (strcasecmp(bname, "makefile") == 0 || strncasecmp(bname, "makefile.", 9) == 0) {
            bp->b_flag |= BFMAKE;
        }
    }

    /* let a user macro get hold of things...if he wants */
    execute(META | SPEC | 'R', FALSE, 1);

    int use_swap = FALSE;
    char swapname[NFILEN];
    char *slash = strrchr(fname, '/');
    if (slash) {
        strncpy(swapname, fname, slash - fname + 1);
        swapname[slash - fname + 1] = '\0';
        strcat(swapname, ".");
        strcat(swapname, slash + 1);
        strcat(swapname, ".swp");
    } else {
        strcpy(swapname, ".");
        strcat(swapname, fname);
        strcat(swapname, ".swp");
    }

    FILE *swp_chk = fopen(swapname, "r");
    if (swp_chk) {
        fclose(swp_chk);
        int ans = mlyesno("Swap file found. Restore unsaved changes");
        if (ans == TRUE) {
            use_swap = TRUE;
        } else {
            unlink(swapname);
        }
    }

    FILE *fp = fopen(use_swap ? swapname : fname, "rb");
    if (!fp) {
        if (!use_swap) {
            mlwrite("(New file)");
            s = FIOFNF;
        } else {
            nanox_set_lamp(NANOX_LAMP_ERROR);
            s = FIOERR;
        }
        goto out;
    }

    mlwrite("(Reading file...)");
    
    char *buffer = malloc(128 * 1024);
    if (!buffer) { fclose(fp); s = FIOMEM; goto msg_out; }

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, 128 * 1024, fp)) > 0) {
        MemoryHandle hChunk = my_handle_alloc(bytes_read);
        if (!hChunk) { s = FIOMEM; break; }
        memcpy(handle_deref(hChunk), buffer, bytes_read);
        
        if (nline > 100) mymemory_freeze(hChunk);

        size_t offset = 0;
        while (offset < bytes_read) {
            size_t line_end = offset;
            while (line_end < bytes_read && buffer[line_end] != '\n' && buffer[line_end] != '\r') {
                line_end++;
            }

            int used = (int)(line_end - offset);
            lp1 = (struct line *)malloc(sizeof(struct line));
            if (!lp1) { s = FIOMEM; break; }

            lp1->l_handle = hChunk;
            lp1->l_offset = (uint32_t)offset;
            lp1->used = (uint16_t)used;
            lp1->size = (uint16_t)used;

            lp2 = lback(curbp->b_linep);
            lp2->next = lp1;
            lp1->next = curbp->b_linep;
            lp1->prev = lp2;
            curbp->b_linep->prev = lp1;

            nline++;
            offset = line_end;
            if (offset < bytes_read && buffer[offset] == '\r') offset++;
            if (offset < bytes_read && buffer[offset] == '\n') {
                if (offset - 1 >= 0 && buffer[offset - 1] == '\r') {
                   /* it was \r\n, skip \n */
                   offset++;
                } else {
                   /* it was just \n, skip it */
                   offset++;
                }
            }
        }
        if (s == FIOMEM) break;
    }
    
    free(buffer);
    fclose(fp);

    if (use_swap) {
        bp->b_flag |= BFCHG;
        unlink(swapname);
    }
    s = FIOSUC;

 msg_out:
    strcpy(mesg, "(");
    if (s == FIOERR) {
        strcat(mesg, "I/O ERROR, ");
        curbp->b_flag |= BFTRUNC;
    }
    if (s == FIOMEM) {
        strcat(mesg, "OUT OF MEMORY, ");
        curbp->b_flag |= BFTRUNC;
    }
    sprintf(&mesg[strlen(mesg)], "Read %d line", nline);
    if (nline != 1)
        strcat(mesg, "s");
    strcat(mesg, ")");
    mlwrite(mesg);
    if (s == FIOERR || s == FIOMEM)
        nanox_set_lamp(NANOX_LAMP_ERROR);
    else
        nanox_set_lamp(NANOX_LAMP_OFF);

 out:
    wp = curwp;
    if (wp->w_bufp == curbp) {
        wp->w_linep = lforw(curbp->b_linep);
        wp->w_dotp = lforw(curbp->b_linep);
        wp->w_doto = 0;
        wp->w_markp = NULL;
        wp->w_marko = 0;
        wp->w_flag |= WFMODE | WFHARD;
    }
    if (s == FIOERR || s == FIOFNF)     /* False if error.      */
        return FALSE;
    return TRUE;
}

