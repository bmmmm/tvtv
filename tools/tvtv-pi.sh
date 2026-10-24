#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The Pi's housekeeping for every project on it (docs/pi.md). Each component writes a list of
# what it installed to /var/lib/tvtv/components/<name>; this reads those lists.
#   tvtv-pi status             the components, their versions and units
#   tvtv-pi du                 where the space goes: the SD card, the stick, each component, the
#                              usual suspects (apt cache, journal, swap, staging copies), packages
#   tvtv-pi check              the layout of docs/pi.md: lists sound, nothing unclaimed, nothing
#                              left over from before; exit 1 on a problem
#   tvtv-pi clean [-n]         free what can go without asking: apt's cache, old journal files,
#                              staging copies left behind; -n shows it only
#   tvtv-pi remove <name> [--purge]   stop and remove a component; its config, secrets and state
#                              stay unless --purge. The show is the base and is not removed here.
# Installed by tools/setup-remote.sh as /usr/local/sbin/tvtv-pi. status, du and check run as any
# user (du and check see more as root); clean and remove need root.
# TVTV_ROOT=<dir> puts every path under that directory (the tests).
set -euo pipefail
export LC_ALL=C

R=${TVTV_ROOT:-}
DB=/var/lib/tvtv/components
LEGACY=(/etc/systemd/system/bc101.service /etc/systemd/system/bc101.socket
  /etc/sudoers.d/tvtv-deploy /run/bc101)
# Copies a deploy leaves in a login's home: the ones of docs/pi.md, then the ones from before.
STAGE_GLOBS=('/home/*/tvtv-deploy' '/root/tvtv-deploy')
OLD_STAGE_GLOBS=('/home/*/tvtv-setup' '/home/*/tvtv-social-check' '/home/*/tvtv-social-setup')
# Where a component may put things (docs/pi.md, "Layout"); a list naming anything else is refused.
PATH_RE='^/(usr/local/(lib|share)/tvtv/[^/]|usr/local/s?bin/(tvtv-|tv-)|etc/tvtv/[^/]|etc/sudoers\.d/tvtv-|etc/[a-z0-9._-]+(/[a-z0-9._-]+)?\.d/50-tvtv|var/lib/(private/)?tvtv-|opt/tvtv(/|$)|srv/tvtv(/|$))'
UNIT_RE='^tvtv-[a-z0-9-]+@?\.(service|socket|timer|path)$'
NAME_RE='^[a-z][a-z0-9-]{0,23}$'

die() { echo "tvtv-pi: $2" >&2; exit "$1"; }
need_root() { [[ $EUID -eq 0 || -n $R ]] || die 1 "$1 needs root (sudo tvtv-pi $1)"; }
p() { printf '%s%s' "$R" "$1"; } # a path of the Pi, under TVTV_ROOT

# ---------- the component lists ----------
# load <name>: fills M_* from $DB/<name>; M_ERR collects what is wrong with it.
load() {
  local f key val
  f=$(p "$DB/$1")
  M_NAME="" M_REPO="" M_VER="" M_REMOVED="" M_UNITS=() M_FILES=() M_KEEP=() M_NEEDS=() M_ERR=()
  [[ -f $f ]] || { M_ERR+=("no list $DB/$1"); return 0; }
  while IFS= read -r line || [[ -n $line ]]; do
    [[ -z ${line//[[:space:]]/} || $line == \#* ]] && continue
    key=${line%% *} val=${line#* }
    [[ $key != "$line" && -n $val ]] || { M_ERR+=("line without a value: $line"); continue; }
    case $key in
      name) M_NAME=$val ;;
      repo) M_REPO=$val ;;
      version) M_VER=$val ;;
      removed) M_REMOVED=$val ;; # what remove leaves: the kept paths and when
      version-file)
        if [[ $val =~ $PATH_RE && $val != *..* ]]; then
          M_VER=$(head -n 1 "$(p "$val")" 2> /dev/null || echo "? ($val missing)")
        else M_ERR+=("version-file outside the layout: $val"); fi
        ;;
      unit) if [[ $val =~ $UNIT_RE ]]; then M_UNITS+=("$val"); else M_ERR+=("unit not named tvtv-*: $val"); fi ;;
      file | keep)
        if [[ $val =~ $PATH_RE && $val != *..* && $val != *//* && $val =~ ^[A-Za-z0-9._/@-]+$ ]]; then
          if [[ $key == file ]]; then M_FILES+=("$val"); else M_KEEP+=("$val"); fi
        else M_ERR+=("$key outside the layout of docs/pi.md: $val"); fi
        ;;
      needs) M_NEEDS+=("$val") ;;
      *) M_ERR+=("unknown key: $key") ;;
    esac
  done < "$f"
  [[ $M_NAME == "$1" ]] || M_ERR+=("name is '${M_NAME}', the file is $1")
}

