# Kill Buffer and Region Operations

The "Kill Buffer" is where deleted or copied text is stored for later "yanking" (pasting). It is separate from the system clipboard but can be synchronized with it on some platforms.

## 1. Data Structure

The kill buffer is implemented as a linked list of fixed-size chunks to handle text of arbitrary length without massive reallocations.

```c
struct kill {
    struct kill *d_next;    /* Next chunk */
    char d_chunk[KBLOCK];   /* 1024 bytes of text */
};
```

## 2. Regions

Most kill operations act on a **Region**, which is the range of text between the **Dot** (cursor) and the **Mark**.

### `getregion(struct region *rp)`
This critical function in `region.c` calculates the bounds of a region:
1. It determines which point (dot or mark) comes first in the buffer.
2. It calculates the `r_size` (number of characters) between them.
3. It fills a `struct region` containing the starting line, offset, and total size.

## 3. Operations

### Kill Region (`killregion`)
1. Calls `getregion` to find the bounds.
2. Clears the current kill buffer if the last command wasn't also a "kill" type (allowing consecutive kills to append to the same buffer).
3. Moves text from the buffer into the kill buffer.
4. Deletes the text from the buffer.

### Copy Region (`copyregion`)
Similar to `killregion`, but it does not call `ldelete`. It simply iterates through the region and inserts characters into the kill buffer using `kinsert()`.

### Yank (Paste)
1. Iterates through the linked list of `kill` chunks.
2. Inserts each character back into the buffer at the current cursor position using `linsert()`.

## 4. Consecutive Kills

uEmacs (and Nanox) handles consecutive kill commands specially. If you kill multiple lines in a row, they are accumulated into a single kill buffer entry, so a single "Yank" will restore all of them. This is managed by the `thisflag` and `lastflag` variables checking for the `CFKILL` bit.

## 5. Implementation Summary

- **`kinsert(int c)`**: Adds a character to the kill buffer, allocating new `kill` chunks as needed.
- **`kdelete()`**: Frees all chunks in the current kill buffer.
- **`region.c`**: Contains the logic for defining and acting on text ranges.
- **`line.c`**: Contains the low-level `kinsert` implementation.

## 6. Keybindings

### Modern Nanox Shortcuts
- **Ctrl+Super+Arrows**: Select Region (visual highlight)
- **Ctrl+Super+Shift+Arrows**: Cut Region (Kill)

### Classic Emacs Bindings
- **Ctrl+W**: `kill-region` (Cut)
- **Alt+Ctrl+Y**: `copy-region` (Copy)
- **Ctrl+Y**: `yank` (Paste)
- **Alt+Space**: `set-mark` (Set starting point)
- **Ctrl+X Ctrl+X**: `exchange-point-and-mark`
- **Ctrl+X Ctrl+U**: `upper-region`
- **Ctrl+X Ctrl+L**: `lower-region`
- **Alt+Ctrl+C**: `word-count` (Count words/lines in region)
