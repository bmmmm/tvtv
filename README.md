# tvtv

*Diese Seite auf [Deutsch](README.de.md).*

A demoscene show for an old tube TV, computed live on a Raspberry Pi 1 Model B. In the
**bitcircus101** space in Bonn it loops 17 scenes (plasma, tunnel, fire, a juggling clown,
flower power, synthwave, a 3D cathode ray tube, Bernd das Brot as the DVD logo, …) and two
narrated slide decks that explain how it all works. Every pixel is computed in C and written
straight into the framebuffer: no X, no image files, 50 frames per second on one 700 MHz core.

**Watch it in the browser:** <https://bmmmm.github.io/tvtv/> runs the same C code compiled to
WebAssembly. The site also has a frame-by-frame
[flipbook](https://bmmmm.github.io/tvtv/flipbook.html) and the
[whitepaper](https://bmmmm.github.io/tvtv/whitepaper.html) (HTML and PDF, English and German)
on how the show works and how it was made.

<!-- ssh: once the remote control lands, add a paragraph here: members steer the show and
     upload visuals over SSH, see "Remote control" below. -->

## Hardware

| Part | Detail |
|---|---|
| Board | Raspberry Pi 1 Model B Rev 2: one ARM1176 core, 700 MHz, 512 MB |
| OS | Raspbian 13 (trixie), kernel 6.18, KMS driver `vc4` |
| Video | composite (RCA) → S-Video adapter → CRT, PAL |
| Network | USB Wi-Fi stick, no LAN cable |

## Running it on the Pi

**Display, once.** Append to the single line in `/boot/firmware/cmdline.txt`:

```
vc4.tv_norm=PAL video=Composite-1:720x288@50,margin_left=56,margin_right=40,margin_top=16,margin_bottom=20
```

- `vc4.tv_norm=PAL`: the Pi defaults to NTSC, which this TV showed in black and white.
- `720x288@50`: progressive instead of 576i, which removes the interlace flicker.
- `margin_*`: overscan compensation, measured with the `testcard` scene: each value is the
  first numbered line fully visible on that edge. The GPU scales the whole picture, console
  included, into the visible area.

**Build and install.** In a checkout on the Pi (`~/tvtv`):

```sh
make               # bc101 plus the two fonts it needs, extracted from the console fonts
tools/install.sh   # make, install the systemd unit, restart, check that it stays up
```

The unit starts the show on tty1 at boot; install.sh writes the install directory into it.

<!-- ssh: the install path becomes /opt/bc101 and the service runs as user bc101; replace
     the paragraph above. -->

**From a workstation** the repo is the source of truth and the Pi only receives copies:

```sh
scripts/deploy.sh -n   # dry run: file changes and the unit diff
scripts/deploy.sh      # rsync, make (incremental), restart bc101.service
```

`BC101_HOST` names the SSH host (default `tvtv-deploy`); files land in `~/tvtv` there. Deploy
fails if the show does not stay up for 3 s after the restart. A full build takes over a minute
on the Pi 1; touching one scene rebuilds in about 6 s.

**Day to day.**

```sh
sudo systemctl stop|start|restart bc101                  # the looping show
~/tvtv/bc101 -l                                          # list scenes
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 juggle fire       # play chosen scenes
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 -t 120 explain    # stop after 120 s
cat /tmp/anim.log                                        # average fps and worst frame per scene
```

Stop the service before starting scenes by hand, otherwise both draw on tty1. `explain`
(making-of), `stack` (tech deep dive) and `testcard` are not part of the default loop. Slow
boot? `sudo ~/tvtv/tools/boot-report.sh` writes timings and network logs to a fresh
`/tmp/boot-report.XXXXXX` and prints its name (password-like values redacted).

<!-- ssh: a "Remote control" section goes here: bc101 ctl status/next/play/hold, the ssh tv
     alias, uploading and removing a visual, who gets a key. -->

## Previewing without the Pi

`make preview` builds `build/preview`, the scenes without the framebuffer, on Linux and macOS.
It needs the two Terminus fonts in `fonts/`: on Debian or Ubuntu `make fonts/big.psf
fonts/small.psf` extracts them from `console-setup`; on a Mac copy them from the Pi
(`scp -O <pi>:'tvtv/fonts/*.psf' fonts/`).

```sh
build/preview crt frames out 3.0 9.5      # one 720x576 BMP per time
build/preview -1x bernd seq out 0 13 25   # numbered 360x288 frames at 25 fps
build/preview crt bench 400               # average and worst frame time; real numbers only on the Pi
build/preview crt probe 3.00 3.08 3.16    # white-marker centroid and non-black box, for motion checks
```

`frames` and `probe` call `frame(t)` once per time, so scenes that build their state frame by
frame (stars, flowers, explode, matrix) need `seq` from 0; the sequencer's fades are not
applied. Stills miss motion bugs: a beam that aliased against the 12.5 fps stepping ran
backwards while every single frame looked right. Probe positions over consecutive steps, and
hash frames that must be identical.

Whole scenes as a flipbook, one self-contained HTML file, playable and scrubbable, nothing
loaded from the net (standard-library Python only):

```sh
python3 tools/flipbook.py tools/flipbook.en.json build/flipbook.html
```

The spec names the scenes, frame rates, timeline marks and notes; `flipbook.de.json` is the
German one. Their texts are CC-BY-4.0 like the slides.

## The website

`make site` builds the whole site into `build/site/`: the scenes compiled to WebAssembly
(`web/shim.c` and `web/player.js`), both flipbooks, and the whitepaper as HTML and PDF, in
English and German. On top of the C toolchain it needs [Emscripten](https://emscripten.org),
[pandoc](https://pandoc.org), [typst](https://typst.app) and node. `make check-site` plays
one second of every scene under node and fails if one draws nothing. The `pages` workflow runs
the same build on GitHub and deploys `main` to GitHub Pages.

## Adding a scene

1. Create `src/scenes/<name>.c` ending in
   `const Scene scene_<name> = {"<name>", seconds, init_or_NULL, frame};`
2. Declare it in `src/scenes.c` and add it to `scene_loop[]` or `scene_extra[]` (on demand).
3. `frame(t, dt)` draws one picture for scene time `t` into `buf[]`; keep it under 20 ms on
   the Pi 1 and precompute tables in `init`.
4. Look at it without the Pi (above), then `scripts/deploy.sh`.

<!-- ssh: with uploaded visuals, members' scenes live outside src/ and the scene table is
     generated; adjust steps 1 and 2. -->

## Slides

`slides/*.txt`, UTF-8, one deck per file, read once per process (edits show up after the
next deploy).

```
# Title            new slide, max 22 characters
@demo [arg]        live strip at the bottom, e.g. @fire, @chain A|B|C, @term make
> code             line rendered as code
plain text         body, max 44 characters per line, max 10 lines
```

Lines before the first `#` are ignored (that is where the SPDX header lives). Demos: `pipe
chain bars pixels fps plasma fire particles list rainbow buf1d double blit palette heat rgb565
sine term cpu` (see `src/scenes/slides.c`).

## Layout

```
src/core.{c,h}        canvas (360x288 RGB565), fonts, drawing helpers
src/scenes.{c,h}      the scene table: default loop and the extras
src/main.c            framebuffer setup, scene sequencer, fades, fps log
src/scenes/*.c        one file per scene
slides/*.txt          German text of the slide decks (data, not code)
system/               systemd unit template that runs the show on tty1
tools/install.sh      on the Pi: make, install unit, restart, check it stays up
tools/boot-report.sh  on the Pi: boot timing and network logs
tools/preview.c       offscreen renderer, benchmark and motion probe
tools/flipbook.*      whole scenes as a self-contained HTML flipbook, spec per language
scripts/deploy.sh     on the workstation: rsync to the Pi, then tools/install.sh
scripts/setup-claude-identity.sh  git identity helper for the maintainers' setup
web/                  the website: WebAssembly shim and player, stylesheet, pages, site.mk
docs/                 the whitepaper, English and German
Makefile              builds bc101 and extracts the console fonts; `make site` for the website
```

The canvas is 360x288 and every pixel is doubled horizontally on its way to the 720x288
framebuffer: half the work, roughly square pixels. Fonts and slides are found next to the
executable, and install.sh writes the install directory into the unit, so nothing depends on
a particular user or home path.

## License

- **Code** (`src/`, `tools/`, `scripts/`, `web/`, `system/`, `.github/`, `Makefile`):
  GPL-3.0-or-later, see [LICENSE](LICENSE).
- **Texts** (`slides/*.txt`, `tools/flipbook.*.json`, `docs/`, the page texts in `web/*.md`,
  this README): CC-BY-4.0, see [LICENSE-CC-BY-4.0](LICENSE-CC-BY-4.0). Reuse them anywhere,
  including in works under other licenses, as long as you credit "bitcircus101", link
  bitcircus101.de and mark your changes.

Two licenses because they fit different things: GPL keeps the program and its derivatives
open, CC-BY lets the prose travel into talks, articles or newsletters with just an
attribution. The slides are only loaded as data by the program, so this split causes no
conflict. The Terminus console fonts are not in the repository: `make` extracts them from the
system, and the website bundles them under the OFL 1.1.

created with ♥ in bitcircus101 · assisted by Claude
