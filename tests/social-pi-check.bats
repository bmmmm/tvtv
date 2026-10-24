#!/usr/bin/env bats
# tools/social-pi-check.sh, the social channels' check for the Pi, here in its quick mode: every
# check passes, the bench runs with the budget it is given, the suggested budget's arithmetic, the
# tree is only read; a check that fails fails the run. And scripts/social-check.sh against
# stand-ins for rsync and ssh that make a directory here the Pi's home, with its host from the
# environment or a settings file (scripts/env.sh), never from this checkout's .env.
# Run from the repo root: bats tests/social-pi-check.bats   (needs python3 and rsync)
setup_file() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  local t=$BATS_FILE_TMPDIR rc=0
  mkdir -p "$t/tmp"
  touch "$t/stamp"
  TMPDIR="$t/tmp" tools/social-pi-check.sh --quick --target 8 --budget 300000 > "$t/out" 2>&1 || rc=$?
  echo "$rc" > "$t/rc"
}
setup() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  T=$BATS_FILE_TMPDIR
  unset BC101_HOST
  export TVTV_ENV=$BATS_TEST_TMPDIR/no-settings
}

# fake_pi <dir> <home>: rsync and ssh in <dir> that take <home> for the Pi's home directory: rsync
# copies host:path to <home>/path, ssh runs its command there; both write down how they were called
fake_pi() {
  mkdir -p "$1" "$2"
  cat > "$1/rsync" << SH
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$1/rsync.args"
args=()
for a in "\$@"; do
  case \$a in
    -*) args+=("\$a") ;;
    *:*) args+=("$2/\${a#*:}") ;;
    *) args+=("\$a") ;;
  esac
done
exec $(command -v rsync) "\${args[@]}"
SH
  cat > "$1/ssh" << SH
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$1/ssh.args"
shift
cd "$2" && HOME="$2" exec bash -c "\$*"
SH
  chmod +x "$1/rsync" "$1/ssh"
}

@test "quick: every check passes and says so, exit 0" {
  cat "$T/out"
  [ "$(cat "$T/rc")" -eq 0 ]
  run cat "$T/out"
  [[ $output == *"== 10 passed, 0 failed" ]]
  [[ $output != *"FAIL "* ]]
  for check in "build" "dem0 vectors, the bridge's" "dem0 vectors, mksnap.py's" "dem0 fuzz" "dem0 pace" \
    "snapshot, valid" "snapshot, broken" "snapshot, watched" "snapshot fuzz" "dem0 bench"; do
    [[ $output == *$'\n'"PASS  $check: "* ]] || { echo "no PASS for $check"; return 1; }
  done
  [[ $output =~ flush-to-zero\ right\ in\ all\ [1-9][0-9]*\ ftz_\*\ programs ]]
}

@test "quick: the bench, four programs of rising cost in the cells the budget gives, the VM time of each" {
  run grep -cE '^  (plasma|bits|tunnel|heavy) ' "$T/out"
  [ "$output" -eq 4 ]
  [[ $(cat "$T/out") == *"dem0 bench: budget 300000, 10 pictures each;"* ]]
  # program, cost, cells, how many, units = (cost + 4) x how many: the finest that fits 300000
  run awk '/^  (plasma|bits|tunnel|heavy) / { print $1, $2, $3, $4, $5 }' "$T/out"
  [ "${lines[0]}" = "plasma 25 4x4 6480 187920" ]
  [ "${lines[1]}" = "bits 131 8x8 1620 218700" ]
  [ "${lines[2]}" = "tunnel 207 9x9 1280 270080" ]
  [ "${lines[3]}" = "heavy 400 12x12 720 290880" ]
  # every one with a VM time per picture and a worst one no shorter
  run awk '/^  (plasma|bits|tunnel|heavy) / && !($6 > 0 && $7 == "ms" && $8 >= $6 && $9 == "ms") { print }' "$T/out"
  [ -z "$output" ]
  grep -Eq '^  the bench had (the core to itself \([0-9]+%\)|[0-9]+% of the core)' "$T/out"
}

