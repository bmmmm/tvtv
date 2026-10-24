# Build on the Pi: `make` produces ./bc101 and the fonts it needs.
# From a workstation use scripts/deploy.sh, which syncs and runs this remotely.
CFLAGS  ?= -O2 -Wall
LDLIBS  := -lm
FONTSRC := /usr/share/consolefonts

# Uploaded visuals live outside the repo (visuals/, see tools/tv-dispatch); their table is generated.
# The show's own names are never a visual's (one uploaded before a name was taken would clash with
# the built-in scene_<name>); tools/gen-visuals.sh skips them too.
VISUALS ?= visuals
RESERVED := pixel dem0 text
VSRCS := $(filter-out $(RESERVED:%=$(VISUALS)/%.c),$(sort $(wildcard $(VISUALS)/*.c)))
SRCS := $(filter-out src/tvctl.c,$(wildcard src/*.c src/scenes/*.c)) $(VSRCS)
OBJS := $(SRCS:%.c=build/%.o) build/visuals.o

all: bc101 tvctl fonts/big.psf fonts/small.psf $(if $(wildcard third_party/fbdoom),fbdoom)

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

build/%.o: %.c src/core.h src/scenes.h src/social.h src/dem0.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@.tmp $< && mv $@.tmp $@

# dem0 is float32 to the bit (docs/remote.md, "Social channels"): no fused multiply-add, which
# rounds once where dem0 rounds twice, on a CPU that has one.
build/src/dem0.o: override CFLAGS += -ffp-contract=off

# Regenerated on every run, but only replaced when it changed, so nothing relinks needlessly;
# a removed visual (or a removed directory) therefore drops out of the table as well.
build/visuals.c: tools/gen-visuals.sh FORCE
	@mkdir -p build
	@LC_ALL=C sh tools/gen-visuals.sh $(VISUALS) > $@.tmp && { cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@; }
FORCE:

build/visuals.o: build/visuals.c src/core.h src/scenes.h Makefile
	$(CC) $(CFLAGS) -Isrc -c -o $@.tmp $< && mv $@.tmp $@

# Doom on the framebuffer, started by the remote (tools/tv-dispatch: doom). Vendored, see
# third_party/fbdoom/ORIGIN; its own Makefile is not used. Its warnings are upstream's.
DOOMDIR := third_party/fbdoom
DOOMSRC := $(addprefix $(DOOMDIR)/,i_main.c dummy.c am_map.c doomdef.c doomstat.c dstrings.c \
	d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c g_game.c \
	hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c i_scale.c i_sound.c i_system.c \
	i_timer.c memio.c m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c \
	m_misc.c m_random.c p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c \
	p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c \
	p_telept.c p_tick.c p_user.c r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c \
	r_things.c sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c tables.c v_video.c \
	wi_stuff.c w_checksum.c w_file.c w_file_stdc_unbuffered.c w_main.c w_wad.c z_zone.c \
	i_video_fbdev.c i_input_tty.c i_input_term.c)
DOOMOBJS := $(DOOMSRC:%.c=build/%.o)
DOOMFLAGS := -O2 -w -DNORMALUNIX -DLINUX -DSNDSERV

fbdoom: $(DOOMOBJS)
	$(CC) -o $@.tmp $^ -lm && mv $@.tmp $@

# a static pattern rule: beats the generic build/%.o above in every make version
$(DOOMOBJS): build/$(DOOMDIR)/%.o: $(DOOMDIR)/%.c $(wildcard $(DOOMDIR)/*.h) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(DOOMFLAGS) -c -o $@.tmp $< && mv $@.tmp $@

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
	rm -rf build bc101 tvctl fbdoom fonts

.PHONY: all clean preview FORCE

# Website (GitHub Pages), needs emcc, pandoc, typst: see web/site.mk. Optional, because
# deploy.sh does not send web/ to the Pi and `make` there must not die on a missing file.
-include web/site.mk
