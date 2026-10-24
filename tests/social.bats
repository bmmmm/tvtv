#!/usr/bin/env bats
# The social channels, 24 pixel, 25 dem0 and 26 text: the snapshot reader (src/social.c) against
# snapshots that tests/mksnap.py writes from the bridge's documents, one broken rule at a time;
# the dem0 VM (src/dem0.c) against the conformance vectors; both on the bench of
# tests/social-check.c and tests/dem0-check.c, with ASan and UBSan where the compiler has them,
# and fuzzed. Then the pictures through build/preview, and announcements, the loop and the card
# in a headless show. Run from the repo root: bats tests/social.bats   (needs python3)
setup_file() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  make -s bc101 tvctl build/preview fonts/big.psf fonts/small.psf > /dev/null
  local cc=${CC:-cc} san=("-fsanitize=address,undefined,float-cast-overflow" -fno-sanitize-recover=all)
  local flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -Isrc)
  echo 'int main(void) { return 0; }' | "$cc" "${san[@]}" -x c - -o "$BATS_FILE_TMPDIR/san" 2> /dev/null &&
    "$BATS_FILE_TMPDIR/san" || san=()
  "$cc" "${flags[@]}" "${san[@]}" tests/social-check.c src/social.c src/dem0.c src/core.c -lm -o "$BATS_FILE_TMPDIR/social-check"
  "$cc" "${flags[@]}" "${san[@]}" tests/dem0-check.c src/dem0.c src/core.c -lm -o "$BATS_FILE_TMPDIR/dem0-check"
  python3 tests/mksnap.py "$BATS_FILE_TMPDIR/full.bin" --full
  export SNAP="$BATS_FILE_TMPDIR/show.bin" SOCK="$BATS_FILE_TMPDIR/ctl"
  # the loop is the test card (an hour long: it moves on only when told) and the three social
  # channels; no snapshot yet
  BC101_SOCIAL="$SNAP" ./bc101 -x -s "$SOCK" -t 900 testcard pixel dem0 text > /dev/null 2> "$BATS_FILE_TMPDIR/show.err" &
  echo $! > "$BATS_FILE_TMPDIR/pid"
  for _ in $(seq 25); do ./tvctl -s "$SOCK" ping > /dev/null 2>&1 && break; sleep 0.2; done
}
teardown_file() { kill "$(cat "$BATS_FILE_TMPDIR/pid")" 2> /dev/null || true; }
setup() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  T=$BATS_FILE_TMPDIR
  CHECK=$T/social-check
  VM=$T/dem0-check
}
ctl() { ./tvctl -s "$SOCK" "$@"; }

# snap <file> <json>: a snapshot from a spec (tests/mksnap.py), moved into place as the bridge does
snap() { python3 tests/mksnap.py "$1" - <<< "$2"; }

# until the show's status contains $1, for up to $2 seconds (default 6)
wait_status() {
  local i
  for ((i = 0; i < ${2:-6} * 5; i++)); do
    [[ $(ctl status) == *"$1"* ]] && return 0
    sleep 0.2
  done
  echo "status never had '$1': $(ctl status)"
  return 1
}

# pixel <bmp> <x> <y>: "r g b" at a point of the 360x288 BMP that preview -1x writes
pixel() {
  python3 - "$@" << 'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
off, = struct.unpack_from("<I", d, 10)
w, h = struct.unpack_from("<ii", d, 18)
x, y = int(sys.argv[2]), int(sys.argv[3])
o = off + (h - 1 - y) * ((w * 3 + 3) & ~3) + 3 * x
b, g, r = d[o:o + 3]
print(r, g, b)
PY
}

