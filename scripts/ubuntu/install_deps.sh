#!/bin/bash
# Ubuntu Build Dependency Installer for nanox (Updated for 2026)

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (or use sudo)"
  exit 1
fi

# Detect Ubuntu version
if [ -f /etc/os-release ]; then
    . /etc/os-release
    VERSION=$VERSION_ID
else
    echo "This script is intended for Ubuntu."
    exit 1
fi

echo "Detected Ubuntu version: $VERSION"

# Common dependencies
DEPS="build-essential pkg-config libncurses-dev libhunspell-dev libpcre2-dev"

case "$VERSION" in
    "24.04") # Noble Numbat (LTS Stable)
        echo "Installing for Ubuntu 24.04 LTS (Stable)..."
        ;;
    "25.10") # Questing Quokka (Latest Stable)
        echo "Installing for Ubuntu 25.10..."
        ;;
    "26.04") # Upcoming LTS (Testing/Development)
        echo "Installing for Ubuntu 26.04 (Development)..."
        ;;
    "22.04") # Jammy Jellyfish (Old LTS)
        echo "Installing for Ubuntu 22.04 LTS (Oldstable)..."
        ;;
    "20.04") # Focal Fossa (Old LTS)
        echo "Installing for Ubuntu 20.04 LTS..."
        ;;
    *)
        echo "Warning: Unsupported or unknown Ubuntu version. Attempting default installation."
        ;;
esac

apt-get update
apt-get install -y $DEPS

echo "Done!"