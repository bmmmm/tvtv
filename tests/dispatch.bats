#!/usr/bin/env bats
# tools/tv-dispatch against a headless show in a scratch install, with faked sshd variables.
# Run from the repo root: bats tests/dispatch.bats   (builds the show once, takes a while)
setup_file() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  T=$BATS_FILE_TMPDIR
  export TVTV_HOME="$T/home" TVTV_SOCK="$T/ctl" TVTV_KEYS="$T/authorized_keys"
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
  start_show
}
teardown_file() { stop_show; }
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

@test "doom: list finds WADs on the stick and in the system, any case" {
  mkdir -p "$T/stick" "$T/games"
  : > "$T/stick/FREEDOOM1.WAD"
  : > "$T/games/freedoom2.wad"
  : > "$T/games/not-a-name.wad"
  TVTV_WADS="$T/stick $T/games $T/missing" run tv bob doom list
  [ "$status" -eq 0 ]
  [ "$output" = "$(printf 'freedoom1\nfreedoom2')" ]
}

@test "doom: arguments are checked before anything starts" {
  mkdir -p "$T/stick"
  : > "$T/stick/freedoom1.wad"
  export TVTV_WADS="$T/stick"
  run tv bob doom nosuchwad
  [ "$status" -eq 1 ]
  [[ $output == *"no WAD 'nosuchwad'"* ]]
  run tv bob doom freedoom1 e5m1
  [ "$status" -eq 2 ]
  run tv bob doom freedoom1 map33
  [ "$status" -eq 2 ]
  run tv bob doom freedoom1 e1m1 6
  [ "$status" -eq 2 ]
  run tv bob doom freedoom1 '' 3
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