# q565 <r> <g> <b>: the colour as it comes out of the show's RGB565 canvas
q565() { echo "$(($1 & 0xF8)) $(($2 & 0xFC)) $(($3 & 0xF8))"; }
hexrgb() { q565 $((16#${1:0:2})) $((16#${1:2:2})) $((16#${1:4:2})); }
# vga <i>: the VGA colour i (tests/mksnap.py's palette), as it comes out of the canvas
vga() {
  local r g b
  read -r r g b <<< "$(python3 tests/mksnap.py vga "$1")"
  q565 "$r" "$g" "$b"
}

@test "a valid snapshot is read with all its parts" {
  run "$CHECK" "$T/full.bin"
  [ "$status" -eq 0 ]
  [ "$output" = "ok seq=1 flags=30 mode=normal bot=@tvtv@bonn.social pixels=3 progs=3 slots=3 announce=2/0/2000" ]
  snap "$T/empty.bin" '{}'
  run "$CHECK" "$T/empty.bin"
  [ "$output" = "ok seq=1 flags=30 mode=normal bot=@tvtv@bonn.social pixels=0 progs=0 slots=0 announce=0/0/0" ]
}

@test "every rule of the snapshot and of a dem0 object turns the file down, each with its reason" {
  local table=$T/rules seen=$T/seen
  cat > "$table" << 'EOF'
size-long size 19953, not 19952
size-short size 19951, not 19952
magic magic
version version 2
size-field size field 19953
crc crc
seq seq 0
flags flags 0x20
mode-empty mode
mode-char mode
mode-nul mode
mode-tail mode
bot-at bot
bot-char bot
bot-long bot
bot-tail bot
announce-kind announce_kind 4
announce-none announce: no kind, but an event or item
announce-event announce: pixel
announce-pixel-item announce: pixel
announce-pixel-none announce: pixel
announce-prog-item announce: program 3
announce-slot-item announce: line 3
announce-slot-free announce: line 1
pixel-secs pixel_secs 4
text-secs text_secs 61
canvas canvas: colour 16 at 5 7
reserved-pixels reserved bytes at 3204
recent-used recent[0]: used is 2
recent-gap recent[1]: used after a free entry
recent-dirty recent[3]: free, but not all 0
recent-x recent[0]: x 64 y 2
recent-y recent[0]: x 1 y 48
recent-color recent[0]: colour 16
recent-event recent[0]: event 0
recent-author recent[0]: author
nprog nprog 9
reserved-progs reserved bytes at 3721
prog-dirty prog[3]: unused, but not all 0
prog-event prog[0]: event 0
prog-secs prog[0]: secs 61
prog-reserved prog[0]: reserved bytes
prog-author prog[0]: author
prog-source-empty prog[0]: source
prog-source-char prog[0]: source
prog-source-long prog[0]: source
prog-source-511 prog[0]: source
nslots nslots 9
reserved-slots reserved bytes at 18669
slot-dirty slot[3]: unused, but not all 0
slot-label-empty slot[0]: label
slot-label-char slot[0]: label
slot-text-char slot[0]: text
slot-text-nul slot[0]: text
slot-author-free slot[1]: author
slot-author-missing slot[0]: author
slot-event-free slot[1]: event
slot-event-missing slot[0]: event
slot-reserved slot[0]: reserved bytes
slot-setat-free slot[1]: set_at of a free line
obj-version prog[0]: object: version
obj-nout prog[0]: object: nout
obj-out-unused prog[0]: object: unused output is not 0
obj-nconst prog[0]: object: too many constants
obj-nins-zero prog[0]: object: number of instructions
obj-nins-big prog[0]: object: number of instructions
obj-reserved prog[0]: object: reserved bytes
obj-const-nan prog[0]: object: constant is not finite
obj-const-inf prog[0]: object: constant is not finite
obj-const-subnormal prog[0]: object: constant is subnormal
obj-const-unused prog[0]: object: unused constant is not 0
obj-op-unknown prog[0]: object: unknown opcode
obj-op-zero prog[0]: object: unknown opcode
obj-k-range prog[0]: object: constant operand
obj-k-b prog[0]: object: constant operand
obj-in-range prog[0]: object: input operand
obj-in-b prog[0]: object: input operand
obj-u-b prog[0]: object: operand
obj-reg prog[0]: object: register
obj-read prog[0]: object: register read before it is written
obj-sel prog[0]: object: register read before it is written
obj-out prog[0]: object: output register is never written
obj-ins-unused prog[0]: object: unused instruction is not 0
obj-cost prog[0]: object: cost does not match the code
obj-cost-big prog[0]: object: cost over 400
EOF
  while read -r rule want; do
    python3 tests/mksnap.py "$T/bad.bin" --break "$rule"
    run "$CHECK" "$T/bad.bin"
    [[ $status -eq 1 && $output == "bad: $want" ]] || { echo "$rule: $output (want: bad: $want)"; return 1; }
    echo "$rule" >> "$seen"
  done < "$table"
  # and no rule of mksnap's is left out here
  run diff <(LC_ALL=C sort "$seen") <(python3 tests/mksnap.py breaks | LC_ALL=C sort)
  [ "$status" -eq 0 ]
}

@test "the last valid snapshot stays after a bad one; a file that is gone takes it with it" {
  snap "$T/s5.bin" '{"seq": 5}'
  snap "$T/s7.bin" '{"seq": 7}'
  python3 tests/mksnap.py "$T/crc.bin" --break crc
  python3 tests/mksnap.py "$T/obj.bin" --break obj-read
  watch() { "$CHECK" watch "$@" 2> "$T/watch.err"; }
  run watch "$T/watched.bin" "$T/s5.bin" "$T/crc.bin" "$T/obj.bin" "$T/s7.bin" - "$T/crc.bin"
  [ "$status" -eq 0 ]
  [ "${lines[0]}" = "5 ok" ]
  [ "${lines[1]}" = "5 crc" ]
  [ "${lines[2]}" = "5 prog[0]: object: register read before it is written" ]
  [ "${lines[3]}" = "7 ok" ]
  [[ ${lines[4]} == "none No such file"* ]]
  [ "${lines[5]}" = "none crc" ]
  # the reason, once per file, on stderr (the journal)
  run cat "$T/watch.err"
  [ "${#lines[@]}" -eq 4 ]
  [ "${lines[0]}" = "social: $T/watched.bin turned down: crc; the last valid one stays" ]
  [[ ${lines[2]} == *"No such file or directory; the social channels go blank" ]]
  [ "${lines[3]}" = "social: $T/watched.bin turned down: crc" ]
}

@test "the snapshot reader survives mutated files, with the CRC made right and without" {
  run "$CHECK" fuzz "$T/full.bin" 20000 101
  echo "$output"
  [ "$status" -eq 0 ]
  [[ $output == "fuzz 20000 rounds, "*" passed" ]]
}

@test "a source too long for its field is turned down before anything is copied" {
  python3 tests/mksnap.py "$T/long.bin" --break prog-source-511
  run "$CHECK" overrun "$T/long.bin"
  [ "$output" = "prog[0]: source, nothing past it" ]
}

@test "dem0: the bridge's conformance vectors, and mksnap.py's" {
  run "$VM" vectors tests/dem0-vectors.txt
  echo "$output"
  [ "$status" -eq 0 ]
  [[ $output =~ ^ok\ ([0-9]+)\ programs,\ ([0-9]+)\ points ]]
  ((BASH_REMATCH[1] >= 30 && BASH_REMATCH[2] >= 100))
  python3 tests/mksnap.py vectors > "$T/py-vectors.txt"
  run "$VM" vectors "$T/py-vectors.txt"
  echo "$output"
  [ "$status" -eq 0 ]
  [[ $output == "ok "*" programs, "*" points"* ]]
}

@test "dem0: a vector that is off fails" {
  # the first point with a finite last value gets 10 more there
  awk '!done && /^at / && $NF ~ /^-?[0-9.]+(e[-+][0-9]+)?$/ { $NF = $NF + 10; done = 1 } { print }' \
    tests/dem0-vectors.txt > "$T/off.txt"
  run cmp -s tests/dem0-vectors.txt "$T/off.txt" # "! cmp" would not fail a bats test
  [ "$status" -eq 1 ]
  run "$VM" vectors "$T/off.txt"
  [ "$status" -eq 1 ]
  [[ $output == *"FAIL "*" at "*": output "*"1 failures"* ]]
}

@test "dem0: random programs at odd points, and broken objects, run clean" {
  run "$VM" fuzz 20000 7
  echo "$output"
  [ "$status" -eq 0 ]
  [[ $output == "fuzz 20000 programs, "* ]]
}

@test "dem0: a held 25 goes round the programs, newest first; a cued one comes next" {
  run "$CHECK" rotate "$T/full.bin" 0 4
  [ "$output" = "2000 1999 1998 2000" ]
  run "$CHECK" rotate "$T/full.bin" 1999 3
  [ "$output" = "1999 1998 2000" ]
  run "$CHECK" rotate "$T/full.bin" 77 2
  [ "$output" = "2000 1999" ]
  # by the event numbers, in whatever order the list has them (the bridge's check asks for none)
  snap "$T/order.bin" '{"progs": [{"asm": "in r0 x; out r0", "event": 5}, {"asm": "in r0 x; out r0", "event": 9},
    {"asm": "in r0 x; out r0", "event": 7}], "pixels": [{"x": 1, "y": 1, "color": 1, "event": 3},
    {"x": 2, "y": 2, "color": 2, "event": 8}]}'
  run "$CHECK" "$T/order.bin"
  [[ $output == "ok "*"pixels=2 progs=3"* ]]
  run "$CHECK" rotate "$T/order.bin" 0 4
  [ "$output" = "9 7 5 9" ]
}

@test "pixel: every cell has its C64 colour, the newest one blinks" {
  snap "$T/wall.bin" '{"fill": 6, "paint": [[0, 0, 2], [63, 47, 7], [10, 20, 1], [8, 8, 0]],
    "pixels": [{"x": 40, "y": 30, "color": 13}]}'
  BC101_SOCIAL="$T/wall.bin" build/preview -1x pixel frames "$T/wall" 1.0 1.25 > /dev/null
  # the wall's top left corner is at 29,22 and a cell is 5x5 (src/scenes/pixel.c)
  c() { pixel "$T/wall/pixel_$1.bmp" $((29 + 5 * $2 + 2)) $((22 + 5 * $3 + 2)); }
  # the colours as einwurf.md 2.1 has them (Pepto)
  [ "$(c 01.00 0 0)" = "$(hexrgb 68372b)" ]
  [ "$(c 01.00 63 47)" = "$(hexrgb b8c76f)" ]
  [ "$(c 01.00 10 20)" = "$(hexrgb ffffff)" ]
  [ "$(c 01.00 8 8)" = "$(hexrgb 000000)" ]
  [ "$(c 01.00 30 30)" = "$(hexrgb 352879)" ]
  [ "$(c 01.00 40 30)" = "$(hexrgb 9ad284)" ]
  # the ring around the newest pixel turns from black to white and back
  ring() { pixel "$T/wall/pixel_$1.bmp" $((29 + 5 * 40 - 1)) $((22 + 5 * 30 - 1)); }
  [ "$(ring 01.00)" = "0 0 0" ]
  [ "$(ring 01.25)" = "$(hexrgb ffffff)" ]
}

