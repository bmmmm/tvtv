#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Keys for the remote; run as root on the Pi (tools/setup-remote.sh installs it as tv-keys).
# One line per person in /etc/tvtv/authorized_keys, which sshd reads for user tv:
#   restrict,pty <type> <key> <who> <role>      role: op (zap) or admin (zap, upload, rm, fetch)
# Usage: tv-keys add <who> <op|admin> [pubkey-file]   (stdin without a file: ssh pi sudo tv-keys add bob op < bob.pub)
#        tv-keys rm <who>
#        tv-keys list
set -euo pipefail
KEYS=${TVTV_KEYS:-/etc/tvtv/authorized_keys}

usage() {
  echo "usage: tv-keys add <who> <op|admin> [pubkey-file] | rm <who> | list" >&2
  exit 2
}

write_keys() { # stdin -> $KEYS, atomically, world-readable (sshd reads it as user tv)
  local tmp
  tmp=$(mktemp "$(dirname "$KEYS")/.keys.XXXXXX")
  cat > "$tmp"
  chmod 0644 "$tmp"
  mv -f "$tmp" "$KEYS"
}

[[ -f $KEYS ]] || { install -d -m 0755 "$(dirname "$KEYS")"; : > "$KEYS"; chmod 0644 "$KEYS"; }

case ${1:-} in
  add)
    who=${2:-}
    role=${3:-}
    [[ $who =~ ^[a-z][a-z0-9_-]{0,23}$ ]] || { echo "who: a-z, 0-9, _ and -, up to 24 chars" >&2; exit 2; }
    [[ $role == op || $role == admin ]] || usage
    if [[ -n ${4:-} ]]; then pub=$(head -n 1 "$4"); else pub=$(head -n 1); fi
    read -r type blob _ <<< "$pub"
    [[ ${type:-} == ssh-* || ${type:-} == ecdsa-* || ${type:-} == sk-* ]] || { echo "not a public key line" >&2; exit 2; }
    tmp=$(mktemp)
    printf '%s %s\n' "$type" "$blob" > "$tmp"
    if ! fp=$(ssh-keygen -l -f "$tmp" 2>/dev/null); then rm -f "$tmp"; echo "ssh-keygen rejects that key" >&2; exit 2; fi
    rm -f "$tmp"
    { awk -v w="$who" '$4 != w' "$KEYS"; printf 'restrict,pty %s %s %s %s\n' "$type" "$blob" "$who" "$role"; } | write_keys
    echo "added $who ($role): ${fp#* }"
    ;;
  rm)
    who=${2:-}
    [[ -n $who ]] || usage
    grep -q " $who [a-z]*$" "$KEYS" || { echo "no key for $who" >&2; exit 1; }
    awk -v w="$who" '$4 != w' "$KEYS" | write_keys
    echo "removed $who"
    ;;
  list)
    while read -r _ type blob who role; do
      [[ -n ${role:-} ]] || continue
      fp=$(ssh-keygen -l -f /dev/stdin <<< "$type $blob" 2>/dev/null | awk '{ print $2 }') || fp='?'
      printf '%-24s %-5s %s\n' "$who" "$role" "$fp"
    done < "$KEYS"
    ;;
  *) usage ;;
esac