components() { # the names, base first
  local f n out=()
  for f in "$(p "$DB")"/*; do
    [[ -f $f ]] || continue
    n=${f##*/}
    [[ $n =~ $NAME_RE ]] || continue
    if [[ $n == show ]]; then out=(show "${out[@]}"); else out+=("$n"); fi
  done
  ((${#out[@]} == 0)) || printf '%s\n' "${out[@]}"
}

unit_state() { # "<enabled> <active>", or "- -" without systemd
  local e a
  e=$(systemctl is-enabled "$1" 2> /dev/null) || true
  a=$(systemctl is-active "$1" 2> /dev/null) || true
  printf '%s %s' "${e:--}" "${a:--}"
}

kb() { # size in KB of the paths that exist, following a symlink given itself (DynamicUser state)
  local t=0 s x
  for x in "$@"; do
    x=$(p "$x")
    [[ -e $x ]] || continue
    s=$(du -skx "$(readlink -f "$x")" 2> /dev/null | cut -f1) || true
    t=$((t + ${s:-0}))
  done
  echo "$t"
}
# KB as K, M or G; one decimal below 10. The same in awk for df (fs_line).
HUMAN_AWK='function h(k,  i, u, f) { split("K M G", u); i = 1; while (k >= 1024 && i < 3) { k /= 1024; i++ }
  f = (k < 10 && i > 1) ? "%.1f%s" : "%d%s"; return sprintf(f, k, u[i]) }'
human() { awk -v k="$1" "$HUMAN_AWK"' BEGIN { print h(k) }'; }

# ---------- status ----------
cmd_status() {
  local n u st present missing f
  mapfile -t names < <(components)
  ((${#names[@]})) || { echo "no components in $DB: run tvtv's setup (docs/pi.md)"; return 0; }
  for n in "${names[@]}"; do
    load "$n"
    if [[ -n $M_REMOVED ]]; then
      printf '%-8s removed %s, kept: %s\n' "$n" "$M_REMOVED" "${M_KEEP[*]:--}"
      continue
    fi
    printf '%-8s %-24s %s\n' "$n" "${M_VER:--}" "${M_REPO:--}"
    for u in "${M_UNITS[@]}"; do
      st=$(unit_state "$u")
      printf '  %-26s %s\n' "$u" "$st"
    done
    present=0 missing=()
    for f in "${M_FILES[@]}"; do
      if [[ -e $(p "$f") ]]; then present=$((present + 1)); else missing+=("$f"); fi
    done
    printf '  files %d/%d%s\n' "$present" "${#M_FILES[@]}" "${missing[*]:+, missing: ${missing[*]}}"
    ((${#M_ERR[@]} == 0)) || printf '  problem: %s\n' "${M_ERR[@]}"
  done
}

# ---------- du ----------
fs_line() { # <label> <path>: used and free of the file system under it
  df -Pk "$(p "$2")" 2> /dev/null | awk -v l="$1" "$HUMAN_AWK"'
    NR == 2 { printf "%-14s %s used of %s, %s free (%s)\n", l, h($3), h($2), h($4), $5 }'
}
row() { [[ $2 -gt 0 ]] && printf '  %-12s %7s  %s\n' "$1" "$(human "$2")" "$3"; return 0; }

cmd_du() {
  local n x k paths
  fs_line "SD card /" /
  if findmnt -n -t ext4 "$(p /srv/tvtv)" > /dev/null 2>&1; then fs_line "stick" /srv/tvtv
  else echo "stick          not mounted (mounts when used; docs/pi.md, \"The USB stick\")"; fi
  [[ $EUID -eq 0 || -n $R ]] || echo "(as $(id -un): some sizes are short, sudo tvtv-pi du sees all)"
  echo "components:"
  mapfile -t names < <(components)
  for n in "${names[@]}"; do
    load "$n"
    paths=()
    for x in "${M_FILES[@]}" "${M_KEEP[@]}"; do [[ $x == /srv/* ]] || paths+=("$x"); done
    k=$(kb "${paths[@]}")
    printf '  %-12s %7s\n' "$n" "$(human "$k")"
    if [[ $n == show ]]; then
      for x in build third_party visuals doom; do
        row "  $x/" "$(kb "/opt/tvtv/$x")" "/opt/tvtv/$x"
      done
    fi
  done
  echo "system:"
  row "apt cache" "$(kb /var/cache/apt/archives)" "/var/cache/apt/archives (tvtv-pi clean)"
  row "apt lists" "$(kb /var/lib/apt/lists)" "/var/lib/apt/lists (apt update makes them again)"
  row "journal" "$(kb /var/log/journal)" "/var/log/journal (at most 32M, tvtv-pi clean)"
  row "swap file" "$(kb /var/swap)" "/var/swap (docs/pi.md, \"Room on the SD card\")"
  row "logs" "$(kb /var/log)" "/var/log"
  for x in $(staging "${STAGE_GLOBS[@]}"); do row "staging" "$(kb "$x")" "$x"; done
  for x in $(staging "${OLD_STAGE_GLOBS[@]}"); do row "left over" "$(kb "$x")" "$x (tvtv-pi clean)"; done
  if command -v dpkg-query > /dev/null && [[ -z $R ]]; then
    echo "largest packages (dpkg-query; before removing any: docs/pi.md):"
    dpkg-query -W -f '${Installed-Size}\t${Package}\n' 2> /dev/null | sort -rn | head -n 12 |
      while IFS=$'\t' read -r k x; do printf '  %-30s %7s\n' "$x" "$(human "$k")"; done
  fi
}

staging() { # the Pi's paths matching the globs, without TVTV_ROOT
  local g m
  for g in "$@"; do
    for m in $(p "$g"); do [[ -e $m ]] && printf '%s\n' "${m#"$R"}"; done
  done
  return 0
}

# ---------- check ----------
cmd_check() {
  local n x problems=0 claimed=() u dep
  problem() { echo "problem: $*"; problems=$((problems + 1)); }
  mapfile -t names < <(components)
  local live=()
  for n in "${names[@]}"; do load "$n"; [[ -n $M_REMOVED ]] || live+=("$n"); done
  [[ " ${live[*]} " == *" show "* ]] || problem "no list for the base (show): run tvtv's setup"
  for n in "${names[@]}"; do
    load "$n"
    for x in "${M_ERR[@]}"; do problem "$n: $x"; done
    claimed+=("${M_KEEP[@]}" "$DB/$n")
    [[ -z $M_REMOVED ]] || continue
    for x in "${M_FILES[@]}"; do [[ -e $(p "$x") ]] || problem "$n: missing $x"; done
    for u in "${M_UNITS[@]}"; do [[ -e $(p "/etc/systemd/system/$u") ]] || problem "$n: missing unit $u"; done
    for dep in "${M_NEEDS[@]}"; do [[ " ${live[*]} " == *" $dep "* ]] || problem "$n needs $dep, which is not installed"; done
    # a unit's drop-ins (systemctl edit) are the site's settings for it, so they count as its own
    for u in "${M_UNITS[@]}"; do claimed+=("/etc/systemd/system/$u" "/etc/systemd/system/$u.d"); done
    claimed+=("${M_FILES[@]}")
  done
  # Anything in the shared places that no list names belongs to nobody: an old install, a manual copy.
  for x in "$R"/usr/local/lib/tvtv/* "$R"/etc/tvtv/* "$R"/etc/systemd/system/tvtv-* \
    "$R"/etc/sudoers.d/tvtv* "$R"/usr/local/sbin/tvtv-* "$R"/usr/local/sbin/tv-*; do
    [[ -e $x || -L $x ]] || continue
    x=${x#"$R"}
    is_claimed "$x" "${claimed[@]}" || problem "unclaimed: $x (no list in $DB names it)"
  done
  for x in "${LEGACY[@]}"; do [[ ! -e $(p "$x") ]] || problem "left over from before docs/pi.md: $x (run tvtv's setup again)"; done
  if grep -qE '^/dev/sda1[[:space:]]+/mnt[[:space:]]' "$(p /etc/fstab)" 2> /dev/null; then
    problem "left over: the stick's old /mnt line in /etc/fstab (run tvtv's setup again)"
  fi
  for x in $(staging "${OLD_STAGE_GLOBS[@]}"); do problem "left over: $x (tvtv-pi clean)"; done
  x=$(df -Pk "$(p /)" 2> /dev/null | awk 'NR == 2 { sub("%", "", $5); print $5 }')
  [[ -z $x || $x -lt 90 ]] || problem "the SD card is ${x}% full (tvtv-pi du)"
  if ((problems)); then echo "$problems problem(s)"; return 1; fi
  echo "ok: ${#live[@]} component(s), layout as in docs/pi.md"
}
is_claimed() {
  local x=$1 c
  shift
  for c in "$@"; do [[ $x == "$c" || $x == "$c"/* ]] && return 0; done
  return 1
}

# ---------- clean ----------
cmd_clean() {
  local dry=0 x before after
  [[ ${1:-} != -n ]] || dry=1
  ((dry)) || need_root clean
  run() { if ((dry)); then echo "would: $*"; else echo "+ $*"; "$@" || echo "  (failed: $?)"; fi; }
  before=$(df -Pk "$(p /)" | awk 'NR == 2 { print $4 }')
  if compgen -G "$(p /var/cache/apt/archives)/*.deb" > /dev/null; then run apt-get clean; fi
  [[ ! -d $(p /var/log/journal) ]] || run journalctl --vacuum-size=16M
  # Pure copies: a deploy makes them again. The social setup copy of before goes only once the
  # new one (tvtv-deploy/social) is there, as its install.sh is what `token` runs.
  for x in $(staging "${OLD_STAGE_GLOBS[@]}" '/home/*/tvtv-deploy/show-setup'); do
    if [[ $x == */tvtv-social-setup && ! -d $(p "${x%/*}/tvtv-deploy/social") ]]; then
      echo "kept: $x (tvtv-social's setup copy; it goes after its next deploy/deploy.sh)"
      continue
    fi
    run rm -rf -- "$(p "$x")"
  done
  ((dry)) && return 0
  after=$(df -Pk "$(p /)" | awk 'NR == 2 { print $4 }')
  echo "freed $(human $((after > before ? after - before : 0)))"
}

# ---------- remove ----------
cmd_remove() {
  local n=${1:-} purge=0 x other u keep
  [[ ${2:-} != --purge ]] || purge=1
  [[ $n =~ $NAME_RE ]] || die 2 "usage: tvtv-pi remove <name> [--purge]"
  [[ $n != show ]] || die 1 "the show is the base of every component; to clear the Pi: docs/pi.md, \"Removing\""
  [[ -f $(p "$DB/$n") ]] || die 1 "no component $n (tvtv-pi status)"
  need_root remove
  for other in $(components); do
    [[ $other == "$n" ]] && continue
    load "$other"
    [[ -z $M_REMOVED ]] || continue
    for x in "${M_NEEDS[@]}"; do [[ $x != "$n" ]] || die 1 "$other needs $n: remove $other first"; done
  done
  load "$n"
  [[ -z $M_REMOVED || $purge -eq 1 ]] || die 1 "$n was removed on $M_REMOVED; --purge removes what was kept"
  # A list that names anything outside the layout is not acted on at all.
  ((${#M_ERR[@]} == 0)) || die 1 "the list of $n is not sound, nothing removed: ${M_ERR[*]}"
  for u in "${M_UNITS[@]}"; do systemctl disable --now --quiet "$u" 2> /dev/null || true; done
  for u in "${M_UNITS[@]}"; do rm -f -- "$(p "/etc/systemd/system/$u")"; done
  for x in "${M_FILES[@]}"; do rm -rf -- "$(p "$x")"; done
  if ((purge)); then
    for x in "${M_KEEP[@]}"; do rm -rf -- "$(p "$x")"; done
    for u in "${M_UNITS[@]}"; do rm -rf -- "$(p "/etc/systemd/system/$u.d")"; done
    rm -f -- "$(p "$DB/$n")"
  else
    # What stays is still named, so check knows whose it is and a later --purge finds it.
    keep=("${M_KEEP[@]}")
    for u in "${M_UNITS[@]}"; do [[ ! -d $(p "/etc/systemd/system/$u.d") ]] || keep+=("/etc/systemd/system/$u.d"); done
    {
      printf '# what was kept of %s; tvtv-pi remove %s --purge removes it\nname %s\nremoved %s\n' \
        "$n" "$n" "$n" "$(date +%F)"
      ((${#keep[@]} == 0)) || printf 'keep %s\n' "${keep[@]}"
    } > "$(p "$DB/$n.new")"
    mv -f -- "$(p "$DB/$n.new")" "$(p "$DB/$n")"
    ((${#keep[@]} == 0)) || echo "kept (--purge removes them): ${keep[*]}"
  fi
  systemctl daemon-reload 2> /dev/null || true
  echo "removed $n"
}

case ${1:-} in
  status) cmd_status ;;
  du) cmd_du ;;
  check) cmd_check ;;
  clean) shift; cmd_clean "$@" ;;
  remove) shift; cmd_remove "$@" ;;
  *) sed -n '3,15s/^# \{0,1\}//p' "$0" >&2; exit 2 ;;
esac
