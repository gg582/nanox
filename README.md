# Nanox

![Demo Video](./nanox.gif)

**Minimalist, Nano-inspired UI Layer for the Legendary MicroEmacs**

Nanox(/na.noks/) is a modern, feature-rich fork of **uEmacs/PK** that brings the intuitive user experience of `nano` to the professional power of `MicroEmacs`. It's designed to be ultra-fast, lightweight, and fully UTF-8 aware, making it the perfect choice for terminal-based editing on any system.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![License](https://img.shields.io/badge/license-Custom-blue)](LICENSE)
[![Stars](https://img.shields.io/github/stars/yourusername/nanox?style=social)](https://github.com/yourusername/nanox)
![Hangeul Ready](https://img.shields.io/badge/Hangeul-UTF--8%20Ready-red)

---

## Key Strengths: Heart of Efficiency

### 1. Deterministic 1:1 Syntax Matching

Unlike heavy modern editors that rely on resource-intensive Tree-sitter or LSP, this editor utilizes a **high-performance 1:1 state-machine matching**.

* **Zero Latency:** Highlighting is calculated at the speed of raw text processing.
* **Predictable:** No more flickering colors or context-guessing errors. What you see is exactly what the state machine sees.

### 2. Convention-Driven Rule-Based Indentation

We’ve ditched complex semantic analysis for **lean, rule-based logic** that follows established coding conventions (e.g., immediate indent-step after a brace).

* **Logic over Heuristics:** Indentation is consistent, fast, and follows a strict set of predictable rules.
* **Minimal Overhead:** By avoiding AST generation, the editor maintains a microscopic memory footprint while providing the essential "smart" feel of a modern IDE.

### 3. [NEW] Atomic UTF-8 Minibuffer Window
Unlike other `uEmacs` forks that suffer from broken CJK input in the message line, Nanox features a robust **Minibuffer Window System**:
* **No More Latin-1 Ghosts:** Uses a dedicated 8-bit masked output pipeline (`TTputc & 0xFF`) to stop sign-extension artifacts.
* **Perfect CJK Sync:** Integrated with `mystrnlen_raw_w` for precise cursor positioning on double-width Korean/Chinese/Japanese characters.
* **Race-Condition Free:** Implements an atomic "Gate Buffer" logic for ISearch and Replace, ensuring your terminal never "beeps" due to incomplete UTF-8 fragments.

### 4. [NEW] Markdown & HTML Rich Text Rendering
Nanox brings **visual formatting** to Markdown and HTML files directly in the terminal:

* **Bold Text:** `**text**` or `__text__` is rendered with bold styling
* **Italic Text:** `*text*` or `_text_` is rendered with distinctive highlighting
* **Underline:** `<u>text</u>` in HTML and Markdown files shows underlined text
* All formatting is purely visual and preserves the raw markup in the file

### 5. [NEW] Live Color Code Preview
For developers working with colors, Nanox provides **inline color preview boxes**:

* **Hex Colors:** `#RGB` and `#RRGGBB` formats are detected and previewed
* **RGB/RGBA:** `rgb(r, g, b)` and `rgba(r, g, b, a)` show color boxes
* **HSL/HSLA:** `hsl()` and `hsla()` color formats are also supported
* Preview boxes appear at the end of lines containing color codes
* Works in **any file type** - CSS, JavaScript, HTML, Python, and more!

### 6. [NEW] Smart Horizontal Scrolling
For long lines that extend beyond the terminal width:

* **Auto-scroll:** The view automatically scrolls to keep the cursor visible
* **Unicode-aware:** Correctly handles CJK characters and emoji widths
* **Visual indicator:** A `$` marker shows when content extends off-screen


## Key Features

- **Ultra-Fast Performance**: Built on the highly optimized MicroEmacs/PK core.
- **Syntax Highlighting**: 32 built-in language profiles plus pluggable `.ini` files for anything else.
- **Nano-Style UI**: Familiar hint bars and function key shortcuts (F1-F12).
- **File Reservation Slots**: "Bookmark" up to 4 files and jump between them instantly with `F9`-`F12`.
- **Powerful Search/Replace**: Support for regular expressions.
- **Integrated Spell Check**: Built-in support for Hunspell.
- **Colorschemes**: Default theme is colorblind-friendly. Waiting for PRs to enhance.

---

## ⌨️ Quick Shortcuts

| Key | Action | Key | Action |
| :--- | :--- | :--- | :--- |
| **F1** | Help Menu | **F2** | Save File |
| **F3** | Open File | **F4** | Quit Editor |
| **F5** | Search Forward | **F6** | Query Replace |
| **F7** | Kill Line | **F8** | Yank (Paste) |
| **F9-F12** | Jump to Slot | **Ctrl+F9-F12** | Set Slot |
| **Alt+G** | Goto Line | **Alt+X** | Execute Command |
| **Ctrl+Super+Arrows** | Select Region (`Ctrl+Alt` fallback) | **Ctrl+Super+Shift+Arrows** | Cut Region |

### Classic Emacs Bindings

For users coming from a traditional Emacs background, Nanox maintains compatibility with several core MicroEmacs shortcuts:

| Key | Action | Key | Action |
| :--- | :--- | :--- | :--- |
| **Ctrl+W** | Kill Region (Cut) | **Alt+Ctrl+Y** | Copy Region |
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

[search]
case_sensitive_default=false
```

Set `help_language` to a locale code (default `en`) to make Nanox look for `emacs-<code>.hlp` before falling back to the bundled `emacs.hlp`.

### Syntax Highlighting Profiles

Nanox now ships with first-class syntax rules for **32 widely used languages** (C/C++, Python, Ruby, Rust, Go, Java/Kotlin/Scala, JavaScript/TypeScript, SQL, HTML/CSS/JSON/YAML, etc.). These definitions live in `syntax.ini` and cover accurate flow/type/keyword lists plus bracket, triple-quote, and numeric highlighting where languages support them.

You can extend or override the built-ins without recompiling:

1. Create `~/.config/nanox/langs` (or `~/.local/share/nanox/langs`).
2. Drop one or more `.ini` files there. Each file can hold a single language section using the same keys found in `syntax.ini`.
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

## Help & Documentation

Press **F1** inside the editor to open the interactive Nanox help system. You can browse keybindings, configuration options, and deep-dive into the MicroEmacs manual by pressing **Enter** on any function name.

---

## History

Nanox is based on **uEmacs/PK 4.0**, which itself is an enhanced version of **MicroEMACS 3.9e** (written by Dave G. Conroy and Daniel M. Lawrence). This version carries forward the tradition of extreme portability and efficiency while adding modern terminal capabilities.

---

**Star this project on GitHub to support its development!** ⭐
