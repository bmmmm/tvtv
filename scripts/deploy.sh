#!/usr/bin/env bash
# Push the repo to the Pi, build it there and restart the show.
# Usage: scripts/deploy.sh [-n]   (-n = dry run, lists what rsync would change)
set -euo pipefail

HOST="${BC101_HOST:-tvtv-deploy}"
DIR="tvtv" # relative to the remote home
UNIT=/etc/systemd/system/bc101.service
cd "$(dirname "$0")/.."

# Only tracked parts travel; build/, fonts/ and the binary stay on the Pi.
if [[ "${1:-}" == "-n" ]]; then
  rsync -ani --delete src slides system Makefile "$HOST:$DIR/"
  exit 0
fi
rsync -a --delete src slides system Makefile "$HOST:$DIR/"

ssh "$HOST" "set -e; cd $DIR; make
  if ! cmp -s system/bc101.service $UNIT; then
    sudo tee $UNIT >/dev/null < system/bc101.service
    sudo systemctl daemon-reload
    sudo systemctl enable bc101
  fi
  sudo systemctl restart bc101
  systemctl is-active bc101"
