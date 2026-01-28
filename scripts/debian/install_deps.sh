#!/bin/bash
# Debian Build Dependency Installer for nanox (Updated for 2026)

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (or use sudo)"
  exit 1
fi

# Detect Debian version
if [ -f /etc/debian_version ]; then
    VERSION_ID=$(cat /etc/debian_version | cut -d'.' -f1)
else
    echo "This script is intended for Debian."
    exit 1
fi

echo "Detected Debian version: $VERSION_ID"

# Common dependencies
DEPS="build-essential pkg-config libncurses-dev libhunspell-dev libpcre2-dev"

case "$VERSION_ID" in
    "13") # Stable (Trixie) - Released Aug 2025
        echo "Installing for Debian 13 (Stable)..."
        ;;
    "14"|"forky") # Testing (Forky)
        echo "Installing for Debian 14 (Testing)..."
        ;;
    "sid") # Unstable
        echo "Installing for Debian Sid (Unstable)..."
        ;;
    "12") # Oldstable (Bookworm)
        echo "Installing for Debian 12 (Oldstable)..."
        ;;
    "11") # Oldoldstable (Bullseye)
        echo "Installing for Debian 11 (Oldoldstable)..."
        ;;
    *)
        echo "Warning: Unsupported or unknown Debian version. Attempting default installation."
        ;;
esac

apt-get update
apt-get install -y $DEPS

echo "Done!"