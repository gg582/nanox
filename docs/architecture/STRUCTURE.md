# Codebase Structure

This document provides a comprehensive mapping of files in the NanoX project and their specific responsibilities.

## Core System
- **`main.c`**: The engine's heart. Contains the main event loop, command-line argument parsing, and top-level key dispatch.
- **`estruct.h`**: The "Master Header." Defines every critical data structure: `BUFFER`, `WINDOW`, `LINE`, `REGION`, and `TERM`.
- **`edef.h`**: Global variable definitions. Every shared state in the editor is defined here.
- **`ebind.h`**: Keybinding table. Maps internal key codes (e.g., `CONTROL | 'S'`) to C function pointers.
- **`efunc.h`**: External function declarations. Acts as a catalog of all executable commands.

## Text & Buffer Management
- **`buffer.c`**: Logic for creating, finding, and switching between buffers.
- **`line.c`**: The most sensitive file. Handles the actual bytes of text, line allocation, and character insertion/deletion.
- **`region.c`**: Logic for operating on blocks of text defined by the cursor and a mark.
- **`window.c`**: Manages the viewport. Handles screen splitting, resizing, and window switching.

## Rendering & Visuals
- **`display.c`**: The rendering engine. Implements the "Virtual Screen" logic and incremental updates.
- **`nanox.c`**: Implements the Nano-style UI enhancements, including the hint bar and status indicators.
- **`highlight.c`**: The Syntax Highlighting engine. Parses rules and applies styles to text spans.
- **`colorscheme.c`**: Manages themes and maps style IDs to ANSI or TrueColor escape sequences.

## Input & Platform
- **`input.c`**: High-level input handling, including string prompts (mlreply) and command completion.
- **`posix.c`**: Linux/Unix-specific terminal setup (termios) and raw character reading.
- **`tcap.c`**: Integration with the termcap/terminfo database for cross-terminal compatibility.
- **`utf8.c`**: Robust UTF-8 encoder/decoder.

## Editing Features
- **`search.c` / `isearch.c`**: Basic and incremental search/replace functionality.
- **`file.c` / `fileio.c`**: Disk I/O. Handles loading and saving files with safety checks.
- **`word.c`**: Word-level operations (capitalization, word-skipping).
- **`random.c`**: Miscellaneous commands that didn't fit elsewhere (e.g., set-fill-column, insert-tab).
