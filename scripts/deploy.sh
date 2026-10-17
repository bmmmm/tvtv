#!/usr/bin/env bash
# Push the repo to the Pi, then build, install and restart there (tools/install.sh).
# Usage: scripts/deploy.sh [-n]   (-n = dry run: rsync changes and unit diff)
set -euo pipefail

HOST="${BC101_HOST:-tvtv-deploy}"
DIR="tvtv" # relative to the remote home
PARTS=(src slides system tools Makefile)
cd "$(dirname "$0")/.."

case "${1:-}" in
  "") ;;
  -n)
    rsync -ani --delete "${PARTS[@]}" "$HOST:$DIR/"
    home=$(ssh "$HOST" 'printf %s "$HOME"' </dev/null)
    rc=0
    sed "s|@BC101_DIR@|$home/$DIR|g" system/bc101.service |
      ssh "$HOST" 'diff -u /etc/systemd/system/bc101.service -' || rc=$?
    case $rc in
      0) echo "unit: unchanged" ;;
      1) echo "unit: would be updated" ;;
      *) echo "unit: could not compare (rc $rc)" >&2; exit 1 ;;
    esac
    exit 0
    ;;
  *)
    echo "usage: $0 [-n]" >&2
    exit 2
    ;;
esac

# Only tracked parts travel; build/, fonts/ and the binary stay on the Pi.
rsync -a --delete "${PARTS[@]}" "$HOST:$DIR/"
# shellcheck disable=SC2029 # $DIR is meant to expand locally
ssh "$HOST" "$DIR/tools/install.sh" </dev/null
