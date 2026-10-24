#!/usr/bin/env bats
# tools/tv-dispatch against a headless show in a scratch install, with faked sshd variables and a
# stand-in for the social bridge's control socket (tests/fake-social.py, needs python3).
# Run from the repo root: bats tests/dispatch.bats   (builds the show once, takes a while)
setup_file() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  T=$BATS_FILE_TMPDIR
  export TVTV_HOME="$T/home" TVTV_SOCK="$T/ctl" TVTV_KEYS="$T/authorized_keys"
  export TVTV_UNIT=tvtv-test-no-such-unit # the Pi's bc101 unit must not decide anything here
  export TVTV_SOCIAL_SOCK="$T/social.sock"
  mkdir -p "$TVTV_HOME"
  cp -r src slides tools Makefile system "$TVTV_HOME/"
  make -s -C "$TVTV_HOME" bc101 tvctl build/preview fonts/big.psf fonts/small.psf > /dev/null
  # two people: alice may upload, bob may only zap
  for who in alice bob mallory; do ssh-keygen -q -t ed25519 -N '' -f "$T/$who" -C "$who@test"; done
  TVTV_KEYS=$TVTV_KEYS tools/tv-keys.sh add alice admin "$T/alice.pub" > /dev/null
  TVTV_KEYS=$TVTV_KEYS tools/tv-keys.sh add bob op < "$T/bob.pub" > /dev/null
  for who in alice bob mallory; do
    printf 'publickey %s\n' "$(cut -d' ' -f1,2 "$T/$who.pub")" > "$T/auth.$who"
  done
  python3 tests/fake-social.py "$TVTV_SOCIAL_SOCK" "$T/social.log" &
  echo $! > "$T/social.pid"
  start_show
}
teardown_file() {
  stop_show
  kill "$(cat "$BATS_FILE_TMPDIR/social.pid")" 2> /dev/null || true
}
setup() { cd "$BATS_TEST_DIRNAME/.." || exit 1; T=$BATS_FILE_TMPDIR; }

start_show() {
  "$TVTV_HOME/bc101" -x -s "$TVTV_SOCK" -t 600 > /dev/null 2> "$BATS_FILE_TMPDIR/show.err" &
  echo $! > "$BATS_FILE_TMPDIR/pid"
  for _ in $(seq 25); do "$TVTV_HOME/tvctl" -s "$TVTV_SOCK" ping > /dev/null 2>&1 && break; sleep 0.2; done
}
stop_show() { kill "$(cat "$BATS_FILE_TMPDIR/pid")" 2> /dev/null || true; }

# tv <who> <command...>: what sshd would run for that key; stdin passes through
tv() {
  local who=$1
  shift
  SSH_USER_AUTH="$T/auth.$who" SSH_CONNECTION="1.2.3.4 1 5.6.7.8 22" SSH_ORIGINAL_COMMAND="$*" tools/tv-dispatch
}

@test "keys: list shows both roles" {
  run tools/tv-keys.sh list
  [ "$status" -eq 0 ]
  [[ $output == *"alice"*"admin"*"SHA256:"* ]]
  [[ $output == *"bob"*"op"* ]]
  run grep -c '^restrict,pty ssh-ed25519 ' "$TVTV_KEYS"
  [ "$output" = "2" ]
}

@test "op: status, channel number, named play" {
  run tv bob status
  [ "$status" -eq 0 ]
  [[ $output == *"scene="* ]]
  run tv bob 7
  [ "$status" -eq 0 ]
  [ "$output" = "ok 7 plasma hold" ]
  run tv bob play testcard 2
  [ "$status" -eq 0 ]
  [[ $output == "ok 22 testcard once" ]]
  run tv bob auto
  [ "$status" -eq 0 ]
}

@test "op may not upload, remove or fetch" {
  run tv bob upload foo < /dev/null
  [ "$status" -eq 3 ]
  run tv bob rm foo
  [ "$status" -eq 3 ]
  run tv bob fetch foo
  [ "$status" -eq 3 ]
}

# le32 <n>: four bytes, little endian (the inner printf writes the escapes, the outer the bytes)
le32() {
  # shellcheck disable=SC2059
  printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $(($1 & 255)) $(($1 >> 8 & 255)) $(($1 >> 16 & 255)) $(($1 >> 24 & 255)))"
}

