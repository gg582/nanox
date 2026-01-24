# makefile for emacs, updated Sun Apr 28 17:59:07 EET DST 1996
#
# Notes
# - Keep the auto-generated file lists (SRC/OBJ/HDR) intact. `make source` rewrites them.
# - Installation is split:
#   - `make install` installs the program binary only (may require privileges depending on PREFIX).
#   - `make configs-install` installs user configuration files to the user's config directory.
#   - `make install-all` runs both in a predictable order.
# - Do not hard-code privilege escalation in Makefile targets. Use `sudo make install`
#   when installing into system directories (e.g. /usr/local).

# Make the build silent by default
V =

ifeq ($(strip $(V)),)
	E = @echo
	Q = @
else
	E = @\#
	Q =
endif
export E Q

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

PROGRAM=nanox

SRC=	basic.c bind.c buffer.c colorscheme.c display.c eval.c exec.c file.c \
	fileio.c highlight.c input.c isearch.c line.c lock.c globals.c main.c \
	names.c nanox.c pklock.c platform.c posix.c random.c region.c search.c \
	spawn.c tcap.c usage.c utf8.c version.c window.c word.c wrapper.c

OBJ=	basic.o bind.o buffer.o colorscheme.o display.o eval.o exec.o file.o \
	fileio.o highlight.o input.o isearch.o line.o lock.o globals.o main.o \
	names.o nanox.o pklock.o platform.o posix.o random.o region.o search.o \
	spawn.o tcap.o usage.o utf8.o version.o window.o word.o wrapper.o

HDR=	ebind.h edef.h efunc.h epath.h estruct.h evar.h line.h usage.h \
	utf8.h util.h version.h wrapper.h nanox.h

# DO NOT ADD OR MODIFY ANY LINES ABOVE THIS -- make source creates them

CC=gcc
WARNINGS=-Wall -Wstrict-prototypes
DEFINES=-DPOSIX -D_GNU_SOURCE

CFLAGS=-Ofast $(WARNINGS) $(DEFINES)

LIBS=ncurses hunspell
BINDIR=$(HOME)/bin
LIBDIR=$(HOME)/lib

CFLAGS += $(shell pkg-config --cflags $(LIBS))
LDLIBS += $(shell pkg-config --libs $(LIBS))

$(PROGRAM): $(OBJ)
	$(E) "  LINK    " $@
	$(Q) $(CC) $(LDFLAGS) $(DEFINES) -o $@ $(OBJ) $(LDLIBS)

.c.o:
	$(E) "  CC      " $@
	$(Q) ${CC} ${CFLAGS} -c $<

clean:
	$(E) "  CLEAN"
	$(Q) rm -f $(PROGRAM) core lintout makeout tags makefile.bak *.o

# -----------------------------------------------------------------------------
# Install configuration
#
# PREFIX:
#   - Default install prefix for the program binary.
#   - Typical system install uses PREFIX=/usr/local with `sudo make install`.
#   - For per-user install, use PREFIX=$(HOME)/.local.
#
# Config installation:
#   - User configuration belongs in XDG_CONFIG_HOME when available.
#   - Default fallback is $(HOME)/.config.
#   - Config installation is separated into `configs-install`.
# -----------------------------------------------------------------------------

PREFIX ?= /usr/local
DESTDIR ?=

# Default install path for the executable.
INSTALL_BIN = $(DESTDIR)$(PREFIX)/bin

# User config directory (XDG base directory spec fallback).
XDG_CONFIG_HOME ?= $(HOME)/.config
INSTALL_CONF = $(DESTDIR)$(XDG_CONFIG_HOME)/nanox

PROG_EXT =

# Adjust for Windows (MinGW/MSYS/Cygwin).
ifneq (,$(findstring MINGW,$(uname_S)))
	PROG_EXT = .exe
	# Avoid system prefixes by default on MSYS/MinGW unless PREFIX is explicitly set.
	ifeq ($(PREFIX),/usr/local)
		INSTALL_BIN = $(DESTDIR)$(HOME)/bin
	endif
endif
ifneq (,$(findstring CYGWIN,$(uname_S)))
	PROG_EXT = .exe
endif

# -----------------------------------------------------------------------------
# Install targets
#
# - `install` installs only the binary to $(INSTALL_BIN).
# - `configs-install` installs configs/nanox/* into $(INSTALL_CONF).
# - `install-all` runs both.
# -----------------------------------------------------------------------------

