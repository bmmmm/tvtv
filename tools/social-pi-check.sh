#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The social channels on the Pi itself, before they go on air (docs/remote.md, "Social channels"):
# the dem0 VM on the real VFP11 against the conformance vectors, whose ftz_* programs pass only if
# flush-to-zero is right, fuzzed, and its pacing; the snapshot reader on valid and broken files,
# watched the way the show watches its file, and fuzzed; and a bench of four programs of rising
# cost, which suggests BC101_DEM0_BUDGET for a VM time per picture.
# Run it from a copy of the tree, as any login, without sudo: scripts/social-check.sh syncs one to
# the Pi and runs it there. It needs cc, make and python3, and builds what it runs as the show is
# built (the Makefile, dem0 with -ffp-contract=off) in a fresh mktemp -d, which goes at the end:
# the show, /opt/tvtv and the tree are only read.
#   tools/social-pi-check.sh [--quick] [--target <ms>] [--budget <units>]
#     --quick           fewer rounds and pictures (the tests use it)
#     --target <ms>     the VM time per picture the suggested budget aims at, default 10
#     --budget <units>  the budget the bench runs with, default $BC101_DEM0_BUDGET or 600000
# Exit 0 only if every check passed. The bench takes wall-clock time, as the show does: a show
# running on the same core takes its share, and the summary says when the bench had to share.
set -uo pipefail
export LC_ALL=C # a decimal point in every number, whatever the login's locale

usage() {
  echo "usage: $0 [--quick] [--target <ms>] [--budget <units>]" >&2
  exit 2
}
quick=0
target=10
budget=${BC101_DEM0_BUDGET:-600000}
while (($#)); do
  case $1 in
    --quick) quick=1 ;;
    --target)
      target=${2:-}
      shift
      ;;
    --budget)
      budget=${2:-}
      shift
      ;;
    *) usage ;;
  esac
  shift
done
if ! [[ $target =~ ^[0-9]+([.][0-9]+)?$ ]] || ! awk -v t="$target" 'BEGIN { exit !(t > 0) }'; then
  echo "--target: milliseconds, more than 0" >&2
  usage
fi
# what the show takes as $BC101_DEM0_BUDGET (src/dem0.c)
if ! [[ $budget =~ ^[1-9][0-9]{0,9}$ ]] || ((budget > 1000000000)); then
  echo "--budget or \$BC101_DEM0_BUDGET: 1 to 1000000000 cost units per picture" >&2
  usage
fi
if ((quick)); then
  nfuzz=500
  frames=10
else
  nfuzz=5000
  frames=200
fi

cd "$(dirname "$0")/.." || exit 2
cc=${CC:-cc}
for f in Makefile src/dem0.c src/social.c tools/preview.c tools/gen-visuals.sh tests/dem0-check.c \
  tests/social-check.c tests/mksnap.py tests/dem0-vectors.txt; do
  [[ -f $f ]] || {
    echo "$PWD/$f is missing: run this in a copy of the tree (scripts/social-check.sh makes one)" >&2
    exit 2
  }
done
for c in "$cc" make python3; do
  command -v "$c" > /dev/null || {
    echo "$c is missing: the check needs cc, make and python3" >&2
    exit 2
  }
done
# Nothing from the login's environment: the show gets none of it (sudo -u zapper make, a unit
# without these), and the bench sets its own.
unset BC101_DEM0_BUDGET BC101_DEM0_MS BC101_SOCIAL CFLAGS VISUALS

work=$(mktemp -d "${TMPDIR:-/tmp}/tvtv-social-check.XXXXXX") || exit 2
trap 'rm -rf "${work:?}"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 141' PIPE # ssh gone: a write fails, and the directory still goes
trap 'exit 143' TERM

summary=()
nok=0
nbad=0
pass() {
  summary+=("PASS  $*")
  nok=$((nok + 1))
}
fail() {
  summary+=("FAIL  $*")
  nbad=$((nbad + 1))
}
step() { printf -- '-- %s\n' "$*"; }
indent() { sed 's/^/      /'; } # what a program said, under its failed check
exited() { if (($1 == 124)); then echo "timed out"; else echo "exit $1"; fi; }
snap() { python3 tests/mksnap.py "$@" > /dev/null; }

