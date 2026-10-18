# Build on the Pi: `make` produces ./bc101 and the fonts it needs.
# From a workstation use scripts/deploy.sh, which syncs and runs this remotely.
CFLAGS  ?= -O2 -Wall
LDLIBS  := -lm
FONTSRC := /usr/share/consolefonts

# Uploaded visuals live outside the repo (visuals/, see tools/tv-dispatch); their table is generated.
VISUALS ?= visuals
VSRCS := $(sort $(wildcard $(VISUALS)/*.c))
SRCS := $(filter-out src/tvctl.c,$(wildcard src/*.c src/scenes/*.c)) $(VSRCS)
OBJS := $(SRCS:%.c=build/%.o) build/visuals.o

all: bc101 tvctl fonts/big.psf fonts/small.psf

# A failed recipe (e.g. zcat on a corrupt .gz) must not leave a half-written target.
.DELETE_ON_ERROR:

# Every target is written to a temporary name and renamed: a crash mid-write then leaves a .tmp
# behind instead of a truncated file with a fresh mtime that make takes as current (2026-10-17:
# a reboot during make left a 0-byte crt.o and the link failed with an undefined reference).
bc101: $(OBJS)
	$(CC) -o $@.tmp $^ $(LDLIBS) && mv $@.tmp $@

# The remote's client (control socket and zapper), no scenes: uploads cannot break it.
tvctl: build/src/tvctl.o build/src/ctl.o build/src/remote.o
	$(CC) -o $@.tmp $^ $(LDLIBS) && mv $@.tmp $@

build/%.o: %.c src/core.h src/scenes.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@.tmp $< && mv $@.tmp $@

# Regenerated on every run, but only replaced when it changed, so nothing relinks needlessly;
# a removed visual (or a removed directory) therefore drops out of the table as well.
build/visuals.c: tools/gen-visuals.sh FORCE
	@mkdir -p build
	@LC_ALL=C sh tools/gen-visuals.sh $(VISUALS) > $@.tmp && { cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@; }
FORCE:

build/visuals.o: build/visuals.c src/core.h src/scenes.h Makefile
	$(CC) $(CFLAGS) -Isrc -c -o $@.tmp $< && mv $@.tmp $@

# Offscreen renderer and benchmark, see tools/preview.c; builds on the Mac too (no fbdev).
preview: build/preview

build/preview: build/tools/preview.o $(filter-out build/src/main.o build/src/ctl.o build/src/remote.o,$(OBJS))
	$(CC) -o $@.tmp $^ $(LDLIBS) && mv $@.tmp $@

fonts/big.psf: $(FONTSRC)/Lat15-TerminusBold32x16.psf.gz Makefile
	@mkdir -p fonts
	zcat $< > $@.tmp && mv $@.tmp $@

fonts/small.psf: $(FONTSRC)/Lat15-TerminusBold16.psf.gz Makefile
	@mkdir -p fonts
	zcat $< > $@.tmp && mv $@.tmp $@

clean:
	rm -rf build bc101 tvctl fonts

.PHONY: all clean preview FORCE

# Website (GitHub Pages), needs emcc, pandoc, typst: see web/site.mk. Optional on purpose:
# the Pi only receives src, slides, system, tools and this Makefile (scripts/deploy.sh).
-include web/site.mk
