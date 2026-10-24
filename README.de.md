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

Die Show hat eine Fernbedienung: `ssh zapper` öffnet einen Zapper im Terminal, mit LED-Ziffern
und allem, und Mitglieder mit Schlüssel schalten Programme um, halten eines fest oder laden ein
eigenes Visual in C hoch, das zum eigenen Kanal wird. Siehe „Fernbedienung“ unten und
[docs/remote.md](docs/remote.md) (Englisch).

Die Kanäle 24 bis 26 kommen von Mastodon: eine Pixelwand, die jeder mit einem Post bemalt
(`@tvtv px 12 30 rot`), Programme in dem0, einer winzigen Sprache für bewegte Bilder, die die Show
Pixel für Pixel ausrechnet (`@tvtv code sin(x/16+t)*40+r*90`), und eine Seite wie im Videotext.
Ein eigener Brückendienst sammelt sie ein; die Show liest nur die Datei, die er schreibt, und
prüft jedes Byte davon.

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
tools/install.sh   # make, Dienst neu starten, prüfen, dass es läuft
```

Die Unit startet die Show beim Booten auf tty1; scripts/deploy.sh trägt Installationsverzeichnis
und Besitzer ein und installiert sie. Mit eingerichteter Fernbedienung (`scripts/deploy.sh --setup`, einmalig) liegt die
Show in `/opt/tvtv`, gehört dem Dienstnutzer `zapper` und läuft als dieser in einer
systemd-Sandbox: kein Netz, schreibgeschütztes Dateisystem, nur Framebuffer und tty1.

**Vom Arbeitsrechner aus** ist das Repo die Quelle der Wahrheit, der Pi bekommt nur Kopien:

```sh
scripts/deploy.sh -n   # Probelauf: Dateiänderungen und der Diff der Unit
scripts/deploy.sh      # rsync, make (inkrementell), bc101.service neu starten
```

`BC101_HOST` benennt den SSH-Host des Deploy-Logins; er steht in `.env` (eine Kopie von
`.env.example`, git ignoriert `.env`) oder in der Umgebung. Die Dateien landen dort in
`/opt/tvtv`. Deploy schlägt fehl, wenn die Show nach dem Neustart nicht 3 s durchhält. Ein
voller Build dauert auf dem Pi 1 über eine Minute; eine geänderte Szene ist in etwa 6 s gebaut.

**Im Alltag.**

```sh
sudo systemctl stop|start|restart bc101                  # die Show in Schleife (stop: die Fernbedienung weckt sie)
sudo systemctl stop|start bc101.socket                   # Stecker ziehen | einstecken: aus bis zum Start
~/tvtv/bc101 -l                                          # Szenen auflisten
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 juggle fire       # gewählte Szenen spielen
sudo openvt -c 1 -s -f -- ~/tvtv/bc101 -t 120 explain    # nach 120 s beenden
cat /run/bc101/anim.log                                  # mittlere fps und schlechtestes Bild je Szene (/tmp bei Handstart)
```

Vor dem Starten von Hand `bc101.socket` stoppen, sonst malen beide auf tty1: Nach einem Stopp
von `bc101` allein schaltet jeder Befehl der Fernbedienung die Show wieder ein, und ein offener
Zapper fragt zweimal pro Sekunde. `explain`
(Making-of), `stack` (Technik im Detail) und `testcard` gehören nicht zur Standardschleife.
Langsamer Boot? `sudo ~/tvtv/tools/boot-report.sh` schreibt Zeiten und Netzwerk-Logs in ein
frisches `/tmp/boot-report.XXXXXX` und nennt den Namen (passwortartige Werte geschwärzt).

## Fernbedienung

```sh
ssh zapper                    # der Zapper: Ziffern wählen ein Programm, Pfeile zappen, Enter hält, n weiter
ssh zapper 7                  # oder ein Befehl: Programm 7, festgehalten
ssh zapper next | auto | status | list | play testcard 30
ssh zapper upload foo < foo.c # Admin-Schlüssel: neues Visual, Kanal 30+; rm foo | fetch foo
ssh zapper px 12 30 rot       # jeder Schlüssel: ein Pixel auf die Pixelwand (24); text 2 Hallo: die Textseite (26)
ssh zapper social             # Stand des Brückendienstes; social mode | pause | ban ...: Admin-Schlüssel
```

Jede Szene hat eine Programmnummer: 00 Standby (schwarz), 01–17 die Schleife, 20–22 die
Extras, 24–26 die Social-Kanäle, ab 30 die hochgeladenen Visuals. Der Nutzer `zapper` auf dem
Pi darf genau ein Programm ausführen, den Dispatcher, und das nur mit Schlüssel; Schlüssel gibt
es in zwei Rollen (`tv-keys add <wer> op|admin`). Ein Upload wird kompiliert, offscreen in
voller Länge durchgespielt und erst dann eingetauscht; kommt die Show nicht zurück, kehrt der
vorherige Build zurück. `scripts/tv` kapselt die ssh-Aufrufe, über den Alias oder eigene
Einstellungen (`tv config`: Host, Nutzer, Schlüssel), `bc101 ctl <befehl>` spricht von der
Konsole des Pi mit der Show. Einrichtung, Schlüssel, Tastenbelegung und Sicherheitsmodell:
[docs/remote.md](docs/remote.md) (Englisch).

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
   `const Scene scene_<name> = {"<name>", sekunden, init_oder_NULL, frame, NULL};`
2. In `src/scenes.c` deklarieren und in `scene_loop[]` oder `scene_extra[]` (nur per Name oder
   Nummer) eintragen. Damit bekommt sie ihre Programmnummer auf der Fernbedienung.
3. `frame(t, dt)` malt ein Bild für die Szenenzeit `t` nach `buf[]`; auf dem Pi 1 unter 20 ms
   bleiben, Tabellen in `init` vorberechnen.
4. Ohne Pi ansehen (oben), dann `scripts/deploy.sh`.

Eine Szene, die nicht ins Repo soll, nimmt den anderen Weg: `scripts/tv upload <name>.c`
schickt dieselbe Art Datei auf den Pi, wo sie in `visuals/` neben der Show landet, einen Kanal
ab 30 und einen generierten Tabelleneintrag bekommt (`tools/gen-visuals.sh`), ohne Änderung an
`src/scenes.c`. `docs/demo.c` ist ein kleines zum Ausprobieren.

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
src/scenes.{c,h}      die Szenentabelle mit Programmnummern: Schleife, Extras, Social, hochgeladene Visuals
src/social.{c,h}      der Schnappschuss der Social-Kanäle: gelesen, Byte für Byte geprüft, der letzte gültige bleibt
src/dem0.{c,h}        die dem0-VM: prüft den Bytecode eines Programms, rechnet ihn spaltenweise übers Canvas
src/main.c            Framebuffer-Setup, Szenen-Sequencer, Blenden, fps-Log, Steuerbefehle
src/ctl.{c,h}         der Steuer-Socket, Serverseite und Client `bc101 ctl`
src/remote.c          der Zapper, eine Fernbedienung im Terminal
src/tvctl.c           tvctl: ctl und Zapper als Client ohne Szenencode
src/scenes/*.c        eine Datei je Szene (pixel, dem0, text: die Social-Kanäle, 24-26)
slides/*.txt          deutscher Text der Folien-Decks (Daten, kein Code)
system/               systemd-Unit-Vorlage (Sandbox) und das sshd-Drop-in für den Nutzer zapper
tools/install.sh      auf dem Pi: make, neu starten, prüfen
tools/setup-remote.sh auf dem Pi, einmalig: Nutzer zapper, /opt/tvtv, Dispatcher, Schlüsseldatei, sshd
tools/tv-dispatch     was `ssh zapper` ausführt: Befehle, der Zapper, Uploads mit Rollback
tools/tv-keys.sh      auf dem Pi: Schlüssel und Rollen für die Fernbedienung
tools/social-pi-check.sh  auf dem Pi: VM und Leser der Social-Kanäle auf dem Gerät, Bench, Budget
tools/gen-visuals.sh  erzeugt build/visuals.c, die Tabelle der hochgeladenen Visuals
tools/boot-report.sh  auf dem Pi: Boot-Zeiten und Netzwerk-Logs
tools/preview.c       Offscreen-Renderer, Benchmark und Bewegungs-Probe
tools/flipbook.*      ganze Szenen als eigenständiges HTML-Flipbook, Spezifikation je Sprache
scripts/deploy.sh     auf dem Arbeitsrechner: rsync zum Pi, dann tools/install.sh; --setup einmalig
scripts/social-check.sh  auf dem Arbeitsrechner: kopiert, was tools/social-pi-check.sh braucht, zum Pi und startet es
scripts/tv            die Fernbedienung vom eigenen Terminal aus (ssh-Wrapper)
tests/*.bats          Sequencer, Socket, Dispatcher, Social-Kanäle: headless, auf jedem Linux
web/                  die Website: WebAssembly-Shim und Player, Stylesheet, Seiten, site.mk
docs/                 das Whitepaper (Deutsch und Englisch), remote.md, demo.c
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

created with ♥ in bitcircus101 · assisted by AI
