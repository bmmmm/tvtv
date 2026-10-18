#!/usr/bin/env bash
# Push the repo to the Pi, then build, install and restart there (tools/install.sh).
# Usage: scripts/deploy.sh [-n | --setup]
#   -n       dry run: rsync changes and unit diff
#   --setup  first time: sync to a scratch directory and run tools/setup-remote.sh there as root
#            (user zapper, /opt/tvtv, the remote, sudo rules for the deploying user). sudo asks for
#            its password on the terminal; BC101_SETUP_HOST may name a login that is allowed to
#            sudo, the deploying login (BC101_HOST) stays what plain deploys use afterwards.
# The systemd unit is rendered here and sent as is: the tree on the Pi belongs to the service
# user, and nothing that user can write may reach root.
set -euo pipefail

HOST="${BC101_HOST:-tvtv-deploy}"       # the deploying login; gets exact sudo rules from --setup
SETUP_HOST="${BC101_SETUP_HOST:-$HOST}" # a login that may sudo (with a password), for --setup
DIR="${BC101_DIR:-/opt/tvtv}"           # on the Pi, owned by the service user
USER_REMOTE="${BC101_USER:-zapper}"     # the service user: runs the show, takes the ssh commands
RSYNC_PATH="sudo -u $USER_REMOTE rsync" # write as the owner
UNIT=/etc/systemd/system/bc101.service
PARTS=(src slides system tools Makefile)
cd "$(dirname "$0")/.."

render_unit() { sed "s|@BC101_DIR@|$DIR|g; s|@BC101_USER@|$USER_REMOTE|g" system/bc101.service; }

case "${1:-}" in
  "") ;;
  -n)
    rsync -ani --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$HOST:$DIR/"
    rc=0
    # shellcheck disable=SC2029 # $UNIT is meant to expand locally
    render_unit | ssh "$HOST" "diff -u $UNIT -" || rc=$?
    case $rc in
      0) echo "unit: unchanged" ;;
      1) echo "unit: would be updated" ;;
      *) echo "unit: could not compare (rc $rc)" >&2; exit 1 ;;
    esac
    exit 0
    ;;
  --setup)
    deployer=$(ssh "$HOST" whoami </dev/null)
    rsync -a --delete "${PARTS[@]}" "$SETUP_HOST:tvtv-setup/"
    # shellcheck disable=SC2029 # $deployer is meant to expand locally
    ssh -t "$SETUP_HOST" "sudo tvtv-setup/tools/setup-remote.sh --deployer '$deployer' && rm -rf tvtv-setup"
    exit 0
    ;;
  *)
    echo "usage: $0 [-n | --setup]" >&2
    exit 2
    ;;
esac

# Only tracked parts travel; build/, fonts/, visuals/ and the binaries stay on the Pi.
rsync -a --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$HOST:$DIR/"
# shellcheck disable=SC2029 # $UNIT is meant to expand locally
render_unit | ssh "$HOST" "sudo tee $UNIT >/dev/null"
# shellcheck disable=SC2029 # $DIR is meant to expand locally
ssh "$HOST" "$DIR/tools/install.sh" </dev/null
