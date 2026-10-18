#!/usr/bin/env bash
# Push the repo to the Pi, then build, install and restart there (tools/install.sh).
# Usage: scripts/deploy.sh [-n | --setup]
#   -n       dry run: rsync changes and unit diff
#   --setup  first time: sync to a scratch directory and run tools/setup-remote.sh there (creates
#            user tv and /opt/tvtv, installs the remote); afterwards plain deploys target /opt/tvtv.
set -euo pipefail

HOST="${BC101_HOST:-tvtv-deploy}"
DIR="${BC101_DIR:-/opt/tvtv}" # on the Pi, owned by the service user tv
RSYNC_PATH="sudo -u tv rsync" # write as the owner
PARTS=(src slides system tools Makefile)
cd "$(dirname "$0")/.."

case "${1:-}" in
  "") ;;
  -n)
    rsync -ani --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$HOST:$DIR/"
    rc=0
    sed "s|@BC101_DIR@|$DIR|g; s|@BC101_USER@|tv|g" system/bc101.service |
      ssh "$HOST" 'diff -u /etc/systemd/system/bc101.service -' || rc=$?
    case $rc in
      0) echo "unit: unchanged" ;;
      1) echo "unit: would be updated" ;;
      *) echo "unit: could not compare (rc $rc)" >&2; exit 1 ;;
    esac
    exit 0
    ;;
  --setup)
    rsync -a --delete "${PARTS[@]}" "$HOST:tvtv-setup/"
    ssh "$HOST" 'sudo tvtv-setup/tools/setup-remote.sh && rm -rf tvtv-setup' </dev/null
    exit 0
    ;;
  *)
    echo "usage: $0 [-n | --setup]" >&2
    exit 2
    ;;
esac

# Only tracked parts travel; build/, fonts/, visuals/ and the binary stay on the Pi.
rsync -a --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$HOST:$DIR/"
# shellcheck disable=SC2029 # $DIR is meant to expand locally
ssh "$HOST" "$DIR/tools/install.sh" </dev/null