# The bench programs in mksnap.py's assembler, cost rising: the docs' plasma (25); bit operations
# on values past 2^31, where I() takes its slow path (131); the docs' tunnel under four layers of
# what posts use (207); the dear operations, pow exp log atan2, up to the limit (400). A short
# source: the long one would not fit its field.
programs=(plasma bits tunnel heavy)
rep() { # rep <n> <statements>: them n times
  local i
  for ((i = 0; i < $1; i++)); do printf ' %s' "$2"; done
}
bench_spec() { # bench_spec <program>: a snapshot spec for mksnap.py with it as the only program
  local asm src round
  case $1 in
    plasma)
      src="sin(x/16+t)*40+sin(y/8-t)*40+r*90"
      asm="in r0 x; const r1 16; div r2 r0 r1; in r3 t; add r4 r2 r3; sin r5 r4; const r6 40; mul r7 r5 r6;"
      asm+=" in r8 y; const r9 8; div r10 r8 r9; sub r11 r10 r3; sin r12 r11; mul r13 r12 r6; add r14 r7 r13;"
      asm+=" in r15 r; const r16 90; mul r17 r15 r16; add r18 r14 r17; out r18"
      ;;
    bits)
      src="bench: bit operations past 2^31"
      round="mul r10 r10 r11; shl r15 r10 r12; bxor r10 r10 r15; mul r10 r10 r11; shr r15 r10 r13;"
      round+=" bor r16 r10 r15; band r17 r10 r15; bxor r10 r16 r17; mul r10 r10 r11; shl r15 r10 r14;"
      round+=" bnot r15 r15; bxor r10 r10 r15;"
      asm="in r0 x; in r1 y; in r2 t; const r3 16777619; mul r4 r0 r3; const r5 2246822519; mul r6 r1 r5;"
      asm+=" bxor r7 r4 r6; const r8 1000; mul r9 r2 r8; bxor r10 r7 r9; const r11 65599; const r12 13;"
      asm+=" const r13 17; const r14 5;$(rep 5 "$round") const r18 23; shr r19 r10 r18; const r20 25;"
      asm+=" shr r21 r10 r20; const r22 27; shr r23 r10 r22; out r19 r21 r23"
      ;;
    tunnel)
      src="bench: the tunnel under four layers"
      round="mul r23 r3 r11; add r23 r23 r2; sin r24 r23; mul r25 r4 r14; sub r25 r25 r2; cos r26 r25;"
      round+=" hypot r27 r24 r26; atan2 r28 r26 r24; mul r29 r28 r18; floor r30 r29; fract r31 r29;"
      round+=" rnd r30 r30; step r31 r31 r30; mul r31 r31 r20; div r29 r27 r8; sqrt r29 r29;"
      round+=" add r22 r22 r29; sel r31 r22 r19; add r22 r22 r31; mul r3 r3 r27; add r4 r4 r24;"
      asm="in r0 x; in r1 y; in r2 t; in r3 u; in r4 v; in r5 r; in r6 a; const r7 0.1; add r8 r5 r7;"
      asm+=" const r9 1; div r10 r9 r8; const r11 4; mul r12 r6 r11; add r13 r12 r10; const r14 3;"
      asm+=" mul r15 r2 r14; add r16 r13 r15; sin r17 r16; const r18 99; mul r19 r17 r18; const r20 9;"
      asm+=" mul r21 r10 r20; add r22 r19 r21;$(rep 4 "$round") exp r23 r17; pow r24 r23 r27;"
      asm+=" add r22 r22 r24; out r22"
      ;;
    heavy)
      src="bench: pow exp log atan2, cost 400"
      round="pow r10 r8 r9; exp r11 r10; log r12 r11; atan2 r13 r12 r3; sin r14 r13; cos r15 r12;"
      round+=" hypot r16 r14 r15; sqrt r17 r16; mod r18 r17 r9; add r19 r18 r2; fract r8 r19;"
      asm="in r2 t; in r3 u; const r9 2.5; in r5 r; fract r8 r5;$(rep 7 "$round") pow r10 r8 r9;"
      asm+=" exp r11 r10; log r12 r11; atan2 r13 r12 r3; sin r14 r13; const r20 255; mul r21 r14 r20; out r21"
      ;;
  esac
  printf '{"progs": [{"asm": "%s", "source": "%s"}]}\n' "$asm" "$src"
}

