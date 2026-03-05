# Syntax Highlighting Engine

Nanox features a flexible, rule-based syntax highlighting engine implemented in `highlight.c`.

## 1. The Highlighting Pipeline
Highlighting happens per-line during the display pass:
1. `display.c:show_line()` is called for a line.
2. It calls `highlight_line()` with the line's text and its `hl_start_state`.
3. The engine returns a `SpanVec`—a list of byte ranges and their associated style IDs (e.g., Keyword, String, Comment).

## 2. State-Based Highlighting
To handle multi-line comments and strings, the engine is stateful:
- **`HighlightState`**: Maintains a small stack of scopes (block comments, triple/single quoted strings). Each opening delimiter pushes a frame; the matching closing delimiter pops it so once the stack empties the renderer can immediately resume normal syntax styling.
- **Propagation**: If a line's end state changes (e.g., you just started a `/*`), it triggers a `WFHARD` flag on the *next* line to ensure it gets redrawn with the correct new context.

## 3. Rule Parsing (`highlight.ini`)
The engine loads rules from `.ini` files. Key categories include:
- **Keywords/Types/Flow**: Matched using a fast word-boundary check.
- **Delimiters**: Single characters that start/end strings.
- **Comment Tokens**: Sequential characters (e.g., `//`) that mark the rest of the line as a comment.

## 4. Performance Optimizations
- **Span Vectors**: Instead of calculating styles for every character, the engine groups characters into spans, significantly reducing the number of style lookups during rendering.
- **Implicit States**: Simple states like "Inside Double Quotes" are handled within the line, while "Inside Block Comment" is persisted across lines via the `line` structure's `hl_start_state` field.

## 5. Adding a New Language
1. Create a `.ini` file in `configs/nanox/langs/`.
2. Define `extensions = .ext` for normal extension-based detection, and optionally `file_matches = ^regex$` for basename regexes (POSIX extended, case-insensitive) when extensions aren't sufficient (e.g., `Makefile`).
3. Define keyword lists and comment pairs.
4. Nanox will automatically detect and load it on the next startup or file load.
