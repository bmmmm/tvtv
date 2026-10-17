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

bc101: $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

build/%.o: %.c src/core.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

fonts/big.psf: $(FONTSRC)/Lat15-TerminusBold32x16.psf.gz Makefile
	@mkdir -p fonts
	zcat $< > $@

fonts/small.psf: $(FONTSRC)/Lat15-TerminusBold16.psf.gz Makefile
	@mkdir -p fonts
	zcat $< > $@

clean:
	rm -rf build bc101 fonts

.PHONY: all clean
