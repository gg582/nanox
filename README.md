# Nanox 🚀

**Minimalist, Nano-inspired UI Layer for the Legendary MicroEmacs**

Nanox is a modern, feature-rich fork of **uEmacs/PK** that brings the intuitive user experience of `nano` to the professional power of `MicroEmacs`. It's designed to be ultra-fast, lightweight, and fully UTF-8 aware, making it the perfect choice for terminal-based editing on any system.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![License](https://img.shields.io/badge/license-Custom-blue)](LICENSE)
[![Stars](https://img.shields.io/github/stars/yourusername/nanox?style=social)](https://github.com/yourusername/nanox)

---
### Fun Fact

Pronunciation: /na.noks/                                                                                                     │
Incorrect: /neɪ.noks/, /nɛ.noks/

## Key Features

- **Ultra-Fast Performance**: Built on the highly optimized MicroEmacs/PK core.
- **Syntax Highlighting**: Real-time highlighting for multiple languages via `syntax.ini`.
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

[edit]
soft_tab=true
soft_tab_width=4

[search]
case_sensitive_default=false
```

---

## 📖 Help & Documentation

Press **F1** inside the editor to open the interactive Nanox help system. You can browse keybindings, configuration options, and deep-dive into the MicroEmacs manual by pressing **Enter** on any function name.

---

## 📜 History

Nanox is based on **uEmacs/PK 4.0**, which itself is an enhanced version of **MicroEMACS 3.9e** (written by Dave G. Conroy and Daniel M. Lawrence). This version carries forward the tradition of extreme portability and efficiency while adding modern terminal capabilities.

---

**Star this project on GitHub to support its development!** ⭐
