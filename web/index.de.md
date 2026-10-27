---
lang: de
title: tvtv
pagetitle: "tvtv: eine Demoszene-Show für einen Röhrenfernseher, live berechnet auf einem Raspberry Pi 1"
description: "Siebzehn Demoszene-Szenen für einen PAL-Röhrenfernseher, jedes Pixel live in C berechnet auf einem Raspberry Pi 1. Diese Seite führt denselben Code im Browser aus."
eyebrow: "bitcircus101 · Raspberry Pi 1 · 360 × 288 · PAL 50 Hz"
subtitle: "Eine Demoszene-Show für einen alten Röhrenfernseher, live berechnet auf einem Raspberry Pi 1 von 2012: ein Kern, 700 MHz, keine Bilddateien. Siebzehn Szenen laufen in Schleife, dazu zwei Folien-Decks, die erklären, wie alles funktioniert. Diese Seite führt denselben C-Code aus, übersetzt nach WebAssembly."
alt-href: index.html
alt-lang: en
alt-label: English
nav:
  - {href: "index.de.html", label: "Show", current: true}
  - {href: "flipbook.de.html", label: "Flipbook"}
  - {href: "whitepaper.de.html", label: "Whitepaper"}
  - {href: "https://github.com/bmmmm/tvtv", label: "GitHub"}
  - {href: "https://bonn.social/%40bc101_TV", label: "Mastodon"}
player: true
footer: "created with ♥ in bitcircus101 · assisted by AI"
credits: "Code GPL-3.0-or-later, Texte CC-BY-4.0. Die Schrift ist Terminus, die Schrift der Linux-Konsole (OFL 1.1)."
---

```{=html}
<div class="tv"><canvas id="tv" aria-label="die bc101-Show, live"></canvas></div>
<div class="console">
  <button id="play" type="button">Pause</button>
  <select id="scene" data-all="ganze Show" aria-label="Szene"></select>
  <label><input id="wide" type="checkbox" checked> 2×</label>
  <label><input id="crt" type="checkbox" checked> CRT</label>
  <span id="readout" class="readout"></span>
  <span class="budget" title="Bildzeit gegen das Budget von 20 ms"><i id="budget"></i></span>
</div>
<p class="hint">Leertaste pausiert, N springt weiter. Szenen mit · gehören nicht zur Schleife: die zwei Folien-Decks und die Testkarte. Das Gerät ist unserem nachgezeichnet, einem Toshiba C-500, ein 5-Zoll-Kofferfernseher aus den frühen Achtzigern. 2× ist die Pixelverdopplung, die der Pi auf dem Weg zum Fernseher macht, CRT das Glas und die Zeilen der Röhre; der Balken ist die Zeit, die dein Rechner pro Bild braucht, gemessen an den 20 ms des Pi.</p>
```

## Kurz gesagt

::: {.facts .grid}
Hardware
:   Ein Raspberry Pi 1 Model B von 2012: ein ARM1176-Kern mit 700 MHz, 512 MB RAM. Ein alter Röhrenfernseher am Composite-Ausgang, PAL, 50 Bilder pro Sekunde.

Pixel
:   Die Show malt in ein Canvas von 360 × 288 Pixeln mit 16 Bit Farbe und schreibt es direkt in den Linux-Framebuffer; jedes Pixel wird auf dem Weg hinaus verdoppelt. Kein X, kein Fenster, keine Bilder.

Budget
:   50 Bilder pro Sekunde lassen 20 Millisekunden pro Bild. Alles Teure, Sinus, Winkel, Abstände, wird einmal in Tabellen gerechnet; ein Bild schlägt nur nach und addiert.

Code
:   Rund 2500 Zeilen C: 19 Szenendateien, ein Kern, ein Sequencer und zwei Folien-Decks als reiner Text. Diese Seite führt sie unverändert aus; ein Shim von 70 Zeilen vertritt den Framebuffer. GPL-3.0 für den Code, CC-BY-4.0 für die Texte.
:::

## So funktioniert es

Jede Szene ist eine Datei mit zwei Funktionen: `init()` rechnet vor, `frame(t)` malt das Bild zum Zeitpunkt `t`. Ein Sequencer spielt die Szenen nacheinander ab und blendet weich über. Die Bilder sind klassische Tricks der Demoszene der 90er: vier addierte Sinuswellen werden ein Plasma, Winkel und Abstand zur Mitte werden die Koordinaten einer Tunnel-Textur, ein Puffer voller Hitze, der abkühlt und driftet, wird Feuer, jedes Pixel eines Wortes wird ein Teilchen mit Schwerkraft.

Der Fernseher wollte erst überredet werden. Der Pi spricht von Haus aus NTSC, der Fernseher erwartet PAL, also war das erste Bild schwarzweiß. 576 Zeilen im Zeilensprung flackerten, also läuft die Show mit 720 × 288 progressiv bei 50 Hz. Und eine Röhre versteckt ihre Ränder, also hat eine Testkarten-Szene den Overscan Zeile für Zeile vermessen.

Programmiert hat die Show eine KI per SSH vom Mac aus, ohne den Fernseher je zu sehen. Deshalb sind die Werkzeuge, die Bilder rendern, Bewegung messen und ganze Szenen zu einem Flipbook bündeln, so wichtig wie die Szenen selbst. Das Whitepaper erzählt diese Geschichte.

Mitglieder steuern die Show per SSH: `ssh zapper` öffnet eine Fernbedienung im Terminal, jede Szene hat eine Programmnummer, und eine Szene in C lässt sich hochladen und wird zum eigenen Kanal, gebaut, offscreen durchgespielt und mit Rollback eingetauscht. Die Show läuft in einer Sandbox, als Nutzer, der sonst nichts darf.

## Weiterlesen

::: {.cards}
::: {.card}
### [Flipbook](flipbook.de.html)
Ganze Szenen Bild für Bild, scrubbar, mit Zeitleiste und Notizen.
:::
::: {.card}
### [Whitepaper](whitepaper.de.html)
Wie die Show funktioniert und wie sie entstand. Auch als [PDF](whitepaper.de.pdf).
:::
::: {.card}
### [Quellcode](https://github.com/bmmmm/tvtv)
Code, Folien und Werkzeuge auf GitHub. Bauen, ohne Pi ansehen, eine Szene hinzufügen.
:::
:::
