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

    #define COMPRESS_LINES 128
    char *chunk_buf = malloc(1024 * 1024 * 16); // up to 16MB per 128 lines just in case
    if (!chunk_buf) { free(buffer); fclose(fp); s = FIOMEM; goto msg_out; }
    
    int chunk_lines = 0;
    size_t chunk_bytes = 0;
    size_t line_offsets[COMPRESS_LINES];
    size_t line_lengths[COMPRESS_LINES];
    size_t current_line_start = 0;

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, 128 * 1024, fp)) > 0) {
        size_t offset = 0;
        while (offset < bytes_read) {
            char c = buffer[offset++];
            if (c == '\n' || c == '\r') {
                if (offset < bytes_read) {
                    if ((c == '\r' && buffer[offset] == '\n') || (c == '\n' && buffer[offset] == '\r')) {
                        offset++;
                    }
                } else {
                    int next_c = fgetc(fp);
                    if ((c == '\r' && next_c == '\n') || (c == '\n' && next_c == '\r')) {
                        // consumed
                    } else if (next_c != EOF) {
                        ungetc(next_c, fp);
                    }
                }

                line_offsets[chunk_lines] = current_line_start;
                line_lengths[chunk_lines] = chunk_bytes - current_line_start;
                chunk_lines++;

                if (chunk_lines == COMPRESS_LINES) {
                    MemoryHandle hChunk = my_handle_alloc(chunk_bytes);
                    if (!hChunk) { s = FIOMEM; break; }
                    if (chunk_bytes > 0) memcpy(handle_deref(hChunk), chunk_buf, chunk_bytes);
                    
                    for (int i = 0; i < COMPRESS_LINES; i++) {
                        lp1 = (struct line *)malloc(sizeof(struct line));
                        if (!lp1) { s = FIOMEM; break; }
                        lp1->l_handle = hChunk;
                        my_handle_ref(hChunk);
                        lp1->l_offset = (uint32_t)line_offsets[i];
                        lp1->used = (uint16_t)line_lengths[i];
                        lp1->size = (uint16_t)line_lengths[i];

                        lp2 = lback(curbp->b_linep);
                        lp2->next = lp1;
                        lp1->next = curbp->b_linep;
                        lp1->prev = lp2;
                        curbp->b_linep->prev = lp1;
                        nline++;
                    }
                    if (s == FIOMEM) break;
                    
                    mymemory_freeze(hChunk);
                    my_handle_free(hChunk); // release initial alloc ref
                    
                    chunk_lines = 0;
                    chunk_bytes = 0;
                    current_line_start = 0;
                } else {
                    current_line_start = chunk_bytes;
                }
            } else {
                chunk_buf[chunk_bytes++] = c;
            }
        }
        if (s == FIOMEM) break;
    }
    
    if (s != FIOMEM && (chunk_bytes > current_line_start || chunk_lines > 0)) {
        if (chunk_bytes > current_line_start) {
            line_offsets[chunk_lines] = current_line_start;
            line_lengths[chunk_lines] = chunk_bytes - current_line_start;
            chunk_lines++;
        }
        if (chunk_lines > 0) {
            MemoryHandle hChunk = my_handle_alloc(chunk_bytes);
            if (!hChunk) { s = FIOMEM; }
            else {
                if (chunk_bytes > 0) memcpy(handle_deref(hChunk), chunk_buf, chunk_bytes);
                for (int i = 0; i < chunk_lines; i++) {
                    lp1 = (struct line *)malloc(sizeof(struct line));
                    if (!lp1) { s = FIOMEM; break; }
                    lp1->l_handle = hChunk;
                    my_handle_ref(hChunk);
                    lp1->l_offset = (uint32_t)line_offsets[i];
                    lp1->used = (uint16_t)line_lengths[i];
                    lp1->size = (uint16_t)line_lengths[i];

                    lp2 = lback(curbp->b_linep);
                    lp2->next = lp1;
                    lp1->next = curbp->b_linep;
                    lp1->prev = lp2;
                    curbp->b_linep->prev = lp1;
                    nline++;
                }
                mymemory_freeze(hChunk);
                my_handle_free(hChunk);
            }
        }
    }
    
    free(chunk_buf);
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

