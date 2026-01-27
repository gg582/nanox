# Input Processing System

Nanox uses a multi-layered input system to translate raw terminal bytes into high-level editor commands.

## 1. Layer 1: Raw Byte Reading (`posix.c`)
The lowest level is `ttgetc()`. It reads from file descriptor 0 (STDIN).
- **Non-blocking checks**: Uses `VMIN=0, VTIME=1` for short timeouts to distinguish between a standalone `Esc` key and the start of an escape sequence (like `Esc [ A` for Up Arrow).
- **UTF-8 Reconstruction**: It buffers multiple bytes to reconstruct a single Unicode character.

## 2. Layer 2: Key Decoding (`input.c`)
The `getcmd()` function is the primary entry point for the editor's main loop.
- **Modifier Detection**: It identifies `CONTROL`, `META` (Alt), `SUPER` (Kitty/Win), and `SPEC` (Function keys/Arrows). Kitty's `CSI u` bitmasks are decoded so Super-based combos survive round trips through the terminal.
- **CSI/SS3 Sequences**: Decodes complex ANSI escape sequences (e.g., `\033[1;5D` for Ctrl+Left) into internal integer representations.

## 3. Layer 3: Command Dispatch (`main.c`)
Once `getcmd()` returns a normalized integer key code:
1. **Binding Lookup**: `getbind(c)` searches the `keytab` (from `ebind.h`) for a matching function pointer.
2. **Execution**: If a function is found (e.g., `forwline`), it is executed.
3. **Self-Insertion**: If no binding is found and the key is a printable character, `linsert()` is called to add the character to the buffer.

## 4. Prompting and Interaction (`mlreply`)
For commands requiring text input (like "Find file"), the editor enters a sub-loop in `getstring()`:
- It echoes characters to the message line.
- It handles `Backspace`, `Ctrl+U` (kill line), and `Tab` (file completion) locally within the prompt.
- It returns the final string to the calling function.
