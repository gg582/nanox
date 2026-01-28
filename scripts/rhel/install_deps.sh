#!/bin/bash
# RHEL/CentOS/Alma/Rocky Build Dependency Installer for nanox (Updated for 2026)

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (or use sudo)"
  exit 1
fi

# Detect RHEL version
if [ -f /etc/redhat-release ]; then
    VERSION=$(cat /etc/redhat-release | grep -oE '[0-9]+' | head -n1)
else
    echo "This script is intended for RHEL-based systems."
    exit 1
fi

echo "Detected RHEL version: $VERSION"

# Common dependencies
DEPS="gcc make pkgconf-pkg-config ncurses-devel hunspell-devel pcre2-devel"

case "$VERSION" in
    "10") # Latest Stable (Released Nov 2025)
        echo "Installing for RHEL 10 (Stable)..."
        ;;
    "9") # Previous Stable
        echo "Installing for RHEL 9 (Supported)..."
        ;;
    "8") # Old Stable
        echo "Installing for RHEL 8..."
        ;;
    "7") # Legacy
        echo "Installing for RHEL 7..."
        if ! yum list installed epel-release >/dev/null 2>&1; then
            yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
        fi
        ;;
    *)
        echo "Warning: Unsupported or unknown RHEL version. Attempting default installation."
        ;;
esac

if command -v dnf >/dev/null 2>&1; then
    dnf install -y $DEPS
else
    yum install -y $DEPS
fi

echo "Done!"