@test "dem0: one value is a VGA colour, x a ramp of them" {
  snap "$T/k.bin" '{"progs": [{"asm": "const r0 4; out r0", "source": "4"}]}'
  BC101_SOCIAL="$T/k.bin" build/preview -1x dem0 frames "$T/k" 7.0 > /dev/null
  [ "$(pixel "$T/k/dem0_07.00.bmp" 180 150)" = "$(vga 4)" ]
  [ "$(pixel "$T/k/dem0_07.00.bmp" 3 100)" = "$(vga 4)" ]
  snap "$T/x.bin" '{"progs": [{"asm": "in r0 x; out r0", "source": "x"}]}'
  BC101_SOCIAL="$T/x.bin" build/preview -1x dem0 frames "$T/x" 7.0 > /dev/null
  [ "$(pixel "$T/x/dem0_07.00.bmp" 100 150)" = "$(vga 100)" ]
  [ "$(pixel "$T/x/dem0_07.00.bmp" 101 150)" = "$(vga 101)" ]
  [ "$(pixel "$T/x/dem0_07.00.bmp" 300 150)" = "$(vga 44)" ]
}

@test "dem0: three values are red, green and blue; the budget sets the cell size" {
  snap "$T/rgb.bin" '{"progs": [{"asm": "in r0 x; const r1 8; mul r2 r0 r1; in r3 y; const r4 255; out r2 r3 r4"}]}'
  # cost 5: 2x2 cells with the default budget, so x 101 is the cell at 100: 800 & 255 = 32
  BC101_SOCIAL="$T/rgb.bin" build/preview -1x dem0 frames "$T/rgb" 7.0 > /dev/null
  [ "$(pixel "$T/rgb/dem0_07.00.bmp" 101 151)" = "$(q565 32 150 255)" ]
  [ "$(pixel "$T/rgb/dem0_07.00.bmp" 100 150)" = "$(q565 32 150 255)" ]
  BC101_DEM0_BUDGET=1000000 BC101_SOCIAL="$T/rgb.bin" build/preview -1x dem0 frames "$T/rgb1" 7.0 > /dev/null
  [ "$(pixel "$T/rgb1/dem0_07.00.bmp" 101 151)" = "$(q565 40 151 255)" ]
  run env BC101_SOCIAL="$T/rgb.bin" build/preview dem0 bench 20
  [[ $output == *"dem0     cost 5, budget 600000: cells 2x2, 25920 of them; vm avg"* ]]
}