# The bench as a table, and the budget for the target from the program with the fewest units per
# ms, the slowest for the budget: its units x the target / its VM time, rounded down to two digits.
bench_table() {
  awk -v target="$target" -v budget="$budget" -v frames="$frames" '
    {
      name[NR] = $1; cost[NR] = $2; cell[NR] = $3; n[NR] = $4; units[NR] = $5; avg[NR] = $6; worst[NR] = $7
      rate[NR] = $6 > 0 ? $5 / $6 : 0
      if (rate[NR] > 0 && (!slowest || rate[NR] < rate[slowest])) slowest = NR
      share = $8 > 0 ? ($9 + $10) / $8 : 1
      if (NR == 1 || share < least) least = share
    }
    END {
      printf "dem0 bench: budget %d, %d pictures each; units = (cost + 4) x cells, what the budget counts\n",
        budget, frames
      printf "  %-7s %4s  %-11s %8s  %9s  %9s  %8s\n", "program", "cost", "cells", "units", "vm avg", "worst",
        "units/ms"
      for (i = 1; i <= NR; i++)
        printf "  %-7s %4d  %2dx%-2d %5d  %8d  %6.2f ms  %6.2f ms  %8d%s\n", name[i], cost[i], cell[i], cell[i],
          n[i], units[i], avg[i], worst[i], rate[i], (avg[i] > 14 ? "  over 14 ms: coarser cells on air" : "")
      if (least < 0.8) {
        printf "  the bench had %d%% of the core: something else ran (the show?), its times are too long and\n",
          100 * least
        print "  the budget below is too low; for the numbers unplug the show (sudo systemctl stop"
        print "  tvtv-show.socket), run this again, then switch it back on (sudo systemctl start tvtv-show)"
      } else {
        printf "  the bench had the core to itself (%d%%)\n", 100 * least
      }
      if (!slowest) {
        print "no suggested budget: no program took a measurable time"
        exit
      }
      raw = int(units[slowest] * target / avg[slowest])
      p = 10 ^ (length(raw "") - 2)
      if (p < 1) p = 1
      printf "suggested BC101_DEM0_BUDGET for %s ms per picture, from %s, the fewest units per ms:\n", target,
        name[slowest]
      printf "  %d units x %s ms / %.2f ms = %d, rounded down: BC101_DEM0_BUDGET=%d\n", units[slowest], target,
        avg[slowest], raw, int(raw / p) * p
    }' "$work/bench.txt"
}

report() {
  echo "== summary"
  printf '%s\n' "${summary[@]}"
  if [[ -s $work/bench.txt ]]; then bench_table; fi
  echo "== $nok passed, $nbad failed"
}

cpu=$(awk -F'\t*: ' '/^Model/ { b = $2 } /^model name/ && !m { m = $2 } END { print (b ? b ", " : "") m }' \
  /proc/cpuinfo 2> /dev/null)
ccv=$("$cc" -dumpfullversion 2> /dev/null || "$cc" -dumpversion 2> /dev/null)
echo "== tvtv social check: $(date -Is), $(uname -srm), ${cpu:-?}, $cc ${ccv:-?}"

step "build in $work, as the show is built (a minute or two on the Pi 1)"
mkdir -p "$work/tools"
cp -R Makefile src "$work/" && cp tools/preview.c tools/gen-visuals.sh "$work/tools/" || exit 2
targets=(build/preview)
if [[ -f fonts/big.psf && -f fonts/small.psf ]]; then
  mkdir -p "$work/fonts" && cp fonts/big.psf fonts/small.psf "$work/fonts/" || exit 2
else
  targets+=(fonts/big.psf fonts/small.psf) # from the console fonts, as for the show
fi
obj=$work/build/src
# The two benches link the show's own objects: dem0.o as the Makefile builds it, -ffp-contract=off.
if make -C "$work" -s -j"$(nproc 2> /dev/null || echo 1)" "${targets[@]}" > "$work/build.log" 2>&1 &&
  "$cc" -O2 -Wall -I"$work/src" -o "$work/dem0-check" tests/dem0-check.c "$obj/dem0.o" "$obj/core.o" -lm \
    >> "$work/build.log" 2>&1 &&
  "$cc" -O2 -Wall -I"$work/src" -o "$work/social-check" tests/social-check.c "$obj/social.o" "$obj/dem0.o" \
    "$obj/core.o" -lm >> "$work/build.log" 2>&1; then
  pass "build: build/preview by the show's Makefile, dem0-check and social-check on its objects"
