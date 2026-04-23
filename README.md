# Nanox

![Demo Video](./nanox.gif)

**Bring function keys back to functions**

## Fast Guide

```bash
nx
# or nanox
# Press F1 to see emacs guide
```
*A terminal editor designed around two hands, physical keys, and explicit intent.*

Nanox(/na.noks/) is a modern, minimalistic, feature-rich fork of **uEmacs/PK** that brings the intuitive user experience of `nano` to the professional power of `MicroEmacs`. It's designed to be ultra-fast, lightweight, and fully UTF-8 aware, making it the perfect choice for terminal-based editing on any system.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![License](https://img.shields.io/badge/license-Custom-blue)](LICENSE)
[![Stars](https://img.shields.io/github/stars/gg582/nanox?style=social)](https://github.com/gg582/nanox)
![Hangeul Ready](https://img.shields.io/badge/Hangeul-UTF--8%20Ready-red)

---

## Experience the Advantages

Nanox isn't just another text editor. It is built to optimize your workflow by turning raw performance and smart conventions into tangible editing advantages.

### ⚡ Blazing Fast, Zero-Latency Editing
Forget the sluggishness of Electron apps or the heavy startup times of LSP-bloated editors. Nanox is written in pure C.
* **Deterministic 1:1 Syntax Matching:** We use a high-performance state-machine matching engine instead of resource-intensive Tree-sitter. Highlighting is calculated at the speed of raw text processing—no flickering, no context-guessing errors.
* **Instant Startup:** Nanox opens instantly, ready for your keystrokes before your finger leaves the Enter key.

### 🧠 Smart, Predictable, and Out of Your Way
We’ve ditched complex semantic analysis for lean, convention-driven logic.
* **Rule-Based Indentation:** Indentation is consistent, fast, and follows established coding conventions without generating massive ASTs, maintaining a microscopic memory footprint.
* **Fuzzy Autocompletion & LSP Support:** Enjoy blazing-fast built-in fuzzy matching for buffer words, and seamlessly tap into LSP (Language Server Protocol) when you need deep language intelligence.
* **Cscope Integration:** Navigate massive codebases instantly with native Cscope support.
* **Integrated Spell Checker:** Built-in Hunspell integration catches typos seamlessly without breaking your flow.

### 🌐 Flawless Internationalization (Hangeul Ready)
Unlike older Emacs forks that mangle CJK (Chinese, Japanese, Korean) input, Nanox treats global text as a first-class citizen.
* **Atomic UTF-8 Minibuffer:** A robust Minibuffer Window System with an 8-bit masked output pipeline ensures no "Latin-1 Ghosts."
* **Perfect CJK Sync:** Integrated cursor positioning correctly calculates double-width characters, so your cursor is always exactly where you expect it to be.
* **Race-Condition Free:** Implements an atomic "Gate Buffer" logic for ISearch and Replace, ensuring your terminal never "beeps" due to incomplete UTF-8 fragments.

### 🎨 A Visually Rich Terminal
Terminal editors don't have to be plain text only.
* **Live Color Code Preview:** Working with CSS or UI code? Nanox automatically detects `#RGB`, `rgb()`, `rgba()`, and `hsl()` strings, rendering a live color preview box right at the end of the line.
* **Markdown & HTML Rich Text Rendering:** Nanox parses `**bold**`, `*italic*`, and `<u>underline</u>` tags and renders them with actual terminal styling, bringing your documentation to life without altering the raw markup.
* **Accessibility-First Themes:** Features a colorblind-friendly default theme, with a built-in Python script (`cognitive_themegen.py`) to generate accessible custom color palettes.

### 🚀 Frictionless Workflow
Designed for human hands and explicit intent.
* **File Reservation Slots:** Jump seamlessly between 4 distinct workspaces. Bookmark files and instantly switch context using `F9` through `F12`. No more losing your place in a labyrinth of hidden buffers.
* **Nano-Style UI:** Familiar hint bars at the bottom mean you never have to memorize obscure chord combinations to get started.
* **Smart Horizontal Scrolling:** The view automatically and intelligently scrolls to keep the cursor visible on long lines, correctly handling CJK characters and emoji widths.

### 🚀 Advanced Editing Capabilities
Nanox now includes powerful tools for structured text and data manipulation:
* **Visual Block (Viblock) Mode:** Perform rectangular editing and replacements (`viblock-edit`, `viblock-replace`). Swap non-overlapping line ranges instantly with `viblock-flip`.
* **Smart Numbering:** Automatically rewrite or reverse list numbering (1., 2., 3...) while preserving suffixes and indentation using `viblock-set-nr`.
* **Sed-style Replace (^R):** Execute powerful regex replacements using the standard `s/pattern/replacement/flags` syntax.
* **Built-in Lint & Tidy:** Automatically fix indentation across the entire buffer using a heuristic-based step detector via the `lint` command in Command Mode (^V).
* **Integrated Color Engine:** Transform color codes (`#hex`, `rgb`) in real-time. Adjust hue/contrast, invert colors, or simulate colorblindness directly within the editor using the `colors` command.
* **Raw Binary Inspection:** Analyze files at the byte or bit level with `file raw-sig`. Supports offset seeking, endianness switching, and bit-alignment visualization.
* **Massive File Queuing:** The slot system can expand up to 64 concurrent file slots, allowing you to queue dozens of files and cycle through them seamlessly.

---

## ⌨️ Quick Shortcuts

| Key | Action | Key | Action |
| :--- | :--- | :--- | :--- |
| **F1** | Help Menu | **F2** | Save File |
| **F3** | Open File | **F4** | Quit Editor |
| **F5** | Search Forward | **F6** | Copy (S:End) |
| **F7** | Cut (S:End) | **F8** | Yank (Paste) |
| **F9-F12** | Jump to Slot | **Ctrl+F9-F12** | Set Slot |
| **Alt+G** | Goto Line | **Alt+X** | Execute Command |

### Classic Emacs Bindings

For users coming from a traditional Emacs background, Nanox maintains compatibility with several core MicroEmacs shortcuts:

| Key | Action | Key | Action |
| :--- | :--- | :--- | :--- |
| **Ctrl+X** | Cut (S:End) | **Ctrl+W** | Copy (S:End) |
| **Ctrl+Y** | Yank (Paste) | **Alt+Space** | Set Mark |
| **Ctrl+X Ctrl+X** | Exchange Point/Mark | **Ctrl+X Ctrl+U** | Upper Case Region |
| **Ctrl+X Ctrl+L** | Lower Case Region | **Alt+Ctrl+R** | Query Replace |
| **Alt+R** | Replace String | **Alt+W** | Search Forward |
| **Alt+Z** | Quick Save & Exit | **Alt+X** | Execute Named Command |
| **Alt+Ctrl+C** | Word Count (Region) | | |

---

## Installation & Build

Nanox is written in C and has minimal dependencies.

### Prerequisites
- A C compiler (GCC/Clang)
- `libncurses` or `termcap`
- `libhunspell` (optional, for spell checking)

### Building from Source
```bash
git clone https://github.com/gg582/nanox.git
cd nanox
make
make configs-install
sudo make install
```

---

## ⚙️ Configuration

Nanox looks for configuration in `~/.config/nanox/config` (or `~/.local/share/nanox/config`).

```ini
[ui]
hint_bar=true
warning_lamp=true
help_key=F1
help_language=en

[edit]
soft_tab=true
soft_tab_width=4
autocomplete=true
use_lsp=false

[search]
case_sensitive_default=false
```

Set `help_language` to a locale code (default `en`) to make Nanox look for `emacs-<code>.hlp` before falling back to the bundled `emacs.hlp`.

Autocomplete defaults to `true` and now uses fuzzy matching with built-in language keywords and buffer words. Set `autocomplete=false` to disable it.

Set `use_lsp=true` to enable extra completion sources when a language server binary is installed for the active file type (`clangd`, `pylsp`/`pyright-langserver`, `typescript-language-server`, `gopls`, `rust-analyzer`, `jdtls`).

### Syntax Highlighting Profiles

Nanox now ships with first-class syntax rules for **32 widely used languages** (C/C++, Python, Ruby, Rust, Go, Java/Kotlin/Scala, JavaScript/TypeScript, SQL, HTML/CSS/JSON/YAML, etc.). These definitions live in `syntax.ini` and cover accurate flow/type/keyword lists plus bracket, triple-quote, and numeric highlighting where languages support them.

You can extend or override the built-ins without recompiling:

1. Create `~/.config/nanox/langs` (or `~/.local/share/nanox/langs`).
2. Drop one or more `.ini` files there. Each file can hold a single language section using the same keys found in `syntax.ini`. Use `file_matches = ^regex$` when you need to match basenames without reliable extensions (e.g., `Makefile`, `Kconfig`); patterns follow POSIX extended regular expressions and are matched case-insensitively against the filename.
3. Restart Nanox. The editor will automatically merge everything under the `langs/` directory after loading the base profiles.

Example (`~/.config/nanox/langs/futhark.ini`):

```ini
[futhark]
extensions = fth
line_comment_tokens = --
string_delims = ",'
flow = if,then,else,loop,for,while
keywords = let,entry,fn,module,open,import,include
return_keywords = in
```

Sample profiles for rarer but still relevant languages (Ada, COBOL, Elixir, Erlang, Fortran) are bundled under `configs/nanox/langs` and installed alongside the default configuration via `make configs-install`.

---

## Theme Generator

Theme generator is available to handle accessibility issue with enough flexibility.
Please run `python3 ./accessibility/cognitive_themegen.py` to generate.

Bundled themes now include additional popular and classic presets such as:
`one-dark`, `nord`, `gruvbox-dark`, `solarized-dark`, `solarized-light`, and `zenburn-classic`.

## Help & Documentation

Press **F1** inside the editor to open the interactive Nanox help system. You can browse keybindings, configuration options, and deep-dive into the MicroEmacs manual by pressing **Enter** on any function name.

---

## History

Nanox is based on **uEmacs/PK 4.0**, which itself is an enhanced version of **MicroEMACS 3.9e** (written by Dave G. Conroy and Daniel M. Lawrence). This version carries forward the tradition of extreme portability and efficiency while adding modern terminal capabilities.

---

**Star this project on GitHub to support its development!** ⭐