@test "quick: the suggested budget is units x target / VM time of the slowest per unit, rounded down to two digits" {
  run awk '/^  (plasma|bits|tunnel|heavy) / { r = $5 / $6; if (!n++ || r < least) { least = r; p = $1 " " $5 " " $6 } }
    END { print p }' "$T/out"
  read -r name units avg <<< "$output"
  raw=$(awk -v u="$units" -v a="$avg" 'BEGIN { printf "%d", u * 8 / a }')
  # two digits, the rest zeros
  sug=${raw:0:2}
  for ((i = 2; i < ${#raw}; i++)); do sug+="0"; done
  grep -qxF "suggested BC101_DEM0_BUDGET for 8 ms per picture, from $name, the fewest units per ms:" "$T/out"
  grep -qxF "  $units units x 8 ms / $avg ms = $raw, rounded down: BC101_DEM0_BUDGET=$sug" "$T/out"
}

@test "quick: the work directory goes, the tree is only read" {
  [ -z "$(ls -A "$T/tmp")" ]
  run find Makefile src tools tests scripts -newer "$T/stamp"
  [ -z "$output" ]
}

@test "options that are not the check's are turned down before anything is built" {
  for bad in "--target 0" "--target ms" "--budget 0" "--budget 1000000001" "--budget 6e5" "-n" "--quick extra"; do
    run tools/social-pi-check.sh $bad
    [ "$status" -eq 2 ] || { echo "$bad: $status"; return 1; }
    [[ $output == *"usage: "* && $output != *"== tvtv social check"* ]]
  done
  run env BC101_DEM0_BUDGET=lots tools/social-pi-check.sh
  [ "$status" -eq 2 ]
  [[ $output == *"BC101_DEM0_BUDGET: 1 to 1000000000"* ]]
}

@test "scripts/social-check.sh: the parts to ~/tvtv-social-check on BC101_HOST, the check run there" {
  fake_pi "$T/bin" "$T/pi"
  run env PATH="$T/bin:$PATH" BC101_HOST=pi-test TMPDIR="$T/tmp" scripts/social-check.sh --quick
  echo "$output"
  [ "$status" -eq 0 ]
  [[ $output == *"== 10 passed, 0 failed" ]]
  # the check's defaults: the show's budget, 10 ms
  [[ $output == *"dem0 bench: budget 600000, 10 pictures each;"* ]]
  [[ $output == *"suggested BC101_DEM0_BUDGET for 10 ms per picture"* ]]
  [ "$(cat "$T/bin/rsync.args")" = "-aR --delete Makefile src tools/social-pi-check.sh tools/preview.c \
tools/gen-visuals.sh tests/dem0-check.c tests/social-check.c tests/mksnap.py tests/dem0-vectors.txt pi-test:tvtv-social-check/" ]
  [ "$(cat "$T/bin/ssh.args")" = "pi-test bash tvtv-social-check/tools/social-pi-check.sh --quick" ]
  # the copy is the parts, src whole and nothing else from the tree
  run bash -c 'cd "$1/tvtv-social-check" && find . -type f ! -path "./src/*" | LC_ALL=C sort' _ "$T/pi"
  [ "$output" = "$(printf './%s\n' Makefile tests/dem0-check.c tests/dem0-vectors.txt tests/mksnap.py \
    tests/social-check.c tools/gen-visuals.sh tools/preview.c tools/social-pi-check.sh)" ]
  diff -r src "$T/pi/tvtv-social-check/src"
  # a file gone from src goes there too, at the next run
  touch "$T/pi/tvtv-social-check/src/gone.c"
  run env PATH="$T/bin:$PATH" BC101_HOST=pi-test scripts/social-check.sh -n
  [[ $output == *"deleting   src/gone.c"* ]]
}

@test "scripts/social-check.sh -n: the host from the settings, what rsync would copy, nothing copied, nothing run" {
  fake_pi "$T/bin3" "$T/pi3"
  printf 'BC101_HOST=pi-from-env   # the deploying login\n' > "$T/settings3"
  run env PATH="$T/bin3:$PATH" TVTV_ENV="$T/settings3" scripts/social-check.sh -n
  [ "$status" -eq 0 ]
  [[ $output == *"tools/social-pi-check.sh"* && $output == *"src/dem0.c"* && $output == *"tests/mksnap.py"* ]]
  [[ $(cat "$T/bin3/rsync.args") == "-aniR --delete "*" pi-from-env:tvtv-social-check/" ]]
  [ ! -e "$T/bin3/ssh.args" ]
  [ ! -e "$T/pi3/tvtv-social-check" ]
}

@test "scripts/social-check.sh without BC101_HOST says where to set it, copies nothing, runs nothing" {
  fake_pi "$T/bin4" "$T/pi4"
  run env PATH="$T/bin4:$PATH" scripts/social-check.sh --quick
  [ "$status" -eq 2 ]
  [[ $output == *"BC101_HOST"*".env.example"* ]]
  [ ! -e "$T/bin4/rsync.args" ]
  [ ! -e "$T/bin4/ssh.args" ]
}

@test "a check that fails fails the run, the others still run, and scripts/social-check.sh returns it" {
  # a copy of the tree in which a point of the flush-to-zero program ftz_chain is off by 1
  mkdir -p "$T/tree"
  cp -R Makefile src tools tests scripts "$T/tree/"
  awk '/^prog / { p = /^prog ftz_chain / } p && /^at / && !done { $NF = $NF + 1; done = 1 } { print }' \
    tests/dem0-vectors.txt > "$T/tree/tests/dem0-vectors.txt"
  run cmp -s tests/dem0-vectors.txt "$T/tree/tests/dem0-vectors.txt" # "! cmp" would not fail a bats test
  [ "$status" -eq 1 ]
  fake_pi "$T/bin2" "$T/pi2"
  run env PATH="$T/bin2:$PATH" BC101_HOST=pi-test TMPDIR="$T/tmp" "$T/tree/scripts/social-check.sh" --quick
  echo "$output"
  [ "$status" -eq 1 ]
  [[ $output == *$'\n'"FAIL  dem0 vectors, the bridge's: 1 failures, in ftz_chain (flush-to-zero is wrong"* ]]
  [[ $output == *$'\n'"PASS  dem0 vectors, mksnap.py's: "* ]]
  [[ $output == *$'\n'"PASS  dem0 bench: "* ]]
  [[ $output == *"== 9 passed, 1 failed" ]]
}