@test "dem0: a program too slow for the Pi gets coarser cells, and at 12x12 stands still" {
  snap "$T/slow.bin" '{"progs": [{"asm": "in r0 x; out r0"}]}'
  run env BC101_DEM0_MS=0.000001 BC101_SOCIAL="$T/slow.bin" build/preview dem0 bench 70
  echo "$output"
  [[ $output == *"in 1x1 cells, from now on 2x2"* ]]
  [[ $output == *"in 9x9 cells, from now on 12x12"* ]]
  [[ $output == *"even in 12x12 cells: the picture stands still"* ]]
  [[ $output == *"cells 12x12, 720 of them"* ]]
}

@test "dem0: late pictures make the cells coarser, also late only every other one; late by chance not" {
  pace() { BC101_DEM0_MS=14 "$VM" pace "$@" 2> /dev/null; }
  [ "$(pace 10 10 10 10 10 10 10 10)" = "1 1 1 1 1 1 1 1" ]
  [ "$(pace 15 15 15 15 15 15 15 15)" = "1 1 1 2 2 2 2 3" ]
  [ "$(pace 5 30 5 30 5 30 5 30)" = "1 1 1 2 2 2 2 3" ]
  [ "$(pace 100 100)" = "2 3" ] # four times the limit and more: at once
  local once
  once="$(printf '10 %.0s' $(seq 19))15" # one picture in 20 a little late
  [ "$(pace $once $once $once $once $once | tr -d ' 1')" = "" ]
}