# wad <file> <IWAD|PWAD> <lump>...: a WAD with an empty lump of each name; as in Doom's own
# files, the directory comes after the data (here 4 bytes of it)
wad() {
  local f=$1 kind=$2 name i
  shift 2
  {
    printf '%s' "$kind"
    le32 $#
    le32 16
    printf 'DATA'
    for name in "$@"; do
      le32 12
      le32 0
      printf '%s' "$name"
      for ((i = ${#name}; i < 8; i++)); do printf '\0'; done
    done
  } > "$f"
}

@test "doom: list finds the games on the stick and in the system, any case" {
  mkdir -p "$T/stick" "$T/games"
  wad "$T/stick/FREEDOOM1.WAD" IWAD E1M1
  wad "$T/games/freedoom2.wad" IWAD MAP01
  wad "$T/games/not-a-name.wad" IWAD MAP01
  wad "$T/games/addon.wad" PWAD MAP01
  TVTV_WADS="$T/stick $T/games $T/missing" run tv bob doom list
  [ "$status" -eq 0 ]
  [ "$output" = "$(printf 'freedoom1\nfreedoom2')" ]
}

@test "doom: arguments are checked before anything starts" {
  mkdir -p "$T/stick"
  wad "$T/stick/freedoom1.wad" IWAD THINGS E1M1 LINEDEFS
  wad "$T/stick/addon.wad" PWAD E1M1
  export TVTV_WADS="$T/stick"
  run tv bob doom nosuchwad
  [ "$status" -eq 1 ]
  [[ $output == *"no WAD 'nosuchwad'"* ]]
  run tv bob doom addon
  [ "$status" -eq 1 ]
  [[ $output == *"add-on"* ]]
  run tv bob doom freedoom1 e1m2
  [ "$status" -eq 1 ]
  [[ $output == *"has no level e1m2"* ]]
  run tv bob doom freedoom1 map01
  [ "$status" -eq 1 ]
  run tv bob doom freedoom1 e5m1
  [ "$status" -eq 2 ]
  run tv bob doom freedoom1 map33
  [ "$status" -eq 2 ]
  run tv bob doom freedoom1 e1m1 6
  [ "$status" -eq 2 ]
  run tv bob doom freedoom1 e1m1 3 extra
  [ "$status" -eq 2 ]
  # all fine, but bats has no terminal: refused before the show hands over the screen
  run tv bob doom freedoom1 e1m1 3
  [ "$status" -eq 2 ]
  [[ $output == *"needs a terminal"* ]]
  run tv bob status
  [[ $output == *"yield=0"* ]]
}

@test "an unknown key is an op" {
  run tv mallory status
  [ "$status" -eq 0 ]
  run tv mallory upload foo < /dev/null
  [ "$status" -eq 3 ]
  [[ $output == *"unknown, op"* ]]
}

@test "shell metacharacters and odd commands are rejected before anything runs" {
  for bad in 'status; id' 'next $(id)' 'play 7 1 | cat' 'tune `id`' "tune 'x'" 'status
id' 'list >/tmp/x' 'upload ../etc' 'next --help'; do
    run tv bob "$bad"
    [ "$status" -eq 2 ]
  done
  run tv bob frobnicate
  [ "$status" -eq 2 ]
  [[ $output == *"unknown command"* ]]
  run tv bob tune
  [ "$status" -eq 2 ]
  run tv bob play 7 abc
  [ "$status" -eq 2 ]
  run tv bob 7 8
  [ "$status" -eq 2 ]
}

@test "help without a terminal" {
  run env SSH_USER_AUTH="$T/auth.bob" SSH_CONNECTION=x tools/tv-dispatch < /dev/null
  [ "$status" -eq 0 ]
  [[ $output == *"zapper"* ]]
}

@test "upload: name and content checks" {
  run tv alice upload Boot < /dev/null
  [ "$status" -eq 2 ]
  run tv alice upload boot < "$TVTV_HOME/src/scenes/black.c"
  [ "$status" -eq 2 ]
  [[ $output == *"built-in"* ]]
  run tv alice upload empty < /dev/null
  [ "$status" -eq 2 ]
  [[ $output == *"nothing on stdin"* ]]
  run tv alice upload bin < "$TVTV_HOME/bc101"
  [ "$status" -eq 2 ]
  run tv alice upload wrong < "$TVTV_HOME/src/scenes/black.c"
  [ "$status" -eq 2 ]
  [[ $output == *"must define"* ]]
}

@test "upload: a good visual goes on air as channel 30" {
  cat > "$T/demo.c" <<'C'
// a bouncing bar, ümlauts in the comment are fine
#include "core.h"
static void frame(float t, float dt) {
    (void)dt;
    clear(rgb(0, 0, 40));
    rect(0, (int)(H / 2 + sinf(t * 2) * 100) - 4, W, 8, hsv16(t * 0.2f, 1, 1));
}
const Scene scene_demo = {"demo", 6, NULL, frame, NULL};
C
  ino_before=$(stat -c %i "$TVTV_HOME/bc101")
  run tv alice upload demo < "$T/demo.c"
  echo "$output"
  [ "$status" -eq 0 ]
  [[ $output == *"demo is channel 30"* ]]
  [ "$(stat -c %i "$TVTV_HOME/bc101")" != "$ino_before" ]
  [ ! -e "$TVTV_HOME/bc101.prev" ]
  run tv bob list
  [[ $output == *"30 demo "*"visual"* ]]
  run tv bob 30
  [ "$status" -eq 0 ]
  sleep 1
  run tv bob status
  [[ $output == *"scene=demo ch=30"* ]]
  run tv alice fetch demo
  [ "$status" -eq 0 ]
  [ "$output" = "$(cat "$T/demo.c")" ]
}

@test "upload: a visual that does not compile is rejected, the show keeps running" {
  printf '#include "core.h"\nstatic void frame(float t, float dt) { nonsense; }\nconst Scene scene_broken = {"broken", 5, NULL, frame, NULL};\n' > "$T/broken.c"
  run tv alice upload broken < "$T/broken.c"
  [ "$status" -eq 1 ]
  [[ $output == *"build failed"* ]]
  [[ $output == *"nonsense"* ]]
  [ -f "$TVTV_HOME/visuals/.broken.c.rejected" ]
  [ ! -f "$TVTV_HOME/visuals/broken.c" ]
  run tv bob status
  [ "$status" -eq 0 ]
  run tv bob list
  [[ $output != *"broken"* ]]
}

@test "upload: a visual that crashes in its test run is rejected" {
  cat > "$T/crash.c" <<'C'
#include "core.h"
static void frame(float t, float dt) { (void)dt; if (t > 2) *(volatile int *)0 = 1; clear(0); }
const Scene scene_crash = {"crash", 8, NULL, frame, NULL};
C
  run tv alice upload crash < "$T/crash.c"
  [ "$status" -eq 1 ]
  [[ $output == *"failed its test run"* ]]
  [ -f "$TVTV_HOME/visuals/.crash.c.rejected" ]
  run tv bob list
  [[ $output != *"crash"* ]]
  [[ $output == *"30 demo"* ]]
}

@test "rm: the visual disappears, the show comes back" {
  tv bob 30 > /dev/null
  sleep 1
  run tv alice rm demo
  [ "$status" -eq 0 ]
  [[ $output == *"demo removed"* ]]
  [ ! -f "$TVTV_HOME/visuals/demo.c" ]
  run tv bob list
  [[ $output != *"demo"* ]]
  run tv bob status
  [ "$status" -eq 0 ]
  [[ $output != *"scene=demo"* ]]
  run tv alice rm demo
  [ "$status" -eq 1 ]
}

@test "social: status, list and the modes, for any key" {
  run tv bob social
  [ "$status" -eq 0 ]
  [[ $output == "ok"*"mode=normal"*"mastodon=ok"* ]]
  run tv bob social list
  [ "$status" -eq 0 ]
  [[ $output == *"code 1 @alice@bonn.social"* ]]
  run tv bob social mode
  [ "$status" -eq 0 ]
  [[ $output == "ok normal"*"party px="* ]]
  [ "$(tail -n 3 "$T/social.log")" = "$(printf 'status\nlist\nmode')" ]
}

@test "social: whatever changes the bridge takes an admin key" {
  local c cmds=("social mode party" "social pause" "social resume" "social ban spam@example.social"
    "social unban mastodon:spam@example.social" "social bans" "social rm code 2" "social rm text 3"
    "social rm pixel 0 0 64 48")
  for c in "${cmds[@]}"; do
    run tv bob "$c"
    [[ $status -eq 3 && $output == *"needs an admin key (you are bob, op)"* ]] || { echo "$c: $output"; return 1; }
  done
  run grep -qx "mode party" "$T/social.log" # nothing reached the bridge ("! grep" would not fail)
  [ "$status" -eq 1 ]
  for c in "${cmds[@]}"; do
    run tv alice "$c"
    [ "$status" -eq 0 ] || { echo "$c: $output"; return 1; }
    [ "$(tail -n 1 "$T/social.log")" = "${c#social }" ]
  done
  run tv alice social mode party
  [ "$output" = "ok mode=party" ]
  run tv alice social mode nonsense
  [ "$status" -eq 1 ]
  [ "$output" = "err mode unbekannter Modus" ]
}

@test "social: the arguments are checked before anything goes to the bridge" {
  local n bad
  n=$(wc -l < "$T/social.log")
  for bad in "social mode Party" "social mode a b" "social pause now" "social ban" "social ban -x" \
    "social rm code 9" "social rm code" "social rm text 0" "social rm pixel 1 2 3" "social rm pixel 1 2 3 444" \
    "social rm frob 1" "social frob" "social list all"; do
    run tv alice "$bad"
    [ "$status" -eq 2 ] || { echo "$bad: $output"; return 1; }
  done
  [ "$(wc -l < "$T/social.log")" -eq "$n" ]
}

@test "px and text: any key with a name, as ssh:<who>, checked by the bridge like a post" {
  run tv bob px 12 30 rot
  [ "$status" -eq 0 ]
  [ "$output" = "ok accepted pixel 57" ]
  [ "$(tail -n 1 "$T/social.log")" = "submit bob px 12 30 rot" ]
  run tv alice text 2 Hallo aus dem Space
  [ "$status" -eq 0 ]
  [ "$output" = "ok accepted text 58" ]
  [ "$(tail -n 1 "$T/social.log")" = "submit alice text 2 Hallo aus dem Space" ]
  run tv bob px 99 0 1
  [ "$status" -eq 1 ]
  [ "$output" = "err px_range x 0-63, y 0-47." ]
  run tv mallory px 1 2 3
  [ "$status" -eq 3 ]
  [[ $output == *"px needs a key with a name"* ]]
  local bad
  for bad in "px 1 2" "px 1 2 3 4" "px 123 1 rot" "px a 1 rot" "text 9 x" "text 2" "text x Hallo"; do
    run tv bob "$bad"
    [ "$status" -eq 2 ] || { echo "$bad: $output"; return 1; }
  done
}

@test "the whitelist lets @ and : through for identities, and nothing else new" {
  run tv alice social ban mastodon:spam@example.social
  [ "$status" -eq 0 ]
  local bad
  for bad in 'social ban a;b' 'px 1 2 $x' 'text 1 `id`' 'social ban a|b' "text 1 it's" 'text 1 a&b' \
    'text 1 (x)' 'text 1 a>b' 'social ban a\b' 'text 1 #x' 'text 1 "x"'; do
    run tv bob "$bad"
    [[ $status -eq 2 && $output == *"unsupported characters"* ]] || { echo "$bad: $output"; return 1; }
  done
}

@test "social without the bridge: not set up here, or not answering" {
  TVTV_SOCIAL_SOCK="$T/none" run tv bob social
  [ "$status" -eq 1 ]
  [ "$output" = "tv: social is not set up on this TV" ]
  python3 -c 'import socket, sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "$T/dead.sock"
  TVTV_SOCIAL_SOCK="$T/dead.sock" run tv bob px 1 2 3
  [ "$status" -eq 1 ]
  [ "$output" = "tv: the social bridge does not answer (tvtv-social)" ]
}

@test "help lists px, text and social" {
  run tv bob help
  [[ $output == *"px <x> <y> <farbe>"* ]]
  [[ $output == *"text <nr> <dein Text>"* ]]
  [[ $output == *"social [list | mode]"* ]]
  [[ $output == *"social mode <name>"* ]]
}

@test "pixel, dem0 and text are the show's names: no visual takes them" {
  # an older show that does not list them yet: refused all the same, before anything is written
  mkdir -p "$T/old"
  printf '#!/bin/sh\necho " 1 boot       10s loop"\n' > "$T/old/bc101"
  chmod +x "$T/old/bc101"
  local n
  for n in pixel dem0 text; do
    run tv alice upload "$n" < "$TVTV_HOME/src/scenes/black.c"
    [[ $status -eq 2 && $output == *"'$n' is a built-in scene"* ]] || { echo "$n: $output"; return 1; }
    TVTV_HOME="$T/old" run tv alice upload "$n" < "$TVTV_HOME/src/scenes/black.c"
    [[ $status -eq 2 && $output == *"'$n' is reserved for the show"* ]] || { echo "$n (old): $output"; return 1; }
  done
  [ ! -e "$T/old/visuals" ]
  [ ! -e "$TVTV_HOME/visuals/pixel.c" ]
}

@test "upload: a build that dies on the tube is rolled back (last: it kills the show)" {
  # passes the offscreen test (the preview tool is not bc101) and dies as soon as bc101 execs it
  cat > "$T/sabot.c" <<'C'
#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
__attribute__((constructor)) static void boom(void) {
    char p[256]; ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
    if (n > 0) { p[n] = 0; const char *b = strrchr(p, '/'); if (b && !strcmp(b, "/bc101")) _exit(7); }
}
static void frame(float t, float dt) { (void)t; (void)dt; clear(0); }
const Scene scene_sabot = {"sabot", 5, NULL, frame, NULL};
C
  ino_before=$(stat -c %i "$TVTV_HOME/bc101")
  run tv alice upload sabot < "$T/sabot.c"
  echo "$output"
  [ "$status" -eq 1 ]
  [[ $output == *"rolling back"* ]]
  [ "$(stat -c %i "$TVTV_HOME/bc101")" = "$ino_before" ]
  [ -f "$TVTV_HOME/visuals/.sabot.c.rejected" ]
  [ ! -e "$TVTV_HOME/bc101.prev" ]
  # what systemd would do now: start the binary on disk -- the restored one runs
  start_show
  run tv bob status
  [ "$status" -eq 0 ]
  run tv bob list
  [[ $output != *"sabot"* ]]
}
