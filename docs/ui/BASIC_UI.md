# BASIC UI LAYOUT (ASCII)

Nanox deliberately keeps the base interface purely ASCII so every terminal can reproduce it without extended glyphs. The diagram below reconstructs the default screen using the exact characters that `modeline()`, `draw_hint_row()`, and `mlwrite()` emit.

```
+------------------------------------------------------------------------------+
| README.md        Ln 128/512  Col 08  Modified *    --W                       |
+------------------------------------------------------------------------------+
|00001|#include <stdio.h>                                                      |
|00002|                                                                         |
|00003|int main(void) {                                                        |
|00004|    puts("Hello Nanox");                                                |
|00005|}                                                                       |
|....                                                                          |
|                                                                              |
|                                                                              |
+------------------------------------------------------------------------------+
|F1 Help  F2 Save  F3 Open  F4 Quit  F5 Search  F6 Copy  F7 Cut  F8 Paste      |
|F9 Slot1 F10 Slot2 F11 Slot3 F12 Slot4 | Message: Saved README.md (3.1 KB)    |
+------------------------------------------------------------------------------+
```

## Modeline
- Drawn by `modeline()` in `display.c` using ASCII separators, brackets, and a trailing lamp field such as `--W` or `--E`.
- Shows the buffer name, total lines, cursor column, modification mark `*`, and the current lamp state.
- Uses the same ANSI coloring system as the text canvas but only relies on ASCII glyphs, so the status row renders on legacy TTYs.

## Text Canvas and Gutter
- Each buffer line is prefixed with a decimal gutter (`render_gutter()` writes five digits plus the pipe `|`).
- Wrapped lines insert four literal spaces so the continuation indicator is also ASCII.
- Control characters are visualized through caret notation (`^A`, `^?`) provided by `vtputc()`, guaranteeing no non-ASCII glyphs leak into the viewport.

## Hint Rows and Message Line
- The two hint rows at the bottom come from `draw_hint_row()` and are composed entirely of ASCII strings such as `F1 Help` or `F4 Quit`.
- `mlwrite()` shares the second hint row; when a prompt or notification is active it simply replaces the right side of the ASCII hint row with the message text.
- When the hint bar is disabled, the same ASCII text is still written to the last physical row, preserving compatibility with bare terminals.

## Lamps and Configurable Labels
- The lamp system (`nanox_set_lamp()` in `nanox.c`) prints ASCII tokens (`--E`, `--W`, or blanks) so alerts stand out even without color.
- User overrides in `configs/nanox/config` (`warning_format`, `error_format`) are expected to stay ASCII because the hint and modeline renderers treat each byte as a single column.

## Rendering Pipeline Notes
- `vtputc()` clamps everything to printable ASCII by expanding tabs into spaces and showing control bytes via `^` notation.
- `updateline()` diff-checks two ASCII-only `video_cell` buffers before emitting terminal escapes, so no UTF-8 decoding is needed when the default UI is used.
- The help sheet defined in `nanox.c` also uses ASCII box rules (`=` and `-`) to mirror the look of the main UI.
