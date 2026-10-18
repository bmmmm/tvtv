# tvtv

*This page in [English](README.md).*

Eine Demoszene-Show für einen alten Röhrenfernseher, live berechnet auf einem Raspberry Pi 1
Model B. Im **bitcircus101** in Bonn laufen 17 Szenen in Schleife (Plasma, Tunnel, Feuer, ein
jonglierender Clown, Flower Power, Synthwave, eine Bildröhre in 3D, Bernd das Brot als
DVD-Logo, …) und zwei erklärende Folien-Decks. Jedes Pixel wird in C berechnet und direkt in
den Framebuffer geschrieben: kein X, keine Bilddateien, 50 Bilder pro Sekunde auf einem Kern
mit 700 MHz.

![Die Bildröhren-Szene, gerendert für die Website](https://bmmmm.github.io/tvtv/img/crt.png)

**Im Browser ansehen:** <https://bmmmm.github.io/tvtv/index.de.html> führt denselben C-Code
aus, übersetzt nach WebAssembly. Dazu gibt es ein
[Flipbook](https://bmmmm.github.io/tvtv/flipbook.de.html) Bild für Bild und das
[Whitepaper](https://bmmmm.github.io/tvtv/whitepaper.de.html) (HTML und PDF, Deutsch und
Englisch) darüber, wie die Show funktioniert und wie sie entstand.

<!-- ssh: sobald die Fernbedienung drin ist, hier ein Absatz: Mitglieder steuern die Show und
     laden Visuals per SSH hoch, siehe "Fernbedienung" unten. -->

## Hardware

| Teil | Detail |
|---|---|
| Board | Raspberry Pi 1 Model B Rev 2: ein ARM1176-Kern, 700 MHz, 512 MB |
| OS | Raspbian 13 (trixie), Kernel 6.18, KMS-Treiber `vc4` |
| Fernseher | Toshiba C-500, ein 5-Zoll-Kofferfernseher aus den frühen 1980ern, PAL |
| Video | Composite (Cinch) → S-Video-Adapter → AV-Eingang des Geräts |
| Netz | USB-WLAN-Stick, kein LAN-Kabel |

## Betrieb auf dem Pi

**Anzeige, einmalig.** An die eine Zeile in `/boot/firmware/cmdline.txt` anhängen:

```
vc4.tv_norm=PAL video=Composite-1:720x288@50,margin_left=56,margin_right=40,margin_top=16,margin_bottom=20
```

- `vc4.tv_norm=PAL`: der Pi sendet standardmäßig NTSC, das zeigte dieser Fernseher in Schwarzweiß.
- `720x288@50`: progressiv statt 576i, das nimmt das Zeilensprung-Flackern weg.
- `margin_*`: Overscan-Ausgleich, mit der Szene `testcard` gemessen: jeder Wert ist die erste
  nummerierte Zeile, die an diesem Rand ganz sichtbar war. Die GPU skaliert das ganze Bild,
  Konsole inklusive, in den sichtbaren Bereich.

**Bauen und installieren.** In einem Checkout auf dem Pi (`~/tvtv`):

```sh
make               # bc101 plus die zwei Fonts, aus den Konsolenfonts extrahiert
tools/install.sh   # make, systemd-Unit installieren, neu starten, prüfen, dass es läuft
```

Die Unit startet die Show beim Booten auf tty1; install.sh trägt das Installationsverzeichnis ein.

<!-- ssh: der Installationspfad wird /opt/bc101, der Dienst läuft als Nutzer bc101; den
     Absatz oben ersetzen. -->

**Vom Arbeitsrechner aus** ist das Repo die Quelle der Wahrheit, der Pi bekommt nur Kopien:

```sh
scripts/deploy.sh -n   # Probelauf: Dateiänderungen und der Diff der Unit
scripts/deploy.sh      # rsync, make (inkrementell), bc101.service neu starten
```

`BC101_HOST` benennt den SSH-Host (Standard `tvtv-deploy`); die Dateien landen dort in
`~/tvtv`. Deploy schlägt fehl, wenn die Show nach dem Neustart nicht 3 s durchhält. Ein
voller Build dauert auf dem Pi 1 über eine Minute; eine geänderte Szene ist in etwa 6 s gebaut.

**Im Alltag.**

```sh
sudo systemctl stop|start|restart bc101                  # die Show in Schleife
~/tvtv/bc101 -l                                          # Szenen auflisten
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 juggle fire       # gewählte Szenen spielen
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 -t 120 explain    # nach 120 s beenden
cat /tmp/anim.log                                        # mittlere fps und schlechtestes Bild je Szene
```

Vor dem Starten von Hand den Dienst stoppen, sonst malen beide auf tty1. `explain`
(Making-of), `stack` (Technik im Detail) und `testcard` gehören nicht zur Standardschleife.
Langsamer Boot? `sudo ~/tvtv/tools/boot-report.sh` schreibt Zeiten und Netzwerk-Logs in ein
frisches `/tmp/boot-report.XXXXXX` und nennt den Namen (passwortartige Werte geschwärzt).

<!-- ssh: hier ein Abschnitt "Fernbedienung": bc101 ctl status/next/play/hold, der Alias ssh
     tv, Visual hochladen und löschen, wer einen Schlüssel bekommt. -->

## Ansehen ohne Pi

`make preview` baut `build/preview`, die Szenen ohne Framebuffer, auf Linux und macOS. Es
braucht die zwei Terminus-Fonts in `fonts/`: auf Debian oder Ubuntu extrahiert sie
`make fonts/big.psf fonts/small.psf` aus `console-setup`; auf dem Mac vom Pi kopieren
(`scp -O <pi>:'tvtv/fonts/*.psf' fonts/`).

```sh
build/preview crt frames out 3.0 9.5      # ein 720x576-BMP je Zeitpunkt
build/preview -1x bernd seq out 0 13 25   # nummerierte 360x288-Bilder mit 25 fps
build/preview crt bench 400               # mittlere und schlechteste Bildzeit; echte Zahlen nur auf dem Pi
build/preview crt probe 3.00 3.08 3.16    # Schwerpunkt der weißen Marke und Box des Nicht-Schwarzen, für Bewegungstests
```

`frames` und `probe` rufen `frame(t)` einmal je Zeitpunkt auf; Szenen, die ihren Zustand
Bild für Bild aufbauen (stars, flowers, explode, matrix), brauchen `seq` ab 0. Die Blenden
des Sequencers fehlen. Standbilder übersehen Bewegungsfehler: ein Strahl, der sich mit dem
12,5-fps-Raster überlagerte, lief rückwärts, obwohl jedes einzelne Bild stimmte. Positionen
über aufeinanderfolgende Schritte proben, Bilder hashen, die identisch sein müssen.

Ganze Szenen als Flipbook, eine eigenständige HTML-Datei, abspielbar und scrubbar, nichts aus
dem Netz geladen (nur Python-Standardbibliothek):

```sh
python3 tools/flipbook.py tools/flipbook.de.json build/flipbook.html
```

Die Spezifikation nennt Szenen, Bildraten, Zeitleisten-Marken und Notizen; `flipbook.en.json`
ist die englische. Ihre Texte sind CC-BY-4.0 wie die Folien.

## Die Website

`make site` baut die ganze Site nach `build/site/`: die Szenen als WebAssembly (`web/shim.c`
und `web/player.js`), beide Flipbooks und das Whitepaper als HTML und PDF, auf Deutsch und
Englisch. Zusätzlich zur C-Toolchain braucht das [Emscripten](https://emscripten.org),
[pandoc](https://pandoc.org), [typst](https://typst.app) und node. `make check-site` spielt
unter node eine Sekunde jeder Szene und schlägt fehl, wenn eine nichts zeichnet. Der Workflow
`pages` macht denselben Build auf GitHub und veröffentlicht `main` auf GitHub Pages.

## Eine Szene hinzufügen

1. `src/scenes/<name>.c` anlegen, am Ende
   `const Scene scene_<name> = {"<name>", sekunden, init_oder_NULL, frame};`
2. In `src/scenes.c` deklarieren und in `scene_loop[]` oder `scene_extra[]` (auf Abruf) eintragen.
3. `frame(t, dt)` malt ein Bild für die Szenenzeit `t` nach `buf[]`; auf dem Pi 1 unter 20 ms
   bleiben, Tabellen in `init` vorberechnen.
4. Ohne Pi ansehen (oben), dann `scripts/deploy.sh`.

<!-- ssh: mit hochgeladenen Visuals liegen Mitglieder-Szenen außerhalb von src/ und die
     Szenentabelle wird generiert; Schritte 1 und 2 anpassen. -->

## Folien

`slides/*.txt`, UTF-8, ein Deck je Datei, einmal je Prozess gelesen (Änderungen zeigen sich
nach dem nächsten Deploy).

```
# Titel            neue Folie, max. 22 Zeichen
@demo [arg]        Live-Streifen unten, z. B. @fire, @chain A|B|C, @term make
> code             Zeile als Code gesetzt
Text               Fließtext, max. 44 Zeichen je Zeile, max. 10 Zeilen
```

Zeilen vor dem ersten `#` werden ignoriert (dort steht der SPDX-Header). Demos: `pipe chain
bars pixels fps plasma fire particles list rainbow buf1d double blit palette heat rgb565 sine
term cpu` (siehe `src/scenes/slides.c`).

## Aufbau

```
src/core.{c,h}        Canvas (360x288 RGB565), Fonts, Zeichenhilfen
src/scenes.{c,h}      die Szenentabelle: Standardschleife und die Extras
src/main.c            Framebuffer-Setup, Szenen-Sequencer, Blenden, fps-Log
src/scenes/*.c        eine Datei je Szene
slides/*.txt          deutscher Text der Folien-Decks (Daten, kein Code)
system/               systemd-Unit-Vorlage, die die Show auf tty1 startet
tools/install.sh      auf dem Pi: make, Unit installieren, neu starten, prüfen
tools/boot-report.sh  auf dem Pi: Boot-Zeiten und Netzwerk-Logs
tools/preview.c       Offscreen-Renderer, Benchmark und Bewegungs-Probe
tools/flipbook.*      ganze Szenen als eigenständiges HTML-Flipbook, Spezifikation je Sprache
scripts/deploy.sh     auf dem Arbeitsrechner: rsync zum Pi, dann tools/install.sh
scripts/setup-claude-identity.sh  Git-Identitätshelfer für das Setup der Betreiber
web/                  die Website: WebAssembly-Shim und Player, Stylesheet, Seiten, site.mk
docs/                 das Whitepaper, Deutsch und Englisch
Makefile              baut bc101 und extrahiert die Konsolenfonts; `make site` für die Website
```

Das Canvas ist 360x288, und jedes Pixel wird auf dem Weg in den 720x288-Framebuffer
waagerecht verdoppelt: halbe Arbeit, etwa quadratische Pixel. Fonts und Folien liegen neben
der ausführbaren Datei, und install.sh trägt das Installationsverzeichnis in die Unit ein;
nichts hängt an einem bestimmten Nutzer oder Home-Pfad.

## Lizenz

- **Code** (`src/`, `tools/`, `scripts/`, `web/`, `system/`, `.github/`, `Makefile`):
  GPL-3.0-or-later, siehe [LICENSE](LICENSE).
- **Texte** (`slides/*.txt`, `tools/flipbook.*.json`, `docs/`, die Seitentexte in `web/*.md`,
  diese README): CC-BY-4.0, siehe [LICENSE-CC-BY-4.0](LICENSE-CC-BY-4.0). Überall
  wiederverwendbar, auch in Werken unter anderen Lizenzen, solange "bitcircus101" genannt,
  bitcircus101.de verlinkt und Änderungen gekennzeichnet werden.

Zwei Lizenzen, weil sie zu verschiedenen Dingen passen: GPL hält das Programm und seine
Ableitungen offen, CC-BY lässt die Prosa mit bloßer Namensnennung in Vorträge, Artikel oder
Newsletter wandern. Die Folien werden vom Programm nur als Daten geladen, der Schnitt erzeugt
also keinen Konflikt. Die Terminus-Konsolenfonts liegen nicht im Repository: `make` extrahiert
sie aus dem System, und die Website bündelt sie unter der OFL 1.1.

created with ♥ in bitcircus101 · assisted by Claude
