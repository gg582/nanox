# NanoX Documentation Index (TOTAL.md)

This document serves as the primary index for the NanoX project documentation. All documentation is organized into functional subdirectories with files named in **UPPERCASE** for clarity and consistency.

## Documentation Structure
The documentation is divided into the following categories:
- **Architecture**: High-level design and file mapping.
- **Core**: Internal data structures, text processing, and configuration.
- **UI**: Rendering engine, syntax highlighting, and status indicators.
- **Interaction**: Input systems, selection logic, and help viewer.
- **Platform**: OS-level integration such as signal handling.
- **Development**: Developer guidelines and tooling setup.

---

## 1. System Architecture
- **[Architectural Overview](architecture/ARCH_OVERVIEW.md)**: High-level system architecture, design philosophy, and the layered model of the editor.
- **[Codebase Structure](architecture/STRUCTURE.md)**: A detailed mapping of every source file (`.c`, `.h`) and its specific responsibility.

## 2. Core Editor Systems
- **[Line & Buffer Management](core/LINE_MANAGEMENT.md)**: Internal representation of text using circular doubly-linked lists and memory allocation.
- **[Configuration Parsing](core/CONFIG_PARSING.md)**: Guide to the INI-style configuration system used for UI and language rules.
- **[Regions and Kill-Buffer](core/REGIONS_AND_KILLBUFFER.md)**: Description of the "kill buffer" (clipboard) and text region calculation.
- **[Buffer & Window Relationship](core/BUFFER_WINDOW_MANAGEMENT.md)**: Explains the Model-View architecture linking buffers to viewport windows.

## 3. User Interface & Rendering
- **[Rendering Engine](ui/RENDERING_ENGINE.md)**: Deep dive into the Virtual Screen, incremental updates, and ANSI output logic.
- **[Syntax Highlighting](ui/SYNTAX_HIGHLIGHTING.md)**: Explanation of the state-based highlighting engine and language rules.
- **[Status Bar & Indicators](ui/STATUS_SYSTEM.md)**: Documentation on the Modeline, Hint Bar, and the Lamp system (`--E` and `--W`).

## 4. Interaction & Input
- **[Input Decoding System](interaction/INPUT_SYSTEM.md)**: Covers the input pipeline from raw bytes to high-level command dispatch.
- **[Interactive Help System](interaction/HELP_SYSTEM.md)**: Guide to the dynamic help loader and the full-screen help interface.

## 5. Platform & System
- **[Signal Handling](platform/SIGNALS.md)**: Technical details on handling `SIGWINCH` (resize) and emergency saves (`SIGTERM`/`SIGHUP`).

## 6. Development & Tooling
- **[EditorConfig Guide](development/EDITORCONFIG.md)**: Guide to using EditorConfig for consistent coding styles across editors.
