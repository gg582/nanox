# Rendering Engine (Display System)

The display system in NanoX is designed for high-performance rendering over terminal connections.

## 1. The Virtual Screen
NanoX maintains an internal array of the entire screen state called `vscreen`. This is an array of `struct video`, which contains `video_cell` structures.
- **`video_cell`**: Stores the Unicode character, foreground color, background color, and attributes (bold, underline) for every single X,Y coordinate on the screen.

## 2. The Update Loop (`display.c`)
Rendering follows a strict multi-pass process:
1. **Reframe**: Ensures the current buffer position (dot) is visible in the window. If not, it adjusts the `w_linep` (top line).
2. **Updall / Updone**: Computes the new content for lines that are marked "dirty" (`WFCHG`, `WFEDIT`, etc.) and writes them to the `vscreen`.
3. **Modeline**: Renders the status bars at the bottom of the screen.
4. **Updupd**: Compares the new `vscreen` with the previous state.
5. **Physical Output**: Only the differences are sent to the terminal using optimized cursor movement and color sequences.

## 3. Incremental Updates
To save bandwidth, NanoX never "clears and redraws" unless explicitly requested (via `Ctrl+L`). Instead, it uses `updateline()` to:
- Move the cursor directly to the changed character.
- Change colors only when the style of a cell differs from the previous one.
- Use "Erase to End of Line" (`CE`) sequences where possible to clear trailing whitespace.

## 4. Attributes and Colors
Colors are handled using ANSI escape sequences. NanoX supports:
- **8/16 ANSI Colors**: Standard terminal colors.
- **256 Colors**: Extended terminal palettes.
- **24-bit TrueColor**: RGB values (supported if the terminal is capable).
Attributes like **Bold** (`\033[1m`) and **Underline** (`\033[4m`) are applied per-cell during the rendering pass.
