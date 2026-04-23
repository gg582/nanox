#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "colors_engine.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "util.h"

#define FORMAT_HEX3 1
#define FORMAT_HEX6 2
#define FORMAT_0X6  3
#define FORMAT_RGB  4

typedef struct {
    double r, g, b;
    int format;
} ColorData;

static void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l) {
    r /= 255.0; g /= 255.0; b /= 255.0;
    double max = fmax(fmax(r, g), b);
    double min = fmin(fmin(r, g), b);
    *l = (max + min) / 2.0;

    if (max == min) {
        *h = *s = 0.0;
    } else {
        double d = max - min;
        *s = (*l > 0.5) ? d / (2.0 - max - min) : d / (max + min);
        if (max == r) {
            *h = (g - b) / d + (g < b ? 6.0 : 0.0);
        } else if (max == g) {
            *h = (b - r) / d + 2.0;
        } else {
            *h = (r - g) / d + 4.0;
        }
        *h *= 60.0;
    }
}

static double hue_to_rgb(double p, double q, double t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}

static void hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b) {
    if (s == 0.0) {
        *r = *g = *b = l;
    } else {
        double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
        double p = 2.0 * l - q;
        h /= 360.0;
        *r = hue_to_rgb(p, q, h + 1.0/3.0);
        *g = hue_to_rgb(p, q, h);
        *b = hue_to_rgb(p, q, h - 1.0/3.0);
    }
    *r *= 255.0; *g *= 255.0; *b *= 255.0;
}

static void clamp_color(ColorData *c) {
    if (c->r < 0) c->r = 0; 
    if (c->r > 255) c->r = 255;
    if (c->g < 0) c->g = 0; 
    if (c->g > 255) c->g = 255;
    if (c->b < 0) c->b = 0; 
    if (c->b > 255) c->b = 255;
}

static void apply_colorblind(ColorData *c, const char *type) {
    double r = c->r, g = c->g, b = c->b;
    double nr = r, ng = g, nb = b;

    if (strcmp(type, "r") == 0 || strcmp(type, "r-weak") == 0) {
        nr = 0.56667 * r + 0.43333 * g + 0.0 * b;
        ng = 0.55833 * r + 0.44167 * g + 0.0 * b;
        nb = 0.0 * r + 0.24167 * g + 0.75833 * b;
        if (strcmp(type, "r-weak") == 0) {
            nr = (r + nr)/2.0; ng = (g + ng)/2.0; nb = (b + nb)/2.0;
        }
    } else if (strcmp(type, "g") == 0 || strcmp(type, "g-weak") == 0) {
        nr = 0.625 * r + 0.375 * g + 0.0 * b;
        ng = 0.70 * r + 0.30 * g + 0.0 * b;
        nb = 0.0 * r + 0.30 * g + 0.70 * b;
        if (strcmp(type, "g-weak") == 0) {
            nr = (r + nr)/2.0; ng = (g + ng)/2.0; nb = (b + nb)/2.0;
        }
    } else if (strcmp(type, "b") == 0 || strcmp(type, "b-weak") == 0) {
        nr = 0.95 * r + 0.05 * g + 0.0 * b;
        ng = 0.0 * r + 0.43333 * g + 0.56667 * b;
        nb = 0.0 * r + 0.475 * g + 0.525 * b;
        if (strcmp(type, "b-weak") == 0) {
            nr = (r + nr)/2.0; ng = (g + ng)/2.0; nb = (b + nb)/2.0;
        }
    } else if (strcmp(type, "whole") == 0 || strncmp(type, "whole-weak", 10) == 0) {
        double lum = 0.299 * r + 0.587 * g + 0.114 * b;
        nr = lum; ng = lum; nb = lum;
        if (strncmp(type, "whole-weak", 10) == 0) {
            nr = (r + nr)/2.0; ng = (g + ng)/2.0; nb = (b + nb)/2.0;
        }
    }
    c->r = nr; c->g = ng; c->b = nb;
}