install: $(PROGRAM)
	$(E) "  INSTALL " $(PROGRAM) " -> " $(INSTALL_BIN)
	$(Q) install -d "$(INSTALL_BIN)"
	$(Q) install -m 755 "$(PROGRAM)$(PROG_EXT)" "$(INSTALL_BIN)/$(PROGRAM)$(PROG_EXT)"

configs-install:
	$(E) "  CONFIG  " "configs/nanox -> " $(INSTALL_CONF)
	$(Q) install -d "$(INSTALL_CONF)"
	$(Q) find configs/nanox -type f | while read f; do \
		rel=$${f#configs/nanox/}; \
		dir=$(INSTALL_CONF)/$$(dirname $$rel); \
		install -d "$$dir"; \
		if [ -f "$(INSTALL_CONF)/$$rel" ]; then \
			cp "$(INSTALL_CONF)/$$rel" "$(INSTALL_CONF)/$$rel.bak"; \
		fi; \
		cp "$$f" "$(INSTALL_CONF)/$$rel"; \
	done

backups-clean:
	$(E) "  CLEAN BACKUPS"
	$(Q) find "$(INSTALL_CONF)" -name "*.bak" -delete

install-all: install configs-install

source:
	@mv makefile makefile.bak
	@echo "# makefile for emacs, updated `date`" >makefile
	@echo '' >>makefile
	@echo SRC=`ls *.c` >>makefile
	@echo OBJ=`ls *.c | sed s/c$$/o/` >>makefile
	@echo HDR=`ls *.h` >>makefile
	@echo '' >>makefile
	@sed -n -e '/^# DO NOT ADD OR MODIFY/,$$p' <makefile.bak >>makefile

depend: ${SRC}
	@for i in ${SRC}; do $(CC) ${DEFINES} -MM $$i; done >makedep
	@echo '/^# DO NOT DELETE THIS LINE/+2,$$d' >eddep
	@echo '$$r ./makedep' >>eddep
	@echo 'w' >>eddep
	@cp makefile makefile.bak
	@ed - makefile <eddep
	@rm eddep makedep
	@echo '' >>makefile
	@echo '# DEPENDENCIES MUST END AT END OF FILE' >>makefile
	@echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >>makefile
	@echo '# see make depend above' >>makefile

# DO NOT DELETE THIS LINE -- make depend uses it

basic.o: basic.c estruct.h edef.h efunc.h line.h utf8.h
bind.o: bind.c estruct.h edef.h efunc.h epath.h line.h utf8.h util.h
buffer.o: buffer.c estruct.h edef.h efunc.h line.h utf8.h
display.o: display.c estruct.h edef.h efunc.h line.h utf8.h version.h wrapper.h
eval.o: eval.c estruct.h edef.h efunc.h evar.h line.h utf8.h util.h version.h
exec.o: exec.c estruct.h edef.h efunc.h line.h utf8.h
file.o: file.c estruct.h edef.h efunc.h line.h utf8.h util.h
fileio.o: fileio.c estruct.h edef.h efunc.h
input.o: input.c estruct.h edef.h efunc.h wrapper.h
isearch.o: isearch.c estruct.h edef.h efunc.h line.h utf8.h
line.o: line.c line.h utf8.h estruct.h edef.h efunc.h
lock.o: lock.c estruct.h edef.h efunc.h
main.o: main.c estruct.h edef.h efunc.h ebind.h line.h utf8.h version.h
pklock.o: pklock.c estruct.h edef.h efunc.h
posix.o: posix.c estruct.h edef.h efunc.h utf8.h
random.o: random.c estruct.h edef.h efunc.h line.h utf8.h
region.o: region.c estruct.h edef.h efunc.h line.h utf8.h
search.o: search.c estruct.h edef.h efunc.h line.h utf8.h
spawn.o: spawn.c estruct.h edef.h efunc.h
tcap.o: tcap.c estruct.h edef.h efunc.h
window.o: window.c estruct.h edef.h efunc.h line.h utf8.h wrapper.h
word.o: word.c estruct.h edef.h efunc.h line.h utf8.h
names.o: names.c estruct.h edef.h efunc.h line.h utf8.h
globals.o: globals.c estruct.h edef.h
version.o: version.c version.h
usage.o: usage.c usage.h
wrapper.o: wrapper.c usage.h
utf8.o: utf8.c utf8.h
nanox.o: nanox.c nanox.h estruct.h edef.h efunc.h line.h util.h version.h

# DEPENDENCIES MUST END AT END OF FILE
# IF YOU PUT STUFF HERE IT WILL GO AWAY
# see make depend above
