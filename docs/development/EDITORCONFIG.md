# EditorConfig Usage Guide

EditorConfig is a tool designed to maintain consistent coding styles for multiple developers working on the same project across various editors and IDEs. This document explains how to use EditorConfig and details the specific configurations for the NanoX project.

---

## 1. What is EditorConfig?

EditorConfig consists of a file named `.editorconfig` placed in the project root. It serves as a standard to automatically configure indentation styles, character encoding, line endings, and more as you switch between different editors.

### Key Advantages

* **Consistency**: Ensures all developers adhere to the same coding style.
* **Automation**: Editors automatically apply settings upon opening a file.
* **Versatility**: Supported by most major IDEs and text editors.

---

## 2. Editor Setup

### Visual Studio Code

VSCode supports EditorConfig natively. If you need the extension for advanced features:
`Extension ID: EditorConfig.EditorConfig`

### Vim / Neovim

A plugin is required:

```vim
" For vim-plug
Plug 'editorconfig/editorconfig-vim'

" For packer.nvim (Neovim)
use 'editorconfig/editorconfig-vim'

```

### JetBrains IDEs (IntelliJ, CLion, etc.)

Native support is built-in. Enable it in settings:
`Settings → Editor → Code Style → Enable EditorConfig support`

### Sublime Text

Install via Package Control:
`Package Control: Install Package → EditorConfig`

### Emacs

Install the package:

```elisp
;; For MELPA
(use-package editorconfig
  :ensure t
  :config
  (editorconfig-mode 1))

```

---

## 3. .editorconfig Syntax

### Basic Structure

```ini
# Stop searching for .editorconfig files in parent directories
root = true

# Apply to all files
[*]
charset = utf-8
end_of_line = lf

# Apply to specific file patterns
[*.c]
indent_style = tab
indent_size = 8

```

### Key Properties

| Property | Description | Possible Values |
| --- | --- | --- |
| `root` | Identifies the root file | `true` |
| `charset` | Character encoding | `utf-8`, `latin1`, `utf-16be`, `utf-16le` |
| `end_of_line` | Newline character | `lf`, `cr`, `crlf` |
| `indent_style` | Indentation type | `tab`, `space` |
| `indent_size` | Indentation size | Number (e.g., `2`, `4`, `8`) |
| `tab_width` | Visual width of a tab | Number (e.g., `8`) |
| `trim_trailing_whitespace` | Remove trailing spaces | `true`, `false` |
| `insert_final_newline` | Add newline at end of file | `true`, `false` |

### File Pattern Matching

```ini
# Single extension
[*.c]

# Multiple extensions
[*.{c,h}]

# Specific filename
[Makefile]

# All files in a specific directory
[lib/**.js]

# Recursive matching
[**/test_*.py]

```

---

## 4. NanoX Project Configurations

The NanoX project utilizes the following rules in its `.editorconfig`:

### C Source Files (`*.c`, `*.h`)

```ini
[*.{c,h}]
indent_style = tab
indent_size = 8
tab_width = 8

```

* Adopts the traditional **Linux Kernel style**.
* Uses **Tab** characters for indentation.
* Sets tab width to **8**.

### Makefile

```ini
[Makefile]
indent_style = tab
indent_size = 8

```

* **Mandatory**: Makefiles must use tab characters.
* Using spaces for indentation will cause build failures.

### Markdown Files (`*.md`)

```ini
[*.md]
indent_style = space
indent_size = 2
trim_trailing_whitespace = false

```

* Uses **2-space** indentation.
* **Trailing whitespace is preserved**, as it is used for line breaks in Markdown.

### Configuration Files (`*.ini`, `*.rc`)

```ini
[*.{ini,rc}]
indent_style = space
indent_size = 4

```

* Uses **4-space** indentation for consistent readability.

---

## 5. Applying EditorConfig to a New Project

1. **Create the file**: Create a `.editorconfig` file in the project root.
`touch .editorconfig`
2. **Add basic settings**: Define the root and global defaults.
3. **Add language-specific settings**: Customize rules for the languages used in your project (Python, JavaScript, C++, etc.).
4. **Commit to Git**: Ensure the configuration is shared with the team.

---

## 6. Troubleshooting

* **Settings not applying**: Verify that the EditorConfig plugin is active in your editor.
* **File location**: Ensure the `.editorconfig` file is located in the exact project root.
* **Check `root = true**`: Ensure this is set to prevent settings from parent directories from interfering.
* **Reload**: Try closing and reopening the file after modifying the configuration.

---

## 7. References

* [EditorConfig Official Site](https://editorconfig.org/)
* [EditorConfig Wiki](https://github.com/editorconfig/editorconfig/wiki)
* [List of Supported Editors](https://editorconfig.org/#pre-installed)

---
