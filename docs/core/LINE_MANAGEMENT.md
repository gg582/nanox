# Line Processing and Buffer Management

At its core, NanoX represents text as a circular doubly-linked list of `line` structures. This document explains how text is stored and manipulated.

## 1. The `line` Structure

Defined in `estruct.h`:
```c
struct line {
    struct line *l_fp;      /* Forward link  */
    struct line *l_bp;      /* Backward link */
    int l_size;             /* Allocated size */
    int l_used;             /* Actual bytes used */
    char l_text[1];         /* Text data (flexible array) */
    HighlightState hl_start_state; /* Syntax highlighting state */
    HighlightState hl_end_state;
};
```

## 2. Memory Allocation (`line.c`)

- **`lalloc(int used)`**: Allocates a new line. It rounds up the allocation to the nearest `BLOCK_SIZE` (16 bytes) to reduce fragmentation from frequent small edits.
- **Flexible Array**: `l_text[1]` is a common C idiom for variable-sized data following the struct header.

## 3. Core Text Operations

### Insertion (`linsert`)
When inserting a character at the cursor (dot):
1. **Easy Case**: If the current line has enough `l_size`, the text is shifted right using `memmove` (or a manual loop) and the new character is inserted.
2. **Hard Case**: If `l_used + 1 > l_size`, a new, larger line is allocated. The old text is copied over, links are updated, and the old line is `free()`d.

### Deletion (`ldelete`)
- Removes `n` characters starting from the cursor.
- If the deletion crosses line boundaries, lines are merged.
- All windows displaying the buffer are updated to ensure their `w_dotp` and `w_doto` remain valid.

## 4. Line Navigation

Navigation is done by following the `l_fp` (forward) and `l_bp` (backward) pointers. The end of the buffer is a special "header" line stored in `buffer->b_linep`. 

```c
/* Check if we are at the end of the buffer */
if (lp->l_fp == bp->b_linep) {
    /* At end of file */
}
```

## 5. UTF-8 Handling

NanoX is UTF-8 aware. While `l_text` stores raw bytes, functions like `linsert` and `backchar` use `unicode_to_utf8` and `utf8_to_unicode` to ensure cursor movement and editing respect multi-byte character boundaries.

## 6. Optimization: `lchange()`

Whenever a line is modified, `lchange(int flag)` is called. This sets flags like `WFEDIT` (line change) or `WFHARD` (major change) in the window structure, which the display engine uses to perform incremental updates instead of redrawing the whole screen.
