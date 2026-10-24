#!/usr/bin/env bash
# Push the repo to the Pi, then build, install and restart there (tools/install.sh).
# Usage: scripts/deploy.sh [-n | --setup]
#   -n       dry run: rsync changes and unit diff
#   --setup  first time: sync a copy to the deploying login's home and print the one command to
#            run on the Pi as a person with sudo (tools/setup-remote.sh: user zapper, /opt/tvtv,
#            the remote, exact sudo rules for the deploying login). The deploying login itself
#            has no password and no sudo for that, which is why this step is a person's.
#            BC101_SETUP_HOST=<login that may sudo> runs it right away over ssh -t instead.
# Where to: BC101_HOST (and BC101_SETUP_HOST, BC101_DIR, BC101_USER) from the environment or from
# .env at the top of the repo, see .env.example.
# The systemd unit is rendered here and sent as is: the tree on the Pi belongs to the service
# user, and nothing that user can write may reach root.
set -euo pipefail

# shellcheck source=scripts/env.sh
source "$(dirname "$0")/env.sh"
load_env BC101_HOST BC101_SETUP_HOST BC101_DIR BC101_USER
if [[ -z ${BC101_HOST:-} ]]; then
  echo "deploy: no BC101_HOST: set the deploying login's ssh destination in .env (see .env.example)" >&2
  exit 2
fi
HOST=$BC101_HOST                        # the deploying login; gets exact sudo rules from --setup
DIR="${BC101_DIR:-/opt/tvtv}"           # on the Pi, owned by the service user
USER_REMOTE="${BC101_USER:-zapper}"     # the service user: runs the show, takes the ssh commands
RSYNC_PATH="sudo -u $USER_REMOTE rsync" # write as the owner
# In system/, installed to /etc/systemd/system/. The socket first: the service requires it, and a
# Pi whose sudo rules predate the socket fails here, before the service file changes (run --setup).
UNITS=(bc101.socket bc101.service)
PARTS=(src slides system tools third_party Makefile)
cd "$(dirname "$0")/.."

render_unit() { sed "s|@BC101_DIR@|$DIR|g; s|@BC101_USER@|$USER_REMOTE|g" "system/$1"; }

case "${1:-}" in
  "") ;;
  -n)
    rsync -ani --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$HOST:$DIR/"
    for u in "${UNITS[@]}"; do
      rc=0
      f=/etc/systemd/system/$u
      # shellcheck disable=SC2029 # $f is meant to expand locally
      render_unit "$u" | ssh "$HOST" "if [ -f $f ]; then diff -u $f -; else cat > /dev/null; exit 3; fi" || rc=$?
      case $rc in
        0) echo "$u: unchanged" ;;
        1) echo "$u: would be updated" ;;
        3) echo "$u: would be created (needs --setup for its sudo rule)" ;;
        *) echo "$u: could not compare (rc $rc)" >&2; exit 1 ;;
      esac
    done
    exit 0
    ;;
  --setup)
    deployer=$(ssh "$HOST" 'printf %s "$USER"' </dev/null)
    home=$(ssh "$HOST" 'printf %s "$HOME"' </dev/null)
    rsync -a --delete "${PARTS[@]}" "$HOST:tvtv-setup/"
    cmd="sudo $home/tvtv-setup/tools/setup-remote.sh --deployer $deployer && sudo rm -rf $home/tvtv-setup"
    if [[ -n ${BC101_SETUP_HOST:-} ]]; then
      # shellcheck disable=SC2029 # $cmd is meant to expand locally
      ssh -t "$BC101_SETUP_HOST" "$cmd"
    else
      cat <<TXT
synced to $HOST:tvtv-setup/. Now log in to the Pi as a person who may sudo and run:

  $cmd

(or: BC101_SETUP_HOST=<that login> scripts/deploy.sh --setup)
TXT
    fi
    exit 0
    ;;
  *)
    echo "usage: $0 [-n | --setup]" >&2
    exit 2
    ;;
esac

# Only tracked parts travel; build/, fonts/, visuals/ and the binaries stay on the Pi.
rsync -a --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$HOST:$DIR/"
for u in "${UNITS[@]}"; do
  # shellcheck disable=SC2029 # $u is meant to expand locally
  render_unit "$u" | ssh "$HOST" "sudo tee /etc/systemd/system/$u >/dev/null"
done
# shellcheck disable=SC2029 # $DIR is meant to expand locally
ssh "$HOST" "$DIR/tools/install.sh" </dev/null
