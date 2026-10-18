---
lang: en
title: tvtv
pagetitle: "tvtv: a demoscene show for a tube TV, computed live on a Raspberry Pi 1"
description: "Seventeen demoscene scenes for a PAL tube TV, every pixel computed live in C on a Raspberry Pi 1. This page runs the same code in your browser."
eyebrow: "bitcircus101 · Raspberry Pi 1 · 360 × 288 · PAL 50 Hz"
subtitle: "A demoscene show for an old tube TV, computed live on a Raspberry Pi 1 from 2012: one core, 700 MHz, no image files. Seventeen scenes loop forever, plus two slide decks that explain how it all works. This page runs the very same C code, compiled to WebAssembly."
alt-href: index.de.html
alt-lang: de
alt-label: Deutsch
nav:
  - {href: "index.html", label: "Show", current: true}
  - {href: "flipbook.html", label: "Flipbook"}
  - {href: "whitepaper.html", label: "Whitepaper"}
  - {href: "https://github.com/bmmmm/tvtv", label: "GitHub"}
player: true
footer: "created with ♥ in bitcircus101 · assisted by Claude"
credits: "Code GPL-3.0-or-later, texts CC-BY-4.0. The letters are Terminus, the Linux console font (OFL 1.1)."
---

```{=html}
<div class="tv"><canvas id="tv" aria-label="the bc101 show, live"></canvas></div>
<div class="console">
  <button id="play" type="button">Pause</button>
  <select id="scene" data-all="whole show" aria-label="scene"></select>
  <label><input id="wide" type="checkbox" checked> 2×</label>
  <label><input id="crt" type="checkbox" checked> CRT</label>
  <span id="readout" class="readout"></span>
  <span class="budget" title="frame time against the 20 ms budget"><i id="budget"></i></span>
</div>
<p class="hint">Space pauses, N skips ahead. Scenes marked · are not in the loop: the two slide decks (in German) and the test card. The set is drawn after ours, a Toshiba C-500, a five-inch portable from the early eighties. 2× is the pixel doubling the Pi does on the way to the TV, CRT the glass and the scan lines of the tube; the bar is the time your machine needs per frame, against the 20 ms the Pi has.</p>
```

## In short

::: {.facts .grid}
Hardware
:   A Raspberry Pi 1 Model B from 2012: one ARM1176 core at 700 MHz, 512 MB of RAM. An old tube TV on the composite output, PAL, 50 pictures per second.

Pixels
:   The show draws into a 360 × 288 canvas with 16-bit colour and writes it straight into the Linux framebuffer; every pixel is doubled on the way out. No X, no window, no images.

Budget
:   50 frames a second leave 20 milliseconds per picture. Everything expensive, sines, angles, distances, is computed once into tables; a frame only looks things up and adds.

Code
:   About 2,500 lines of C: 19 scene files, a core, a sequencer, and two slide decks that are plain text. This page runs them unchanged; a 70-line shim stands in for the framebuffer. GPL-3.0 for the code, CC-BY-4.0 for the texts.
:::

## How it works

Every scene is one file with two functions: `init()` precomputes, `frame(t)` draws the picture for time `t`. A sequencer plays the scenes one after another and cross-fades between them. The pictures are classic tricks of the 1990s demoscene: four added sine waves become a plasma, angle and distance to the centre become the coordinates of a tunnel texture, a buffer of heat that cools and drifts becomes fire, every pixel of a word becomes a particle with gravity.

The TV needed some convincing first. The Pi speaks NTSC by default and the TV expects PAL, so the first picture was black and white. 576 interlaced lines flickered, so the show runs 720 × 288 progressive at 50 Hz. And a tube hides its edges, so a test card scene measured the overscan line by line.

The show was programmed by Claude, an AI, over SSH from a Mac, without ever seeing the TV. That made the tools that render frames, probe motion and bundle whole scenes into a flipbook as important as the scenes themselves. The whitepaper tells that story.

Members steer the show over SSH: `ssh zapper` opens a remote control drawn in the terminal, every scene has a channel number, and a scene in C can be uploaded to become a channel of its own, built, test-run offscreen and swapped in with a rollback. The show runs in a sandbox, as a user that can do nothing else.

## Read on

::: {.cards}
::: {.card}
### [Flipbook](flipbook.html)
Whole scenes frame by frame, scrubbable, with a timeline and notes.
:::
::: {.card}
### [Whitepaper](whitepaper.html)
How the show works and how it was made. Also as a [PDF](whitepaper.en.pdf).
:::
::: {.card}
### [Source](https://github.com/bmmmm/tvtv)
Code, slides and tools on GitHub. Build it, preview it without a Pi, add a scene.
:::
:::