else
  tail -n 40 "$work/build.log" | indent
  fail "build: nothing else can run without it"
  report
  exit 1
fi

step "dem0 VM: the conformance vectors, $nfuzz random programs, the pacing"
# vectors <whose> <file> [<note>]: every program loads, every point is within the tolerance
vectors() {
  local out rc names
  out=$("$work/dem0-check" vectors "$2" 2>&1)
  rc=$?
  if ((rc == 0)) && [[ $out =~ ^ok\ ([0-9]+)\ programs,\ ([0-9]+)\ points,\ ([0-9]+)\ of ]]; then
    pass "dem0 vectors, $1: ${BASH_REMATCH[1]} programs, ${BASH_REMATCH[2]} points," \
      "${BASH_REMATCH[3]} of them to the bit${3:+; $3}"
    return
  fi
  head -n 30 <<< "$out" | indent
  names=$(sed -n 's/^FAIL \([^ :,]*\)[ :,].*/\1/p' <<< "$out" | grep -vx line | sort -u | tr '\n' ' ')
  [[ $names == *ftz_* ]] && names+="(flush-to-zero is wrong: the VFP's FZ, fpu_dem0 in src/dem0.c)"
  fail "dem0 vectors, $1: $(tail -n 1 <<< "$out")${names:+, in ${names% }}"
}
nftz=$(grep -c '^prog ftz_' tests/dem0-vectors.txt)
note=""
((nftz)) && note="flush-to-zero right in all $nftz ftz_* programs"
vectors "the bridge's" tests/dem0-vectors.txt "$note"
if python3 tests/mksnap.py vectors > "$work/py-vectors.txt"; then
  vectors "mksnap.py's" "$work/py-vectors.txt"
else
  fail "dem0 vectors, mksnap.py's: mksnap.py could not write them"
fi

out=$(timeout 600 "$work/dem0-check" fuzz "$nfuzz" 7 2>&1)
rc=$?
if ((rc == 0)) && [[ $out == "fuzz $nfuzz programs, "* ]]; then
  pass "dem0 fuzz: ${out#fuzz }, seed 7: nothing crashed"
else
  tail -n 10 <<< "$out" | indent
  fail "dem0 fuzz: $(exited "$rc")"
fi

# the cells after each picture of a program of cost 1, at the show's limit of 14 ms
pace() { BC101_DEM0_MS=14 BC101_DEM0_BUDGET=600000 "$work/dem0-check" pace "$@" 2> /dev/null; }
late=$(pace 15 15 15 15 15 15 15 15)
much=$(pace 100 100)
if [[ $late == "1 1 1 2 2 2 2 3" && $much == "2 3" ]]; then
  pass "dem0 pace: late every picture, coarser cells after 4 and after 8; four times late, at once"
else
  fail "dem0 pace: 15 ms x 8 gave cells '$late' (want 1 1 1 2 2 2 2 3), 100 ms x 2 '$much' (want 2 3)"
fi

step "snapshot reader: valid, broken, watched, $nfuzz mutated files"
snap "$work/full.bin" --full
# Latin-1 on the text page: bytes past 0x7F, where ARM's unsigned char is not x86's signed one
snap "$work/latin1.bin" - <<< '{"slots": [{"label": "GR\u00dcSSE", "text": "Sch\u00f6ne Gr\u00fc\u00dfe"}]}'
out=$("$work/social-check" "$work/full.bin" "$work/latin1.bin" 2>&1)
want="ok seq=1 flags=30 mode=normal bot=@tvtv@bonn.social pixels=3 progs=3 slots=3 announce=2/0/2000
ok seq=1 flags=30 mode=normal bot=@tvtv@bonn.social pixels=0 progs=0 slots=1 announce=0/0/0"
if [[ $out == "$want" ]]; then
  pass "snapshot, valid: read with all their parts, Latin-1 text too"
else
  indent <<< "$out"
  fail "snapshot, valid: not read as written"
fi

wrong=""
nrules=0
while read -r rule why; do
  nrules=$((nrules + 1))
  snap "$work/bad-$rule.bin" --break "$rule"
  out=$("$work/social-check" "$work/bad-$rule.bin" 2>&1)
  [[ $out == "bad: $why" ]] || wrong+="$rule: $out (want: bad: $why)"$'\n'
