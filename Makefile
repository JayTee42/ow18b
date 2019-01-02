# Compiler and linker
CC=gcc
LD=gcc

# Directories
INCLDIR=include
SRCDIR=src
BUILDDIR=build

# Binary
BIN=ow18b

# Source files
SRC=$(wildcard $(SRCDIR)/*.c)

# Compiler
CFLAGS=-c -std=gnu99 -march=native \
          -I$(INCLDIR) \
          -Wall -Wextra -Wvla -Wmissing-prototypes

# Linker
LDLIBS=-lbluetooth

# Debug
DBGDIR=$(BUILDDIR)/debug
DBGOBJ=$(SRC:$(SRCDIR)/%.c=$(DBGDIR)/%.o)
DBGCFLAGS=-g -O0 -DDEBUG_BUILD
DBGBIN=$(DBGDIR)/$(BIN)

# Release
RELDIR=$(BUILDDIR)/release
RELOBJ=$(SRC:$(SRCDIR)/%.c=$(RELDIR)/%.o)
RELCFLAGS=-O3
RELBIN=$(RELDIR)/$(BIN)

.PHONY: all clean prep debug release

all: release

clean:
	rm -rf $(BUILDDIR)
	mkdir -p $(DBGDIR) $(RELDIR)

prep:
	mkdir -p $(DBGDIR) $(RELDIR)

# Debug
$(DBGDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(DBGCFLAGS) -o $@ $<

$(DBGBIN): $(DBGOBJ)
	$(LD) -o $@ $^ $(LDLIBS)

debug: prep $(DBGBIN)

# Release
$(RELDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(RELCFLAGS) -o $@ $<

$(RELBIN): $(RELOBJ)
	$(LD) -o $@ $^ $(LDLIBS)

release: prep $(RELBIN)