static void execute_op(ColorData *c, const char *op) {
    while (*op && isspace((unsigned char)*op)) op++;
    if (!*op) return;

    if (strncmp(op, "swap", 4) == 0) {
        // Simple heuristic: just swap r/g/b based on presence
        // Better: parse "r,g,b b,g,r"
        const char *args = op + 4;
        while (*args && isspace((unsigned char)*args)) args++;
        // parse target channels (simple version: just look at the second block)
        const char *space = strchr(args, ' ');
        if (space) {
            const char *dst = space;
            while (*dst && isspace((unsigned char)*dst)) dst++;
            double old_r = c->r, old_g = c->g, old_b = c->b;
            double *ptrs[3] = { &c->r, &c->g, &c->b };
            int p_idx = 0;
            for (int i = 0; dst[i] && p_idx < 3; i++) {
                if (dst[i] == 'r') *ptrs[p_idx++] = old_r;
                else if (dst[i] == 'g') *ptrs[p_idx++] = old_g;
                else if (dst[i] == 'b') *ptrs[p_idx++] = old_b;
            }
        }
    } else if (strncmp(op, "adjust-hue", 10) == 0) {
        double deg = atof(op + 10);
        double h, s, l;
        rgb_to_hsl(c->r, c->g, c->b, &h, &s, &l);
        h = fmod(h + deg, 360.0);
        if (h < 0) h += 360.0;
        hsl_to_rgb(h, s, l, &c->r, &c->g, &c->b);
    } else if (strncmp(op, "adjust-contrast", 15) == 0) {
        double pct = atof(op + 15) / 100.0;
        c->r = (c->r/255.0 - 0.5) * (1.0 + pct) + 0.5;
        c->g = (c->g/255.0 - 0.5) * (1.0 + pct) + 0.5;
        c->b = (c->b/255.0 - 0.5) * (1.0 + pct) + 0.5;
        c->r *= 255.0; c->g *= 255.0; c->b *= 255.0;
    } else if (strncmp(op, "invert absolute", 15) == 0) {
        c->r = 255.0 - c->r;
        c->g = 255.0 - c->g;
        c->b = 255.0 - c->b;
    } else if (strncmp(op, "invert relative", 15) == 0) {
        double lum = 0.299 * c->r + 0.587 * c->g + 0.114 * c->b;
        c->r = 2.0 * lum - c->r;
        c->g = 2.0 * lum - c->g;
        c->b = 2.0 * lum - c->b;
    } else if (strncmp(op, "grayscale", 9) == 0) {
        double lum = 0.299 * c->r + 0.587 * c->g + 0.114 * c->b;
        c->r = c->g = c->b = lum;
    } else if (strncmp(op, "bit-depth 565", 13) == 0) {
        c->r = floor(c->r * 31.0 / 255.0) * 255.0 / 31.0;
        c->g = floor(c->g * 63.0 / 255.0) * 255.0 / 63.0;
        c->b = floor(c->b * 31.0 / 255.0) * 255.0 / 31.0;
        c->format = 5; // special format flag to output 0xXXXX
    } else if (strncmp(op, "colorblind", 10) == 0) {
        const char *type = op + 10;
        while (*type && isspace((unsigned char)*type)) type++;
        apply_colorblind(c, type);
    }
    clamp_color(c);
}

typedef struct {
    int start;
    int len;
    ColorData c;
} Match;

static int parse_hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int match_colors(const char *text, int len, Match *matches, int max_matches) {
    int count = 0;
    int i = 0;
    while (i < len && count < max_matches) {
        if (text[i] == '#' && i + 3 <= len) {
            int h1 = parse_hex(text[i+1]);
            int h2 = parse_hex(text[i+2]);
            int h3 = parse_hex(text[i+3]);
            if (h1 >= 0 && h2 >= 0 && h3 >= 0) {
                if (i + 6 <= len) {
                    int h4 = parse_hex(text[i+4]);
                    int h5 = parse_hex(text[i+5]);
                    int h6 = parse_hex(text[i+6]);
                    if (h4 >= 0 && h5 >= 0 && h6 >= 0) {
                        // #RRGGBB
                        matches[count].start = i;
                        matches[count].len = 7;
                        matches[count].c.format = FORMAT_HEX6;
                        matches[count].c.r = (h1 << 4) | h2;
                        matches[count].c.g = (h3 << 4) | h4;
                        matches[count].c.b = (h5 << 4) | h6;
                        count++;
                        i += 7;
                        continue;
                    }
                }
                // #RGB
                matches[count].start = i;
                matches[count].len = 4;
                matches[count].c.format = FORMAT_HEX3;
                matches[count].c.r = (h1 << 4) | h1;
                matches[count].c.g = (h2 << 4) | h2;
                matches[count].c.b = (h3 << 4) | h3;
                count++;
                i += 4;
                continue;
            }
        } else if (text[i] == '0' && i + 8 <= len && (text[i+1] == 'x' || text[i+1] == 'X')) {
            int h1 = parse_hex(text[i+2]); int h2 = parse_hex(text[i+3]);
            int h3 = parse_hex(text[i+4]); int h4 = parse_hex(text[i+5]);
            int h5 = parse_hex(text[i+6]); int h6 = parse_hex(text[i+7]);
            if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0 && h5 >= 0 && h6 >= 0) {
                matches[count].start = i;
                matches[count].len = 8;
                matches[count].c.format = FORMAT_0X6;
                matches[count].c.r = (h1 << 4) | h2;
                matches[count].c.g = (h3 << 4) | h4;
                matches[count].c.b = (h5 << 4) | h6;
                count++;
                i += 8;
                continue;
            }
        } else if (i + 10 <= len && strncmp(&text[i], "rgb(", 4) == 0) {
            int j = i + 4;
            while (j < len && isspace((unsigned char)text[j])) j++;
            int r = 0; while (j < len && isdigit((unsigned char)text[j])) r = r*10 + (text[j++] - '0');
            while (j < len && isspace((unsigned char)text[j])) j++;
            if (j < len && text[j] == ',') {
                j++;
                while (j < len && isspace((unsigned char)text[j])) j++;
                int g = 0; while (j < len && isdigit((unsigned char)text[j])) g = g*10 + (text[j++] - '0');
                while (j < len && isspace((unsigned char)text[j])) j++;
                if (j < len && text[j] == ',') {
                    j++;
                    while (j < len && isspace((unsigned char)text[j])) j++;
                    int b = 0; while (j < len && isdigit((unsigned char)text[j])) b = b*10 + (text[j++] - '0');
                    while (j < len && isspace((unsigned char)text[j])) j++;
                    if (j < len && text[j] == ')') {
                        matches[count].start = i;
                        matches[count].len = j - i + 1;
                        matches[count].c.format = FORMAT_RGB;
                        matches[count].c.r = r > 255 ? 255 : r;
                        matches[count].c.g = g > 255 ? 255 : g;
                        matches[count].c.b = b > 255 ? 255 : b;
                        count++;
                        i = j + 1;
                        continue;
                    }
                }
            }
        }
        i++;
    }
    return count;
}

