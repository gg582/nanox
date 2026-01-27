# Nanox Architectural Overview

Nanox is a modern, nano-inspired user interface layer built on top of the classic uEmacs/PK text editor. It maintains the powerful internal buffer and window management of uEmacs while providing a more intuitive, visual experience similar to modern terminal editors.

## 1. Design Philosophy
- **Buffer-Centric**: All text is stored in buffers, which are represented as circular doubly-linked lists of lines.
- **Windowing**: Multiple windows can display the same or different buffers simultaneously.
- **Incremental Display**: Only changed portions of the screen are redrawn to minimize terminal I/O latency.
- **Modal Modifiers**: Uses Alt/Meta and Ctrl modifiers for complex operations rather than just escape sequences.

## 2. Layered Architecture
- **Terminal Layer (`posix.c`, `tcap.c`)**: Handles raw serial I/O and terminal capability negotiation.
- **Text Layer (`line.c`, `buffer.c`, `region.c`)**: Low-level text manipulation and memory management.
- **Command Layer (`main.c`, `input.c`, `ebind.h`)**: Maps keys to internal C functions and manages the command execution loop.
- **UI/Nanox Layer (`nanox.c`, `display.c`, `highlight.c`)**: Handles the visual experience, status bars, syntax highlighting, and the configuration system.
