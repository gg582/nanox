## Overview

This document lists commonly used Language Server Protocol (LSP) servers and their installation methods across major UNIX-like operating systems.

Detection in nanox is based on the presence of executable binaries in `$PATH`.

Supported servers:

* clangd
* pylsp / pyright-langserver
* typescript-language-server
* gopls
* rust-analyzer
* jdtls

---

## Completion UI / LSP integration behavior

When `autocomplete=true`, nanox now shows UTF-8 safe completion candidates in a popup mini-buffer below the cursor.

Key flow:

* `Tab` once: keep typing context and keep the popup open.
* `Tab` twice: enter popup selection mode.
* `Up/Down`: move selection.
* `Enter`: commit selected completion.
* `Left/Right`: leave popup selection mode and return to normal cursor movement.
* Popup foreground/background/selection colors follow the active colorscheme highlight styles (`normal`, `selection`, `notice`).

LSP-aware mode (`use_lsp=true`) augments completion sources when a matching server binary is found in `$PATH`.
In addition, source-symbol and language-specific symbol extraction is always used so completion remains useful even without an installed server.

---

## clangd (C / C++)

### Binary name

`clangd`

### Ubuntu / Debian

```bash
sudo apt install clangd
```

### RHEL / Fedora

```bash
sudo dnf install clang-tools-extra
```

### Arch

```bash
sudo pacman -S clang
```

### openSUSE

```bash
sudo zypper install clang-tools
```

### Slackware

* Not official
* Use SlackBuilds or manual LLVM build

### FreeBSD

```bash
pkg install llvm
```

### OpenBSD

```bash
pkg_add llvm
```

### NetBSD

```bash
pkgin install llvm
```

---

## pylsp (Python - python-lsp-server)

### Binary name

`pylsp`

### All Linux / BSD

```bash
pip install python-lsp-server
```

Optional plugins:

```bash
pip install pylsp-mypy pylsp-rope pylsp-black
```

---

## pyright (Python alternative)

### Binary name

`pyright-langserver`

### All systems (via npm)

```bash
npm install -g pyright
```

---

## typescript-language-server

### Binary name

`typescript-language-server`

### All systems

```bash
npm install -g typescript typescript-language-server
```

---

## gopls (Go)

### Binary name

`gopls`

### All systems

```bash
go install golang.org/x/tools/gopls@latest
```

Ensure:

```bash
export PATH=$PATH:$(go env GOPATH)/bin
```

---

## rust-analyzer (Rust)

### Binary name

`rust-analyzer`

### Ubuntu / Debian

```bash
sudo apt install rust-analyzer
```

### RHEL / Fedora

```bash
sudo dnf install rust-analyzer
```

### Arch

```bash
sudo pacman -S rust-analyzer
```

### openSUSE

```bash
sudo zypper install rust-analyzer
```

### Slackware

* Use rustup or manual build

### FreeBSD

```bash
pkg install rust-analyzer
```

### OpenBSD

```bash
pkg_add rust-analyzer
```

### NetBSD

```bash
pkgin install rust-analyzer
```

---

## jdtls (Java)

### Binary name

`jdtls`

### Ubuntu / Debian

```bash
sudo apt install jdtls
```

### RHEL / Fedora

```bash
sudo dnf install jdtls
```

### Arch

```bash
sudo pacman -S jdtls
```

### openSUSE

```bash
sudo zypper install jdtls
```

### Slackware

* Manual install from Eclipse JDTLS release

### FreeBSD

```bash
pkg install jdtls
```

### OpenBSD / NetBSD

* Usually manual install required

---

## Detection Strategy

nanox should detect LSP availability using:

```bash
which <binary>
```

Examples:

```bash
which clangd
which pylsp
which pyright-langserver
which typescript-language-server
which gopls
which rust-analyzer
which jdtls
```

If binary exists and `use_lsp=true`, enable LSP integration.

---

## Notes

* npm-based servers require Node.js installed
* pip-based servers require Python environment consistency
* Go-based servers depend on GOPATH/bin
* BSD systems may lag behind Linux in package freshness
* Slackware typically requires manual builds or SlackBuilds
