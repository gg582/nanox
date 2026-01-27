# Buffers and Windows Management

Nanox follows the classic Model-View architecture where **Buffers** represent the data (Model) and **Windows** represent the viewports (View).

## 1. Buffers (`struct buffer`)
A buffer is an object containing the actual text of a file.
- **Independence**: Buffers exist even if they are not currently visible on the screen.
- **Metadata**: Each buffer tracks its own filename, modification status (`BFCHG`), and local modes (e.g., Wrap mode, Read-only mode).
- **Text Storage**: Points to the header of the circular linked list of lines.

## 2. Windows (`struct window`)
A window is a rectangular area of the screen that looks into a buffer.
- **Individual State**: Each window has its own "Dot" (cursor) and "Mark" position. This allows two windows to show different parts of the same buffer simultaneously.
- **Viewport Tracking**: The `w_linep` field points to the line currently at the very top of the window's display area.

## 3. The Window-Buffer Relationship
- **`b_nwnd`**: Each buffer keeps a count of how many windows are currently displaying it.
- **Synchronization**: When a buffer is modified in one window, all other windows showing that same buffer are flagged for a redraw (`WFHARD`) to ensure they stay in sync.
- **Current Window/Buffer**: Global pointers `curwp` and `curbp` always point to the window currently holding the focus and its associated buffer.

## 4. Screen Splitting
Nanox supports vertical screen splitting.
- When the screen is split, the `term.t_nrow` is divided among the active windows.
- The `window.c` file contains logic for `splitwind` (C-X 2) and `delwind` (C-X 0).
