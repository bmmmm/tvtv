#!/usr/bin/env bats
# The sequencer and its control socket, headless (no framebuffer needed). Run from the repo root:
#   bats tests/ctl.bats
setup_file() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  make -s bc101 tvctl fonts/big.psf fonts/small.psf > /dev/null
  export SOCK="$BATS_FILE_TMPDIR/ctl"
  ./bc101 -x -s "$SOCK" -t 300 > /dev/null 2> "$BATS_FILE_TMPDIR/show.err" &
  echo $! > "$BATS_FILE_TMPDIR/pid"
  for _ in $(seq 25); do ./bc101 ctl -s "$SOCK" ping > /dev/null 2>&1 && break; sleep 0.2; done
}
teardown_file() { kill "$(cat "$BATS_FILE_TMPDIR/pid")" 2> /dev/null || true; }
setup() { cd "$BATS_TEST_DIRNAME/.." || exit 1; }
ctl() { ./bc101 ctl -s "$SOCK" "$@"; }

@test "status reports the running loop" {
  run ctl status
  [ "$status" -eq 0 ]
  [[ $output == ok* ]]
  [[ $output == *"mode=auto"* ]]
  [[ $output == *"ino="* ]]
}

@test "list has standby, the loop and the extras with channel numbers" {
  run ctl list
  [[ $output == *" 0 black "*"standby"* ]]
  [[ $output == *" 1 boot "*"loop"* ]]
  [[ $output == *"20 explain"*"extra"* ]]
}

@test "tune by number holds the channel after the fade" {
  run ctl tune 7
  [ "$status" -eq 0 ]
  [ "$output" = "ok 7 plasma hold" ]
  sleep 1
  run ctl status
  [[ $output == *"scene=plasma ch=7"* ]]
  [[ $output == *"mode=hold next=plasma"* ]]
}

@test "tune by name" {
  ctl tune fire > /dev/null
  sleep 1
  run ctl status
  [[ $output == *"scene=fire ch=13"* ]]
}

@test "play shows a scene once and returns to the loop" {
  ctl play testcard 2 > /dev/null
  sleep 1
  run ctl status
  [[ $output == *"scene=testcard"* ]]
  [[ $output == *"mode=once"* ]]
  sleep 2.5
  run ctl status
  [[ $output == *"mode=auto"* ]]
  [[ $output != *"scene=testcard"* ]]
}

@test "next leaves hold, auto is the loop" {
  ctl tune 0 > /dev/null
  sleep 1
  run ctl status
  [[ $output == *"scene=black ch=0"* ]]
  run ctl next
  [ "$status" -eq 0 ]
  sleep 1
  run ctl status
  [[ $output == *"mode=auto"* ]]
  [[ $output != *"scene=black"* ]]
}

@test "unknown channel, bad seconds and unknown command are errors" {
  run ctl tune 99
  [ "$status" -eq 1 ]
  [ "$output" = "err no such channel: 99" ]
  run ctl play 7 99999
  [ "$status" -eq 1 ]
  run ctl bogus
  [ "$status" -eq 1 ]
  [[ $output == "err unknown command: bogus" ]]
}

@test "log lists the channels played so far" {
  run ctl log
  [ "$status" -eq 0 ]
  [[ $output == *" 7 plasma "*"fps"* ]]
}

@test "ctl without a show says so" {
  run ./bc101 ctl -s "$BATS_FILE_TMPDIR/nothing" status
  [ "$status" -eq 2 ]
  [[ $output == *"not reachable"* ]]
}

@test "tvctl is the same client without the show" {
  run ./tvctl -s "$SOCK" status
  [ "$status" -eq 0 ]
  [[ $output == *"mode="* ]]
  run ./tvctl -s "$SOCK"
  [ "$status" -eq 2 ]
}

@test "the zapper draws a frame" {
  run env COLUMNS=80 LINES=24 ./tvctl remote -s "$SOCK" --frame --no-color --ascii --name alice --admin
  [ "$status" -eq 0 ]
  [[ $output == *"ZAPPER"*"alice (admin)"* ]]
  [[ $output == *"PROGRAMME"* ]]
  [[ $output == *"07 plasma"* ]]
  [[ $output == *"0-9 Programm"* ]]
}

@test "reexec keeps the mode and the channel" {
  ctl tune 9 > /dev/null
  sleep 1
  run ctl reexec
  [ "$status" -eq 0 ]
  sleep 1.5
  run ctl status
  [ "$status" -eq 0 ]
  [[ $output == *"scene=hippie ch=9"* ]]
  [[ $output == *"mode=hold"* ]]
  [[ $output == *"up=1"* ]] || [[ $output == *"up=0"* ]]
}

@test "yield hands the screen over until the process is gone" {
  sleep 30 &
  local p=$!
  run ctl yield "$p"
  [ "$status" -eq 0 ]
  run ctl status
  [[ $output == *"yield=$p"* ]]
  run ctl yield $$
  [ "$status" -eq 1 ]
  [[ $output == *"taken"* ]]
  kill "$p"
  wait "$p" 2> /dev/null || true
  sleep 0.5
  run ctl status
  [[ $output == *"yield=0"* ]]
}

@test "a channel change takes the screen back" {
  sleep 30 &
  local p=$!
  ctl yield "$p" > /dev/null
  run ctl tune 7
  [ "$status" -eq 0 ]
  sleep 0.5
  # sleep 30 cannot end on its own by now: gone, or a zombie until this shell reaps it, means
  # the show killed it
  [[ ! -e /proc/$p ]] || [[ $(awk '{ print $3 }' "/proc/$p/stat") == Z ]]
  wait "$p" 2> /dev/null || true
  sleep 0.5
  run ctl status
  [[ $output == *"yield=0"* ]]
  ctl auto > /dev/null
}

@test "yield refuses what is not a running process" {
  run ctl yield 1
  [ "$status" -eq 1 ]
  run ctl yield abc
  [ "$status" -eq 1 ]
  run ctl yield 999999999
  [ "$status" -eq 1 ]
  run ctl status
  [[ $output == *"yield=0"* ]]
}
