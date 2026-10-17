# tvtv — bc101 demo show on a CRT

A demoscene-style show for **bitcircus101**, running on a Raspberry Pi 1 Model B
that feeds an old tube TV over composite video. Every pixel is computed live in
C and written straight into the framebuffer — no X, no image files.

15 scenes loop forever (plasma, tunnel, fire, juggling clown, flower power,
rainbow, synthwave, …), plus two narrated slide decks that explain how it works.

## Hardware

| Part | Detail |
|---|---|
| Board | Raspberry Pi 1 Model B Rev 2 — one ARM1176 core, 700 MHz |
| OS | Raspbian 13 (trixie), kernel 6.18, KMS driver `vc4` |
| Video | composite (RCA) → S-Video adapter → CRT |
| Network | USB Wi-Fi stick, no LAN cable |

## Display setup (one-time, on the Pi)

Appended to the single line in `/boot/firmware/cmdline.txt`:

```
vc4.tv_norm=PAL video=Composite-1:720x288@50,margin_left=56,margin_right=40,margin_top=16,margin_bottom=20
```

- `vc4.tv_norm=PAL` — the Pi defaults to NTSC; on this TV that meant black and white.
- `720x288@50` — progressive instead of 576i, which removes interlace flicker.
- `margin_*` — overscan compensation, measured with the `testcard` scene: each
  value is the first numbered line that was fully visible on that edge. The GPU
  scales the whole picture (console included) into the visible area.

## Layout

```
src/core.{c,h}    canvas (360x288 RGB565), fonts, drawing helpers
src/main.c        framebuffer setup, scene sequencer, fades, fps log
src/scenes/*.c    one file per scene
slides/*.txt      German text of the slide decks (data, not code)
system/           systemd unit template that runs the show on tty1
tools/install.sh  on the Pi: make, install unit, restart, check it stays up
tools/boot-report.sh  on the Pi: boot timing + network logs for analysis
scripts/deploy.sh on the workstation: rsync to the Pi, then tools/install.sh
scripts/setup-claude-identity.sh  git identity helper (wrapper into ~/ops)
Makefile          builds bc101 and extracts the console fonts (run on the Pi)
```

The canvas is 360x288 and every pixel is doubled horizontally on its way to the
720x288 framebuffer — half the work, roughly square pixels. Fonts and slides are
found next to the executable, and install.sh writes the install directory into
the unit, so nothing depends on a particular user or home path.

## Workflow

The repo is the source of truth; the Pi only receives copies.

```sh
scripts/deploy.sh -n   # dry run: file changes and the unit diff
scripts/deploy.sh      # sync, make (incremental), restart bc101.service
```

Deploy fails (exit 1) if the show does not stay up for 3 s after the restart.

Slow boot? On the Pi run `sudo ~deploy/tvtv/tools/boot-report.sh`; it
writes a fresh `/tmp/boot-report.XXXXXX` and prints its name (timings,
NetworkManager and Wi-Fi logs; password-like values redacted).

`BC101_HOST` overrides the SSH host (default `tvtv-deploy`, files land in
`~/tvtv` there). A full build takes over a minute on the Pi 1; touching one
scene rebuilds in about 6 s.

## Running

```sh
sudo systemctl stop|start|restart bc101   # the looping show (starts at boot)
~/tvtv/bc101 -l                           # list scenes
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 juggle fire    # play chosen scenes
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 -t 120 explain # stop after 120 s
cat /tmp/anim.log                         # average fps and worst frame per scene
```

Stop the service before starting scenes by hand, otherwise both draw on tty1.
`explain` (making-of), `stack` (tech deep dive with buffer examples) and
`testcard` are not part of the default loop.

## Adding a scene

1. Create `src/scenes/<name>.c` ending in
   `const Scene scene_<name> = {"<name>", seconds, init_or_NULL, frame};`
2. Declare it in `src/main.c` and add it to `all[]` (loop) or `extra[]` (on demand).
3. `frame(t, dt)` draws one picture for scene time `t` into `buf[]`; keep it
   under 20 ms on the Pi 1 — precompute tables in `init`.

## Slides

`slides/*.txt`, UTF-8. Each deck is read once per process, so edits show up
after the next `scripts/deploy.sh` (which restarts bc101).

```
# Title            new slide, max 22 characters
@demo [arg]        live strip at the bottom, e.g. @fire, @chain A|B|C, @term make
> code             line rendered as code
plain text         body, max 44 characters per line, max 10 lines
```

Demos: `pipe chain bars pixels fps plasma fire particles list rainbow buf1d
double blit palette heat rgb565 sine term cpu` (see `src/scenes/slides.c`).

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). The Terminus console fonts are not
bundled; `make` extracts them from the system on the Pi.
