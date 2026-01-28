#!/bin/bash
# Arch Linux Build Dependency Installer for nanox

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (or use sudo)"
  exit 1
fi

echo "Detected Arch Linux"

# Arch is rolling, but we can check if testing repos are enabled
REPOS=$(pacman -Qkk 2>/dev/null | grep -c "testing")

if [ "$REPOS" -gt 0 ]; then
    echo "Testing repositories detected."
fi

# Common dependencies
DEPS="base-devel ncurses hunspell pcre2"

pacman -Syu --needed --noconfirm $DEPS

echo "Done!"