int command_mode_handle_colors_command(const char *input) {
    struct region rp;
    int has_region = getregion(&rp) == TRUE;
    
    struct line *lp;
    int remaining = 0;
    int start_offset = 0;

    if (has_region) {
        lp = rp.r_linep;
        start_offset = rp.r_offset;
        remaining = rp.r_size;
    } else {
        lp = curwp->w_dotp;
        remaining = llength(lp);
    }

    int match_count_total = 0;

    while (remaining > 0 && lp != curbp->b_linep) {
        int line_len = llength(lp);
        int proc_len = line_len - start_offset;
        if (has_region) {
            if (remaining < proc_len) proc_len = remaining;
            remaining -= (proc_len + 1); // +1 for newline
        } else {
            remaining = 0;
        }

        Match matches[32];
        // We pass the string starting at start_offset
        int mcount = match_colors((char*)lp->text + start_offset, proc_len, matches, 32);

        if (mcount > 0) {
            match_count_total += mcount;
            char *new_text = malloc(line_len + 512); // generous padding
            if (!new_text) break;
            int out_idx = 0;
            int in_idx = 0;
            
            // copy before start_offset
            while (in_idx < start_offset) {
                new_text[out_idx++] = lp->text[in_idx++];
            }

            for (int m = 0; m < mcount; m++) {
                // matches[m].start is relative to start_offset!
                int match_abs_start = start_offset + matches[m].start;

                // copy before this match
                while (in_idx < match_abs_start) {
                    new_text[out_idx++] = lp->text[in_idx++];
                }
                
                // apply ops
                ColorData c = matches[m].c;
                char *cmd_dup = strdup(input);
                char *tok = strtok(cmd_dup, ";");
                while (tok) {
                    execute_op(&c, tok);
                    tok = strtok(NULL, ";");
                }
                free(cmd_dup);

                // format output
                int ir = (int)round(c.r);
                int ig = (int)round(c.g);
                int ib = (int)round(c.b);
                
                char outbuf[64];
                if (c.format == 5) { // 565
                    int r5 = ir * 31 / 255;
                    int g6 = ig * 63 / 255;
                    int b5 = ib * 31 / 255;
                    int v565 = (r5 << 11) | (g6 << 5) | b5;
                    snprintf(outbuf, sizeof(outbuf), "0x%04X", v565);
                } else if (c.format == FORMAT_HEX3) {
                    snprintf(outbuf, sizeof(outbuf), "#%01X%01X%01X", ir >> 4, ig >> 4, ib >> 4);
                } else if (c.format == FORMAT_HEX6) {
                    snprintf(outbuf, sizeof(outbuf), "#%02X%02X%02X", ir, ig, ib);
                } else if (c.format == FORMAT_0X6) {
                    snprintf(outbuf, sizeof(outbuf), "0x%06X", (ir<<16)|(ig<<8)|ib);
                } else if (c.format == FORMAT_RGB) {
                    snprintf(outbuf, sizeof(outbuf), "rgb(%d, %d, %d)", ir, ig, ib);
                }

                int olen = strlen(outbuf);
                memcpy(new_text + out_idx, outbuf, olen);
                out_idx += olen;
                in_idx += matches[m].len;
            }
            // copy rest
            while (in_idx < line_len) {
                new_text[out_idx++] = lp->text[in_idx++];
            }
            new_text[out_idx] = '\0';

            // replace line
            curwp->w_dotp = lp;
            curwp->w_doto = 0;
            ldelete(line_len, FALSE);
            linsert_block(new_text, out_idx);
            free(new_text);
            
            lp = curwp->w_dotp; 
        }
        
        lp = lforw(lp);
        start_offset = 0; // next line starts at offset 0
    }

    if (match_count_total > 0) {
        mlwrite("Colors modified: %d", match_count_total);
    } else {
        mlwrite("No colors found.");
    }
    
    curwp->w_flag |= WFHARD;
    return TRUE;
}
