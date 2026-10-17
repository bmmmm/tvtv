#!/usr/bin/env bash
# Push the repo to the Pi, build it there and restart the show.
# Usage: scripts/deploy.sh [-n]   (-n = dry run, lists what rsync would change)
set -euo pipefail

HOST="${BC101_HOST:-tvtv-deploy}"
DIR="tvtv" # relative to the remote home
cd "$(dirname "$0")/.."

case "${1:-}" in
  "") ;;
  -n)
    rsync -ani --delete src slides system Makefile "$HOST:$DIR/"
    exit 0
    ;;
  *)
    echo "usage: $0 [-n]" >&2
    exit 2
    ;;
esac

# Only tracked parts travel; build/, fonts/ and the binary stay on the Pi.
rsync -a --delete src slides system Makefile "$HOST:$DIR/"

ssh "$HOST" bash -s -- "$DIR" <<'REMOTE'
set -euo pipefail
cd "$HOME/$1"
make
unit=/etc/systemd/system/bc101.service
sed "s|@BC101_DIR@|$PWD|g" system/bc101.service > build/bc101.service
if ! cmp -s build/bc101.service "$unit"; then
  sudo tee "$unit" >/dev/null < build/bc101.service
  sudo systemctl daemon-reload
  sudo systemctl enable bc101
fi
sudo systemctl restart bc101
pid=$(systemctl show -p MainPID --value bc101)
sleep 3
if [[ "$(systemctl is-active bc101)" != active || "$(systemctl show -p MainPID --value bc101)" != "$pid" ]]; then
  echo "bc101 did not stay up (journalctl -u bc101)" >&2
  exit 1
fi
echo "bc101 running, pid $pid"
REMOTE