done << 'EOF'
crc crc
size-short size 19951, not 19952
slot-text-char slot[0]: text
obj-const-subnormal prog[0]: object: constant is subnormal
obj-read prog[0]: object: register read before it is written
obj-cost-big prog[0]: object: cost over 400
EOF
if [[ -z $wrong ]]; then
  pass "snapshot, broken: $nrules files, each turned down with its reason"
else
  indent <<< "${wrong%$'\n'}"
  fail "snapshot, broken: $(grep -c . <<< "$wrong") of $nrules not turned down as they should be"
fi

# as in tests/social.bats: the last valid one stays after a bad one, a file that is gone blanks
snap "$work/s5.bin" - <<< '{"seq": 5}'
snap "$work/s7.bin" - <<< '{"seq": 7}'
out=$("$work/social-check" watch "$work/watched.bin" "$work/s5.bin" "$work/bad-crc.bin" "$work/bad-obj-read.bin" \
  "$work/s7.bin" - "$work/bad-crc.bin" 2> /dev/null)
mapfile -t got <<< "$out"
if [[ ${#got[@]} -eq 6 && ${got[0]} == "5 ok" && ${got[1]} == "5 crc" &&
  ${got[2]} == "5 prog[0]: object: register read before it is written" && ${got[3]} == "7 ok" &&
  ${got[4]} == "none No such file"* && ${got[5]} == "none crc" ]]; then
  pass "snapshot, watched: the last valid one stays after a bad one, a file that is gone blanks the channels"
else
  indent <<< "$out"
  fail "snapshot, watched: not what the show must hold"
fi

out=$(timeout 600 "$work/social-check" fuzz "$work/full.bin" "$nfuzz" 101 2>&1)
rc=$?
if ((rc == 0)) && [[ $out =~ ^fuzz\ [0-9]+\ rounds,\ ([0-9]+)\ passed$ ]]; then
  pass "snapshot fuzz: $nfuzz mutated files, ${BASH_REMATCH[1]} of them valid: nothing crashed"
else
  tail -n 10 <<< "$out" | indent
  fail "snapshot fuzz: $(exited "$rc")"
fi

step "dem0 bench: ${programs[*]}, $frames pictures each, budget $budget"
# With the limit far away the cells stay what the budget gives: the pacing would make them coarser
# on a slow Pi, and the units of a picture unknown. Its CPU time against its wall-clock time says
# whether the bench had the core to itself.
: > "$work/bench.txt"
missing=""
TIMEFORMAT='%3R %3U %3S'
re="cost ([0-9]+), budget ([0-9]+): cells ([0-9]+)x[0-9]+, ([0-9]+) of them; vm avg +([0-9.]+) ms +worst +([0-9.]+) ms"
for name in "${programs[@]}"; do
  bench_spec "$name" | snap "$work/bench-$name.bin" -
  {
    time BC101_SOCIAL="$work/bench-$name.bin" BC101_DEM0_BUDGET=$budget BC101_DEM0_MS=1000000000 timeout 600 \
      "$work/build/preview" -f "$work/fonts" dem0 bench "$frames" > "$work/bench-$name.out" 2>&1
  } 2> "$work/bench-$name.time"
  rc=$?
  if ((rc == 0)) && [[ $(< "$work/bench-$name.out") =~ $re ]] && ((BASH_REMATCH[2] == budget)); then
    m=("${BASH_REMATCH[@]}")
    read -r real user sys < "$work/bench-$name.time"
    echo "$name ${m[1]} ${m[3]} ${m[4]} $(((m[1] + 4) * m[4])) ${m[5]} ${m[6]} ${real:-0} ${user:-0} ${sys:-0}" \
      >> "$work/bench.txt"
  else
    indent < "$work/bench-$name.out"
    missing+=" $name ($(exited "$rc"))"
  fi
done
if [[ -z $missing ]]; then
  pass "dem0 bench: ${#programs[@]} programs, cost $(awk '{ printf "%s%s", (NR > 1 ? " " : ""), $2 }' \
    "$work/bench.txt"), $frames pictures each"
else
  fail "dem0 bench: no VM time for$missing"
fi

report
((nbad == 0))
