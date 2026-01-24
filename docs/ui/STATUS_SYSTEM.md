# Status Bars and Indicators

NanoX provides constant feedback to the user through three distinct UI areas: the Modeline, the Hint Bar, and the Message Line.

## 1. The Modeline (Status Row)
Located above the hint bar, the modeline displays metadata about the current window:
- **Filename**: The name of the file being edited.
- **Position**: Line and Column numbers (updated via `window_line_number()` and `window_column_number()`).
- **State**: Indicators for modification (`*`) and active lamps (`--E`, `--W`).
- **Rendering**: Controlled by the `modeline()` function in `display.c`.

## 2. The Hint Bar
The bottom-most two rows of the terminal display a "Nano-style" shortcut menu.
- **Dynamic Content**: Shows common bindings like `F1 Help`, `F2 Save`, etc.
- **Configuration**: Can be toggled on or off in the config via `hint_bar = true/false`.
- **Implementation**: Drawn using `draw_hint_row()` in `display.c`.

## 3. The Message Line (Command Line)
The very last row (shared with the second hint bar row or appearing temporarily) is used for messages and prompts.
- **`mlwrite(fmt, ...)`**: Used to display transient messages like "(Saved file.c)".
- **`mlreply(prompt, ...)`**: Used to ask the user for input (e.g., "Find file: ").

## 4. Visual Lamps
Lamps are priority-based status indicators:
1. **`--E` (Error)**: Critical failures (Red/Bold in some themes).
2. **`--W` (Warning)**: Non-fatal issues (Yellow/Bold).
These are set via `nanox_set_lamp()` and cleared when a new message is written to the message line.
