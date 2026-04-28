# Modern Makefile for nanox

PROGRAM = nanox
LINK_NAME = nx

# Build directory
BUILD_DIR = build

# Modules
MODULES = core commands io platform utils features tui

# Compiler and flags
CC ?= gcc
CFLAGS = -std=c2x -O2 -g \
         -Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wconversion \
         -fstack-protector-strong -fno-common -ffunction-sections -fdata-sections \
         -Iinclude \
         $(foreach mod,$(MODULES),-I$(mod)) \
         -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -DPOSIX -D_GNU_SOURCE

# Feature Flags
USE_NCURSES ?= 1
ifeq ($(USE_NCURSES),1)
    CFLAGS += -DUSE_NCURSES
endif

# Libraries discovery
LIBS = ncursesw pcre2-8 pthread
LDLIBS = $(shell pkg-config --libs $(LIBS) 2>/dev/null || echo -lncursesw -lpcre2-8 -lpthread) -llz4 -lm

# Hunspell support
HUNSPELL_CFLAGS := $(shell pkg-config --cflags hunspell 2>/dev/null)
HUNSPELL_LIBS := $(shell pkg-config --libs hunspell 2>/dev/null)
ifneq ($(strip $(HUNSPELL_LIBS)),)
    CFLAGS += -DHAVE_HUNSPELL $(HUNSPELL_CFLAGS)
    LDLIBS += $(HUNSPELL_LIBS)
endif

# Linker flags
LDFLAGS = -flto=auto -fuse-linker-plugin -Wl,--gc-sections

# Source discovery
# We explicitly list some to maintain control, or use wildcard and filter
SRC = $(foreach mod,$(MODULES),$(wildcard $(mod)/*.c))

# If not using ncurses, exclude ncurses.c
ifneq ($(USE_NCURSES),1)
    SRC := $(filter-out tui/ncurses.c,$(SRC))
endif

# Always exclude orig_display.c
SRC := $(filter-out tui/orig_display.c,$(SRC))

OBJ = $(SRC:%.c=$(BUILD_DIR)/%.o)
DEP = $(OBJ:.o=.d)

# Silent build
V ?= 0
ifeq ($(V),0)
    E = @echo
    Q = @
else
    E = @#
    Q =
endif

.PHONY: all clean install configs-install install-all

all: $(PROGRAM)

$(PROGRAM): $(OBJ)
	$(E) "  LINK    " $@
	$(Q) $(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	$(E) "  CC      " $<
	$(Q) mkdir -p $(dir $@)
	$(Q) $(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

clean:
	$(E) "  CLEAN"
	$(Q) rm -rf $(BUILD_DIR) $(PROGRAM)

# Installation
PREFIX ?= /usr/local
DESTDIR ?=
INSTALL_BIN = $(DESTDIR)$(PREFIX)/bin
XDG_CONFIG_HOME ?= $(HOME)/.config
INSTALL_CONF = $(DESTDIR)$(XDG_CONFIG_HOME)/nanox

install: $(PROGRAM)
	$(E) "  INSTALL " $(PROGRAM) " -> " $(INSTALL_BIN)
	$(Q) install -d "$(INSTALL_BIN)"
	$(Q) install -m 755 "$(PROGRAM)" "$(INSTALL_BIN)/$(PROGRAM)"
	$(E) "  LINK    " "$(LINK_NAME) -> $(PROGRAM)"
	$(Q) ln -sf "$(PROGRAM)" "$(INSTALL_BIN)/$(LINK_NAME)"

configs-install:
	$(E) "  CONFIG  " "configs/nanox -> " $(INSTALL_CONF)
	$(Q) install -d "$(INSTALL_CONF)"
	$(Q) cp -r configs/nanox/* "$(INSTALL_CONF)/"
	$(E) "  HELP    " "assets/emacs*.hlp -> " $(INSTALL_CONF)
	$(Q) cp assets/emacs*.hlp "$(INSTALL_CONF)/" 2>/dev/null || true
	$(E) "  RC      " "assets/emacs.rc -> " $(INSTALL_CONF)
	$(Q) cp assets/emacs.rc "$(INSTALL_CONF)/" 2>/dev/null || true

install-all: install configs-install
