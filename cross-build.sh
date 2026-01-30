#!/bin/bash

# This script provides a unified interface for cross-compiling the nanox project
# across multiple hardware architectures. It automates toolchain selection,
# sysroot isolation, and dependency verification.

if [ -z "$1" ]; then
    echo "Usage: ./cross-build.sh [aarch64|armhf|i386|riscv64]"
    exit 1
fi

# Map the input argument to the corresponding GNU triplet and required dev packages.
# This ensures we use the correct cross-compiler and architecture-specific pkg-config.
case "$1" in
    aarch64)
        TARGET_TRIPLET="aarch64-linux-gnu"
        ARCH_SUFFIX="arm64"
        ;;
    armhf)
        TARGET_TRIPLET="arm-linux-gnueabihf"
        ARCH_SUFFIX="armhf"
        ;;
    i386)
        TARGET_TRIPLET="i686-linux-gnu"
        ARCH_SUFFIX="i386"
        ;;
    riscv64)
        TARGET_TRIPLET="riscv64-linux-gnu"
        ARCH_SUFFIX="riscv64"
        ;;
    *)
        echo "Unsupported architecture: $1"
        exit 1
        ;;
esac

CC="${TARGET_TRIPLET}-gcc"
STRIP="${TARGET_TRIPLET}-strip"
PKG_CONFIG_PATH="/usr/lib/${TARGET_TRIPLET}/pkgconfig"
export PKG_CONFIG_PATH

# Isolate build artifacts by architecture to prevent object file pollution
# when switching between different cross-compilation targets.
BUILD_DIR="build_${1}"
mkdir -p "$BUILD_DIR"

echo "--- Starting Cross-Build for ${1} (${TARGET_TRIPLET}) ---"

# Verify that the multi-arch development headers are installed on the host.
# Without these, the linker will fail to resolve ncurses or hunspell symbols.
REQUIRED_LIBS=("libncurses-dev:${ARCH_SUFFIX}" "libhunspell-dev:${ARCH_SUFFIX}" "libpcre2-dev:${ARCH_SUFFIX}")

for lib in "${REQUIRED_LIBS[@]}"; do
    if ! dpkg -l | grep -q "$lib"; then
        echo "Error: $lib is not installed."
        echo "Run: sudo apt install $lib"
        exit 1
    fi
done

# Resolve preprocessor and linker flags from the target's pkg-config path.
# We explicitly append pcre2-8 to align with the nanox Makefile requirements.
TARGET_CFLAGS=$(pkg-config --cflags ncurses hunspell)
TARGET_LDLIBS=$(pkg-config --libs ncurses hunspell)
TARGET_LDLIBS="$TARGET_LDLIBS -lpcre2-8"

# Clean the previous build state and invoke make with the cross-toolchain.
# We use --gc-sections to strip unused code, keeping the binary optimized for size.
make clean
make -j$(nproc) \
    CC="$CC" \
    CFLAGS="-Os -ffunction-sections -fdata-sections -DPOSIX -D_GNU_SOURCE $TARGET_CFLAGS" \
    LDLIBS="$TARGET_LDLIBS" \
    LDFLAGS="-Wl,--gc-sections" \
    WERROR=0

# Move the resulting ELF binary to the architecture-specific build directory
# and strip its symbol table to minimize the deployment footprint.
if [ $? -eq 0 ]; then
    echo "--- Build Successful! ---"
    mv nanox "${BUILD_DIR}/nanox_${1}"
    $STRIP "${BUILD_DIR}/nanox_${1}"
    file "${BUILD_DIR}/nanox_${1}"
else
    echo "--- Build Failed ---"
    exit 1
fi
