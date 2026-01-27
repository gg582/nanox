# Interactive Help System

Nanox includes a built-in, searchable help viewer that parses documentation on the fly.

## 1. The Help File (`emacs.hlp`)
The system reads from a structured text file where sections are marked with `=>`.
```text
=> File Operations
Commands for saving and loading...
-------
=> Navigation
Moving the cursor...
```

## 2. Dynamic Loading (`load_help_file`)
Instead of hardcoding help text, `nanox.c` parses `emacs.hlp` into a list of `nanox_help_topic` structures. This allows users to customize the help content by simply editing the `.hlp` file.

### Localization via `help_language`
Set `help_language=<code>` under the `[ui]` section of `~/.config/nanox/config` to request a localized manual. Nanox will first look for `emacs-<code>.hlp` (e.g., `emacs-ko.hlp`) and gracefully fall back to `emacs.hlp` if the localized file is missing. English (`en`) is the default and now called out in the stock config.

## 3. Help Mode Navigation
When `F1` or `nanox_help_command` is triggered, the editor enters a specialized modal state:
- **Menu View**: Displays a list of all available help topics.
- **Section View**: Displays the content of the selected topic.
- **Navigation Keys**:
  - `Up/Down` or `i/k`: Scroll through topics or text.
  - `Enter`: Open a topic.
  - `Backspace`: Go back to the menu.
  - `Esc/F1`: Exit help.

## 4. UI Rendering
The help system bypasses the normal buffer rendering and uses `TTmove()` and `TTputc()` directly to draw a clean, full-screen menu interface. Once closed, it sets `sgarbf = TRUE` to force the editor to redraw the original buffers.
