#!/bin/bash
# OpenSUSE Build Dependency Installer for nanox (Updated for 2026)

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (or use sudo)"
  exit 1
fi

# Detect OpenSUSE version
if [ -f /etc/os-release ]; then
    . /etc/os-release
    VERSION=$VERSION_ID
    NAME=$NAME
else
    echo "This script is intended for OpenSUSE."
    exit 1
fi

echo "Detected OpenSUSE: $NAME $VERSION"

# Common dependencies
DEPS="gcc make pkg-config ncurses-devel hunspell-devel pcre2-devel"

if [[ "$VERSION" == *"Tumbleweed"* ]] || [[ "$NAME" == *"Tumbleweed"* ]]; then
    echo "Installing for OpenSUSE Tumbleweed (Rolling)..."
elif [[ "$VERSION" == "16.0" ]] || [[ "$VERSION" == "16" ]]; then
    echo "Installing for OpenSUSE Leap 16 (Stable)..."
elif [[ "$VERSION" == "15.6" ]]; then
    echo "Installing for OpenSUSE Leap 15.6 (Oldstable)..."
elif [[ "$VERSION" == "15.5" ]]; then
    echo "Installing for OpenSUSE Leap 15.5..."
else
    echo "Warning: Unsupported or unknown OpenSUSE version. Attempting default installation."
fi

zypper install -y $DEPS

echo "Done!"