@test "the reader takes only a regular file: a FIFO, a directory, a device, a symlink are turned down at once" {
  [ -p "$T/fifo" ] || mkfifo "$T/fifo"
  mkdir -p "$T/dir"
  ln -sfn "$T/full.bin" "$T/link"
  for p in "$T/fifo" "$T/dir" /dev/zero "$T/link"; do
    run timeout 5 "$CHECK" poll "$p"
    echo "$p: $output"
    [ "$status" -eq 0 ]
    local last=${lines[${#lines[@]} - 1]} # before it the reader's line for the journal
    [[ $last == "none "* && $last != "none ok" ]]
  done
  run timeout 5 "$CHECK" poll "$T/full.bin"
  [[ $output == *" ok" && $output != none* ]]
}

@test "the card: no snapshot, snow and KEIN EMPFANG" {
  BC101_SOCIAL="$T/nothing" build/preview -1x text frames "$T/card" 1.0 > /dev/null
  [ "$(pixel "$T/card/text_01.00.bmp" 75 112)" = "0 0 0" ]
  run python3 - "$T/card/text_01.00.bmp" << 'PY'
import sys
d = open(sys.argv[1], "rb").read()[54:54 + 360 * 3 * 40]
print(len({d[i:i + 3] for i in range(0, len(d), 3)}))
PY
  ((output > 30))
}

@test "list: 24 to 26 are the social channels" {
  run ctl list
  [[ $output == *"24 pixel        - social"* ]]
  [[ $output == *"25 dem0         - social"* ]]
  [[ $output == *"26 text         - social"* ]]
}

@test "show: without a snapshot the loop leaves the social channels out, held they are snow" {
  run ctl status
  [[ $output == *"scene=testcard"*"next=testcard"* ]]
  [[ $output != *"social="* ]]
  ctl tune 24 > /dev/null
  wait_status "scene=pixel ch=24 t="
  sleep 1
  run ctl status
  [[ $output == *"dur=10.0 mode=hold"* ]]
  [[ $output =~ fps=([0-9]+) ]]
  ((BASH_REMATCH[1] > 30))
  ctl auto > /dev/null
  wait_status "scene=testcard"
}

@test "show: the first snapshot after the start announces nothing" {
  snap "$SNAP" '{"seq": 1, "flags": ["announce"], "progs": [{"asm": "in r0 x; out r0", "event": 50}],
    "announce": {"kind": "dem0", "item": 0, "event": 50}}'
  wait_status "social=1"
  sleep 1.5
  run ctl status
  [[ $output == *"scene=testcard"*"mode=auto"* ]]
}

@test "show: no announcement while a channel is held, and none kept for later" {
  ctl tune 7 > /dev/null
  wait_status "scene=plasma"
  snap "$SNAP" '{"seq": 2, "flags": ["announce"], "progs": [{"asm": "in r0 y; out r0", "event": 51},
    {"asm": "in r0 x; out r0", "event": 50}], "announce": {"kind": "dem0", "item": 0, "event": 51}}'
  wait_status "social=2"
  sleep 1.5
  run ctl status
  [[ $output == *"scene=plasma"*"mode=hold"* ]]
  ctl auto > /dev/null
  wait_status "scene=testcard"
  sleep 1.5
  run ctl status
  [[ $output == *"scene=testcard"*"mode=auto"* ]]
}

