#!/usr/bin/env bash
# Push the repo to the Pi, then build, install and restart there (tools/install.sh).
# Usage: scripts/deploy.sh [-n | --setup]
#   -n       dry run: rsync changes and unit diff
#   --setup  first time, and after an update that brings new units or sudo rules: sync a copy to
#            ~/tvtv-deploy/show-setup of the deploying login and print the one command to run on
#            the Pi as a person with sudo (tools/setup-remote.sh: user zapper, /opt/tvtv, the
#            remote, the layout of docs/pi.md, exact sudo rules for the deploying login). The
#            deploying login itself has no password and no sudo for that, which is why this step
#            is a person's. TVTV_SETUP_HOST=<login that may sudo> runs it right away over ssh -t.
# Where to: TVTV_DEPLOY_HOST (and TVTV_SETUP_HOST, TVTV_HOME, TVTV_USER) from the environment or
# from .env at the top of the repo, see .env.example; the old BC101_* names still count.
# The systemd unit is rendered here and sent as is: the tree on the Pi belongs to the service
# user, and nothing that user can write may reach root.
set -euo pipefail

# shellcheck source=scripts/env.sh
source "$(dirname "$0")/env.sh"
load_env TVTV_DEPLOY_HOST TVTV_SETUP_HOST TVTV_HOME TVTV_USER
if [[ -z ${TVTV_DEPLOY_HOST:-} ]]; then
  echo "deploy: no TVTV_DEPLOY_HOST: set the deploying login's ssh destination in .env (see .env.example)" >&2
  exit 2
fi
HOST=$TVTV_DEPLOY_HOST                  # the deploying login; gets exact sudo rules from --setup
DIR="${TVTV_HOME:-/opt/tvtv}"           # on the Pi, owned by the service user
USER_REMOTE="${TVTV_USER:-zapper}"      # the service user: runs the show, takes the ssh commands
RSYNC_PATH="sudo -u $USER_REMOTE rsync" # write as the owner
STAGE=tvtv-deploy/show-setup            # in the deploying login's home (docs/pi.md, "Staging")
# In system/, installed to /etc/systemd/system/. The socket first: the service requires it.
UNITS=(tvtv-show.socket tvtv-show.service)
PARTS=(src slides system tools third_party Makefile)
cd "$(dirname "$0")/.."

render_unit() { sed "s|@TVTV_HOME@|$DIR|g; s|@TVTV_USER@|$USER_REMOTE|g" "system/$1"; }

# VERSION travels with the parts: tvtv-pi status shows what runs on the Pi.
ver=$(mktemp -d)
trap 'rm -rf "$ver"' EXIT
git describe --always --dirty --tags 2> /dev/null > "$ver/VERSION" || echo unknown > "$ver/VERSION"

case "${1:-}" in
  "") ;;
  -n)
    rsync -ani --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$ver/VERSION" "$HOST:$DIR/"
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
    deployer=$(ssh "$HOST" 'printf %s "$USER"' < /dev/null)
    home=$(ssh "$HOST" 'printf %s "$HOME"' < /dev/null)
    # The path comes from the other machine: only a plain one is used.
    [[ $home =~ ^/[A-Za-z0-9_./-]+$ ]] || { echo "unexpected home directory on $HOST: $home" >&2; exit 1; }
    rsync -a --delete --rsync-path="mkdir -p $STAGE && rsync" "${PARTS[@]}" "$ver/VERSION" "$HOST:$STAGE/"
    cmd="sudo $home/$STAGE/tools/setup-remote.sh --deployer $deployer && sudo rm -rf $home/$STAGE"
    if [[ -n ${TVTV_SETUP_HOST:-} ]]; then
      # shellcheck disable=SC2029 # $cmd is meant to expand locally
      ssh -t "$TVTV_SETUP_HOST" "$cmd"
    else
      cat << TXT
synced to $HOST:$STAGE/. Now log in to the Pi as a person who may sudo and run:

  $cmd

(or: TVTV_SETUP_HOST=<that login> scripts/deploy.sh --setup)
TXT
    fi
    exit 0
    ;;
  *)
    echo "usage: $0 [-n | --setup]" >&2
    exit 2
    ;;
esac

# A Pi set up before the units were called tvtv-show has no sudo rule for them: say so before
# anything changes there.
# shellcheck disable=SC2029 # the unit's name is meant to expand locally
if ! ssh "$HOST" "sudo -n -l tee /etc/systemd/system/${UNITS[0]}" > /dev/null 2>&1 < /dev/null; then
  echo "deploy: $HOST may not install ${UNITS[0]}: the Pi's setup predates it, run scripts/deploy.sh --setup" >&2
  exit 1
fi
# Only tracked parts travel; build/, fonts/, visuals/ and the binaries stay on the Pi.
rsync -a --delete --rsync-path="$RSYNC_PATH" "${PARTS[@]}" "$ver/VERSION" "$HOST:$DIR/"
for u in "${UNITS[@]}"; do
  # shellcheck disable=SC2029 # $u is meant to expand locally
  render_unit "$u" | ssh "$HOST" "sudo tee /etc/systemd/system/$u >/dev/null"
done
# Our install.sh, not the copy in $DIR: that tree is the service user's, and the deploying login
# may sudo.
# shellcheck disable=SC2029 # $DIR is meant to expand locally
ssh "$HOST" bash -s -- "$DIR" < tools/install.sh
