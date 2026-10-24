#!/usr/bin/env bash
# Runs on the Pi: build as the owner of the tree, reload and restart the service and check that
# it stays up. It needs sudo for systemctl only: the unit file never comes from the tree, which
# belongs to the unprivileged service user. For the same reason this script never runs from that
# tree once the remote is set up: scripts/deploy.sh pipes its own copy to bash on the Pi and
# tools/setup-remote.sh runs the setup's, each with the tree as the argument (a script the service
# user can write, run by the deploying login or by root, would lead from zapper to root).
#   tools/install.sh [tree]      default: the checkout this script is in
set -euo pipefail
# One function, called on the last line: piped to bash, all of it is read before anything runs.
install_show() {
  local UNIT=tvtv-show owner pid
  cd "${1:-$(dirname "$0")/..}"

  owner=$(stat -c %U .)
  # stdin is this script when it comes piped (deploy.sh): make must not read the rest of it
  if [[ $owner != "$(id -un)" ]]; then
    sudo -u "$owner" make < /dev/null
  else
    make < /dev/null
  fi

  sudo systemctl daemon-reload
  sudo systemctl enable --quiet $UNIT
  sudo systemctl restart $UNIT

  pid=$(systemctl show -p MainPID --value $UNIT)
  sleep 3
  if [[ "$(systemctl is-active $UNIT)" != active || "$(systemctl show -p MainPID --value $UNIT)" != "$pid" ]]; then
    echo "$UNIT did not stay up (journalctl -u $UNIT)" >&2
    exit 1
  fi
  echo "$UNIT running, pid $pid, user $owner"
}

install_show "$@"