@test "show: no announcement while another process has the screen (Doom), and none kept for later" {
  local snapf=$T/yield.bin sock=$T/yield.ctl ok=0
  BC101_SOCIAL="$snapf" ./bc101 -x -s "$sock" -t 60 testcard > /dev/null 2>&1 &
  local show=$!
  for _ in $(seq 25); do ./tvctl -s "$sock" ping > /dev/null 2>&1 && break; sleep 0.2; done
  snap "$snapf" '{"seq": 1, "progs": [{"asm": "in r0 x; out r0", "event": 7}]}'
  SOCK=$sock wait_status "social=1" || ok=1
  sleep 30 &
  local game=$!
  ./tvctl -s "$sock" yield "$game" > /dev/null || ok=1
  snap "$snapf" '{"seq": 2, "flags": ["announce"], "progs": [{"asm": "in r0 t; out r0", "event": 8},
    {"asm": "in r0 x; out r0", "event": 7}], "announce": {"kind": "dem0", "item": 0, "event": 8}}'
  SOCK=$sock wait_status "social=2" || ok=1
  sleep 1.5
  # the post took nothing back: the game plays on, the show waits for it
  local during alive=0
  during=$(./tvctl -s "$sock" status)
  [[ -e /proc/$game && $(awk '{ print $3 }' "/proc/$game/stat") != Z ]] && alive=1
  kill "$game"
  wait "$game" 2> /dev/null || true
  SOCK=$sock wait_status "yield=0" || ok=1
  sleep 1.5
  run ./tvctl -s "$sock" status
  kill "$show"
  [ "$ok" -eq 0 ]
  [ "$alive" -eq 1 ]
  [[ $during == *"scene=testcard"*"mode=auto"*"yield=$game"* ]]
  # and after the game, the news is old: the loop goes on
  [[ $output == *"scene=testcard"*"mode=auto"* ]]
}

@test "show: an announced program plays once, for its seconds, then the loop goes on" {
  snap "$SNAP" '{"seq": 3, "flags": ["announce"], "progs": [{"asm": "in r0 t; out r0", "event": 52, "secs": 7},
    {"asm": "in r0 y; out r0", "event": 51}], "announce": {"kind": "dem0", "item": 0, "event": 52}}'
  wait_status "scene=dem0 ch=25"
  run ctl status
  [[ $output == *"dur=7.0 mode=once"* ]]
  wait_status "mode=auto" 12
  wait_status "scene=testcard"
}

@test "show: announcements are at least 20 s apart" {
  snap "$SNAP" '{"seq": 4, "flags": ["announce"], "progs": [{"asm": "in r0 t; out r0", "event": 53}],
    "announce": {"kind": "dem0", "item": 0, "event": 53}}'
  wait_status "social=4"
  sleep 1.5
  run ctl status
  [[ $output == *"scene=testcard"*"mode=auto"* ]]
}

