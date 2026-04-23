#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "raw_sig.h"
#include "util.h"

#define MAX_SIG_BUF 1024

typedef struct {
    const char *name;
    const unsigned char *sig;
    int len;
} MagicNumber;

static const MagicNumber magic_numbers[] = {
    {"ELF", (const unsigned char *)"\x7F\x45\x4C\x46", 4},
    {"JPEG", (const unsigned char *)"\xFF\xD8\xFF", 3},
    {"PNG", (const unsigned char *)"\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8},
    {"GIF87a", (const unsigned char *)"GIF87a", 6},
    {"GIF89a", (const unsigned char *)"GIF89a", 6},
    {"ZIP", (const unsigned char *)"PK\x03\x04", 4},
    {"PDF", (const unsigned char *)"%PDF-", 5},
    {"EXE (MZ)", (const unsigned char *)"MZ", 2},
    {NULL, NULL, 0}
};

static long parse_hex(const char *s) {
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return strtol(s + 2, NULL, 16);
    return strtol(s, NULL, 16);
}

void command_mode_handle_raw_sig(const char *args) {
    char type[32];
    int n = 0;
    long offset = -1;
    int endian_le = 0;
    int bits_nr = 0;
    int align = 0;

    if (!args || *args == '\0') {
        mlwrite("Usage: file raw-sig <bytes|bits> <n> [offset <hex>] [endian be|le] [bits-nr <int>] [align <int>]");
        return;
    }

    char *copy = strdup(args);
    char *token = strtok(copy, " ");
    if (!token) { free(copy); return; }
    strncpy(type, token, sizeof(type)-1);
    
    token = strtok(NULL, " ");
    if (!token) {
        mlwrite("Missing count <n>");
        free(copy);
        return;
    }
    n = atoi(token);

    while ((token = strtok(NULL, " ")) != NULL) {
        if (strcasecmp(token, "offset") == 0) {
            token = strtok(NULL, " ");
            if (token) offset = parse_hex(token);
        } else if (strcasecmp(token, "endian") == 0) {
            token = strtok(NULL, " ");
            if (token && strcasecmp(token, "le") == 0) endian_le = 1;
        } else if (strcasecmp(token, "bits-nr") == 0) {
            token = strtok(NULL, " ");
            if (token) bits_nr = atoi(token);
        } else if (strcasecmp(token, "align") == 0) {
            token = strtok(NULL, " ");
            if (token) align = atoi(token);
        }
    }
    free(copy);

    if (curbp->b_fname[0] == '\0') {
        mlwrite("No file associated with buffer");
        return;
    }

    FILE *fp = fopen(curbp->b_fname, "rb");
    if (!fp) {
        mlwrite("Could not open file: %s", curbp->b_fname);
        return;
    }

    if (offset != -1) {
        fseek(fp, offset, SEEK_SET);
    } else {
        offset = 0;
    }

    char out[NSTRING];
    int pos = 0;

    if (strcasecmp(type, "bytes") == 0) {
        unsigned char data[MAX_SIG_BUF];
        if (n > MAX_SIG_BUF) n = MAX_SIG_BUF;
        if (n < 0) n = 0;
        int nread = (int)fread(data, 1, (size_t)n, fp);
        
        pos += snprintf(out + pos, (size_t)((int)sizeof(out) - pos), "[0x%04lX] ", offset);
        
        for (int i = 0; i < nread; i++) {
            pos += snprintf(out + pos, (size_t)((int)sizeof(out) - pos), "%02X ", data[i]);
            if (align > 0 && (i + 1) % align == 0 && i + 1 < nread) {
                pos += snprintf(out + pos, (size_t)((int)sizeof(out) - pos), "| ");
            }
        }
        
        if (pos < (int)sizeof(out) - 4) {
            strcat(out + pos, "| ");
            pos += 2;
        }

        for (int i = 0; i < nread; i++) {
            if (pos >= (int)sizeof(out) - 1) break;
            unsigned char c = data[i];
            if (c >= 0x20 && c <= 0x7E) out[pos++] = (char)c;
            else out[pos++] = '.';
        }
        out[pos] = '\0';

        const char *match = NULL;
        for (int i = 0; magic_numbers[i].name; i++) {
            if (nread >= magic_numbers[i].len && memcmp(data, magic_numbers[i].sig, (size_t)magic_numbers[i].len) == 0) {
                match = magic_numbers[i].name;
                break;
            }
        }
        if (match) {
            snprintf(out + pos, (size_t)((int)sizeof(out) - pos), " (Format: %s)", match);
        }
        mlwrite("%s", out);
    } else if (strcasecmp(type, "bits") == 0) {
        unsigned char data[MAX_SIG_BUF];
        int start_bit = bits_nr % 8;
        int skip_bytes = bits_nr / 8;
        if (skip_bytes > 0) fseek(fp, skip_bytes, SEEK_CUR);

        int bytes_to_read = (n + start_bit + 7) / 8;
        if (bytes_to_read > MAX_SIG_BUF) bytes_to_read = MAX_SIG_BUF;
        if (bytes_to_read < 0) bytes_to_read = 0;
        int nread = (int)fread(data, 1, (size_t)bytes_to_read, fp);

        pos += snprintf(out + pos, (size_t)((int)sizeof(out) - pos), "[0x%04lX] ", offset);
        if (bits_nr > 0 && n == 1) {
             pos = snprintf(out, sizeof(out), "[0x%04lX] bit[%d]: ", offset, bits_nr);
        }

        unsigned long long val = 0;
        int bits_left = n;
        int curr_byte = 0;
        int curr_bit = start_bit;

        while (bits_left > 0 && curr_byte < nread) {
            int bit;
            if (endian_le)
                bit = (data[curr_byte] >> curr_bit) & 1;
            else
                bit = (data[curr_byte] >> (7 - curr_bit)) & 1;

            if (pos < (int)sizeof(out) - 2) out[pos++] = bit ? '1' : '0';
            
            if (endian_le)
                val |= ((unsigned long long)bit << (n - bits_left));
            else
                val = (val << 1) | (unsigned long long)bit;

            curr_bit++;
            if (curr_bit == 8) {
                curr_bit = 0;
                curr_byte++;
                if (bits_left > 1 && pos < (int)sizeof(out) - 2) out[pos++] = ' ';
            }
            bits_left--;
        }
        out[pos] = '\0';
        
        if (n > 1) {
            snprintf(out + pos, (size_t)((int)sizeof(out) - pos), " (Dec: %llu)", val);
        }
        mlwrite("%s", out);
    } else {
        mlwrite("Unknown type: %s", type);
    }

    fclose(fp);
}
