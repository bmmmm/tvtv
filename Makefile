# Build on the Pi: `make` produces ./bc101 and the fonts it needs.
# From a workstation use scripts/deploy.sh, which syncs and runs this remotely.
CFLAGS  ?= -O2 -Wall
LDLIBS  := -lm
FONTSRC := /usr/share/consolefonts

SRCS := $(wildcard src/*.c src/scenes/*.c)
OBJS := $(SRCS:%.c=build/%.o)

all: bc101 fonts/big.psf fonts/small.psf

# A failed recipe (e.g. zcat on a corrupt .gz) must not leave a half-written target.
.DELETE_ON_ERROR:

# Every target is written to a temporary name and renamed: a crash mid-write then leaves a .tmp
# behind instead of a truncated file with a fresh mtime that make takes as current (2026-10-17:
# a reboot during make left a 0-byte crt.o and the link failed with an undefined reference).
bc101: $(OBJS)
	$(CC) -o $@.tmp $^ $(LDLIBS) && mv $@.tmp $@

build/%.o: %.c src/core.h src/scenes.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@.tmp $< && mv $@.tmp $@

# Offscreen renderer and benchmark, see tools/preview.c; builds on the Mac too (no fbdev).
preview: build/preview

build/preview: build/tools/preview.o $(filter-out build/src/main.o,$(OBJS))
	$(CC) -o $@.tmp $^ $(LDLIBS) && mv $@.tmp $@

fonts/big.psf: $(FONTSRC)/Lat15-TerminusBold32x16.psf.gz Makefile
	@mkdir -p fonts
	zcat $< > $@.tmp && mv $@.tmp $@

fonts/small.psf: $(FONTSRC)/Lat15-TerminusBold16.psf.gz Makefile
	@mkdir -p fonts
	zcat $< > $@.tmp && mv $@.tmp $@

clean:
	rm -rf build bc101 fonts

.PHONY: all clean preview

# Website (GitHub Pages), needs emcc, pandoc, typst: see web/site.mk. Optional, because
# deploy.sh does not send web/ to the Pi and `make` there must not die on a missing file.
-include web/site.mk