@test "show: pixel wall and text page are announced for their seconds, also over the loop" {
  local kind secs
  for kind in pixel text; do
    secs=$([[ $kind == pixel ]] && echo 9 || echo 11)
    BC101_SOCIAL="$T/$kind.bin" ./bc101 -x -s "$T/$kind.ctl" -t 60 fire > /dev/null 2>&1 &
    local pid=$! ok=0
    for _ in $(seq 25); do ./tvctl -s "$T/$kind.ctl" ping > /dev/null 2>&1 && break; sleep 0.2; done
    snap "$T/$kind.bin" '{"seq": 1, "pixels": [{"x": 1, "y": 1, "color": 1, "event": 7}], "slots": [{"label": "A"}]}'
    SOCK=$T/$kind.ctl wait_status "social=1" || ok=1
    snap "$T/$kind.bin" '{"seq": 2, "pixel_secs": 9, "text_secs": 11, "pixels": [{"x": 2, "y": 2, "color": 2, "event": 8}],
      "slots": [{"label": "A", "text": "hallo", "event": 8}], "announce": {"kind": "'"$kind"'", "item": 0, "event": 8}}'
    SOCK=$T/$kind.ctl wait_status "scene=$kind ch=" || ok=1
    run ./tvctl -s "$T/$kind.ctl" status
    kill "$pid"
    [ "$ok" -eq 0 ]
    [[ $output == *"dur=$secs.0 mode=once"* ]]
  done
}

@test "show: the loop leaves out what the snapshot leaves out" {
  snap "$SNAP" '{"seq": 5, "flags": [], "progs": [{"asm": "in r0 x; out r0"}]}'
  wait_status "social=5"
  ctl next > /dev/null
  wait_status "scene=testcard ch=22 t=0"   # the new one, past the fade: the loop has moved on
  run ctl status
  [[ $output == *"next=testcard"* ]]
  snap "$SNAP" '{"seq": 6, "flags": ["loop_text"]}'
  wait_status "next=text"
  snap "$SNAP" '{"seq": 7, "flags": ["loop_dem0"]}'
  wait_status "social=7"
  run ctl status
  [[ $output == *"next=testcard"* ]]
  snap "$SNAP" '{"seq": 8, "flags": ["loop_dem0", "loop_pixel"], "progs": [{"asm": "in r0 x; out r0"}]}'
  wait_status "social=8"
  run ctl status
  [[ $output == *"next=pixel"* ]]
  ctl next > /dev/null
  wait_status "scene=pixel"
  run ctl status
  [[ $output == *"next=dem0"* ]]
  ctl next > /dev/null
  wait_status "scene=dem0"
  run ctl status
  [[ $output == *"next=testcard"* ]]
}

@test "show: a held 25 without programs is the card, 10 s a time, and the show answers" {
  ctl tune 22 > /dev/null
  wait_status "scene=testcard"
  snap "$SNAP" '{"seq": 9, "flags": ["loop_dem0"]}'
  wait_status "social=9"
  ctl tune 25 > /dev/null
  wait_status "scene=dem0 ch=25"
  sleep 1.5
  run ctl status
  [[ $output == *"dur=10.0 mode=hold"* ]]
  [[ $output =~ fps=([0-9]+) ]]
  ((BASH_REMATCH[1] > 30))
  run ctl ping
  [ "$status" -eq 0 ]
  ctl auto > /dev/null
}

@test "show: a bad snapshot keeps the last one, a file that is gone blanks the channels" {
  snap "$SNAP" '{"seq": 10}'
  wait_status "social=10"
  python3 tests/mksnap.py "$SNAP" --break slot-text-char
  sleep 1.5
  run ctl status
  [[ $output == *"social=10"* ]]
  grep -q "show.bin turned down: slot\[0\]: text; the last valid one stays" "$T/show.err"
  rm "$SNAP"
  sleep 1.5
  run ctl status
  [[ $output != *"social="* ]]
  grep -q "show.bin: No such file or directory; the social channels go blank" "$T/show.err"
}

@test "pixel, dem0 and text are the show's own names: no visual takes them" {
  mkdir -p "$T/vis"
  for n in pixel dem0 text ok; do echo "// $n" > "$T/vis/$n.c"; done
  run sh tools/gen-visuals.sh "$T/vis"
  [[ $output == *"pixel' is the show's own name"*"dem0' is the show's own name"* ]] ||
    [[ $output == *"dem0' is the show's own name"*"pixel' is the show's own name"* ]]
  [[ $output == *"text' is the show's own name"* ]]
  [[ $output == *"extern const Scene scene_ok;"* ]]
  [[ $output != *"scene_pixel"* && $output != *"scene_text"* && $output != *"scene_dem0"* ]]
  run make -s -n VISUALS="$T/vis" bc101
  [[ $output == *"$T/vis/ok.c"* ]]
  [[ $output != *"$T/vis/pixel.c"* && $output != *"$T/vis/text.c"* && $output != *"$T/vis/dem0.c"* ]]
}
