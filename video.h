#ifndef __VIDEO_H__
#define __VIDEO_H__
typedef struct {
    unicode_t ch;
    int fg;
    int bg;
    bool bold;
    bool underline;
} video_cell;

struct video {
    int v_flag;             /* Flags */
    video_cell v_text[1];           /* Screen data. */
};

#define VFCHG   0x0001              /* Changed flag                 */
#define VFEXT   0x0002              /* extended (beyond column 80)  */
#define VFREQ   0x0008              /* reverse video request        */
#define VFCOL   0x0010              /* color change requested       */
#endif
