# Hooks for Nanox Integration

To integrate the syntax highlighting engine into Nanox (uEmacs), follow these steps.

## 1. Line Structure (`line.h`)

Add highlighting state to `struct line` to support incremental updates.

```c
/* In line.h */
#include "highlight.h" /* Add this include */

struct line {
    struct line *l_fp;
    struct line *l_bp;
    int l_size;
    int l_used;
    HighlightState hl_start_state; /* Add this */
    HighlightState hl_end_state;   /* Add this */
    char l_text[1];
};
```

Since `lalloc` in `line.c` allocates `sizeof(struct line) + size`, this change is automatically handled for new lines. You may need to initialize `hl_start_state` to `HS_NORMAL` in `lalloc`.

## 2. Display Structures (`display.c`)

Modify `struct video` to store color information for each character.

```c
/* In display.c */

/* Define a cell structure */
typedef struct {
    unicode_t char_code;
    int color_attr; /* 0-7 foreground, flags for bold etc */
} video_cell;

struct video {
    int v_flag;
    video_cell v_text[1]; /* Replace unicode_t v_text[1] */
};
```

**Note:** You will need to update `vtinit` (allocation size), `vtputc` (assignment), `vteeol` (clearing), and `updateline` (rendering) to handle `video_cell`.

## 3. Initialization (`main.c`)

Initialize the highlighting engine at startup.

```c
/* In main.c */
#include "highlight.h"

int main(int argc, char *argv[]) {
    /* ... */
    nanox_init(); /* Existing init */
    highlight_init("syntax.ini"); /* Add this */
    /* ... */
}
```

## 4. Rendering Hook (`display.c`)

Modify `show_line` to highlight text before rendering.

```c
/* In display.c */

static void show_line(struct line *lp)
{
    int i = 0, len = llength(lp);
    
    /* 1. Run Highlighting */
    SpanVec spans;
    HighlightState end_state;
    
    /* Use cached start state from line */
    highlight_line(lp->l_text, len, lp->hl_start_state, &spans, &end_state);
    
    /* Update line's end state for propagation */
    if (memcmp(&lp->hl_end_state, &end_state, sizeof(HighlightState)) != 0) {
        lp->hl_end_state = end_state;
        /* Trigger update for next line if state changed */
        struct line *next = lforw(lp);
        if (next != curbp->b_linep) {
            next->hl_start_state = end_state;
            lchange(WFHARD); /* Force re-render of following lines */
        }
    }

    /* 2. Render Spans */
    int current_span_idx = 0;
    int current_style = HL_NORMAL;
    
    int char_idx = 0;
    while (char_idx < len) {
        /* Update style based on spans */
        while (current_span_idx < spans.count) {
            Span *s = (spans.heap_spans) ? &spans.heap_spans[current_span_idx] : &spans.spans[current_span_idx];
            if (char_idx >= s->end) {
                current_span_idx++;
                continue;
            }
            if (char_idx >= s->start) {
                current_style = s->style;
            } else {
                current_style = HL_NORMAL;
            }
            break;
        }
        
        /* Get char and set global color for vtputc */
        unicode_t c;
        int bytes = utf8_to_unicode(lp->l_text, char_idx, len, &c);
        
        /* Set current color attribute (global var or argument to modified vtputc) */
        current_color_attr = colorscheme_get(current_style).fg; 
        
        vtputc(c); /* Modified to use current_color_attr */
        
        char_idx += bytes;
    }
    
    span_vec_free(&spans);
}
```

## 5. Physical Update (`display.c`)

Modify `updateline` to emit ANSI color codes when `v_text[col].color_attr` changes.

```c
static int updateline(int row, struct video *vp)
{
    /* ... inside loop ... */
    if (vp->v_text[col].color_attr != current_phys_color) {
        /* Emit ANSI SGR code */
        set_color(vp->v_text[col].color_attr);
        current_phys_color = vp->v_text[col].color_attr;
    }
    /* ... emit char ... */
}
```
