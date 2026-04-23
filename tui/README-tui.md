# TUI (Text User Interface) Module

This directory encapsulates all terminal interaction and UI rendering logic. By isolating these components into their own tree structure, the codebase enforces a cleaner separation of concerns.

## Object-Oriented Architecture

The TUI system leverages C struct-based polymorphism to achieve object-oriented design:

1. **`struct terminal` Interface**: Acts as an abstract base class (interface). It defines virtual methods for terminal operations (`t_putchar`, `t_move`, `t_set_colors`, etc.).
2. **Terminal Drivers**: 
   - `ncurses.c` (`ncurses_term`): Implements the `struct terminal` interface using Ncurses.
   - `tcap.c` (`tcap_term`): Implements the `struct terminal` interface using pure Termcap/ANSI escapes.
3. **Renderer & Engine**: `display.c` handles virtual screen buffering (`vscreen`) and optimized diff-based rendering.
4. **Extensibility**: Plugins (like `render_plugin.c`) follow a dynamic registration pattern, allowing decoupled UI enhancements (like gutters, ghost text) without modifying the core renderer loop.

This isolation ensures the editor's core logic (`core/`) remains entirely agnostic of whether it is running on Ncurses, Termcap, or potentially a GUI backend in the future.
