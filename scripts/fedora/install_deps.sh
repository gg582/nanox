#!/bin/bash
# Fedora Build Dependency Installer for nanox (Updated for 2026)

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (or use sudo)"
  exit 1
fi

# Detect Fedora version
if [ -f /etc/fedora-release ]; then
    VERSION=$(cat /etc/fedora-release | awk '{print $3}')
else
    echo "This script is intended for Fedora."
    exit 1
fi

echo "Detected Fedora version: $VERSION"

# Common dependencies
DEPS="gcc make pkgconf-pkg-config ncurses-devel hunspell-devel pcre2-devel"

case "$VERSION" in
    "42") # Stable
        echo "Installing for Fedora 42 (Stable)..."
        ;;
    "43"|"44"|"rawhide") # Testing/Rawhide
        echo "Installing for Fedora $VERSION (Testing/Rawhide)..."
        ;;
    "41") # Oldstable
        echo "Installing for Fedora 41 (Oldstable)..."
        ;;
    "40") # Old
        echo "Installing for Fedora 40..."
        ;;
    *)
        echo "Warning: Unsupported or unknown Fedora version. Attempting default installation."
        ;;
esac

dnf install -y $DEPS

echo "Done!"