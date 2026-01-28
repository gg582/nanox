#!/bin/bash
# Generic Build and Install script for nanox
# This script attempts to detect the package manager, install dependencies,
# build the project, and install it.

set -e

echo "Starting nanox build and install process..."

# 1. Detect Package Manager and Install Dependencies
if [ -f /etc/debian_version ]; then
    echo "Detected Debian/Ubuntu based system."
    if [ -f scripts/debian/install_deps.sh ]; then
        sudo ./scripts/debian/install_deps.sh
    elif [ -f scripts/ubuntu/install_deps.sh ]; then
        sudo ./scripts/ubuntu/install_deps.sh
    else
        echo "Installing basic dependencies via apt..."
        sudo apt-get update
        sudo apt-get install -y build-essential pkg-config libncurses-dev libhunspell-dev libpcre2-dev
    fi
elif [ -f /etc/fedora-release ]; then
    echo "Detected Fedora based system."
    if [ -f scripts/fedora/install_deps.sh ]; then
        sudo ./scripts/fedora/install_deps.sh
    else
        sudo dnf install -y gcc make pkgconf-pkg-config ncurses-devel hunspell-devel pcre2-devel
    fi
elif [ -f /etc/redhat-release ]; then
    echo "Detected RHEL based system."
    if [ -f scripts/rhel/install_deps.sh ]; then
        sudo ./scripts/rhel/install_deps.sh
    else
        sudo dnf install -y gcc make pkgconf-pkg-config ncurses-devel hunspell-devel pcre2-devel || \
        sudo yum install -y gcc make pkgconf-pkg-config ncurses-devel hunspell-devel pcre2-devel
    fi
elif [ -f /etc/arch-release ]; then
    echo "Detected Arch Linux based system."
    if [ -f scripts/arch/install_deps.sh ]; then
        sudo ./scripts/arch/install_deps.sh
    else
        sudo pacman -Syu --needed --noconfirm base-devel ncurses hunspell pcre2
    fi
elif [ -f /etc/os-release ] && grep -q "opensuse" /etc/os-release;
 then
    echo "Detected openSUSE based system."
    if [ -f scripts/opensuse/install_deps.sh ]; then
        sudo ./scripts/opensuse/install_deps.sh
    else
        sudo zypper install -y gcc make pkg-config ncurses-devel hunspell-devel pcre2-devel
    fi
else
    echo "Unknown or minor distribution detected."
    echo "Attempting to find common tools..."
    
    # Try to detect available package managers for minor distros
    if command -v zypper >/dev/null 2>&1; then
        sudo zypper install -y gcc make pkg-config ncurses-devel hunspell-devel pcre2-devel
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y gcc make pkgconf-pkg-config ncurses-devel hunspell-devel pcre2-devel
    elif command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y build-essential pkg-config libncurses-dev libhunspell-dev libpcre2-dev
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --needed --noconfirm base-devel ncurses hunspell pcre2
    elif command -v apk >/dev/null 2>&1; then
        sudo apk add build-base ncurses-dev hunspell-dev pcre2-dev pkgconfig
    else
        echo "Could not find a supported package manager."
        echo "Please install build-essential, ncurses-dev, hunspell-dev, and pcre2-dev manually."
    fi
fi

# 2. Build nanox
echo "Building nanox..."
make clean
make -j$(nproc)

# 3. Install nanox
echo "Installing nanox..."
sudo make install-all

echo "nanox has been successfully built and installed!"
echo "You can run it by typing 'nanox' or 'nx'."
