#!/usr/bin/env bash
# Runs on the Pi: build as the owner of the tree, reload and restart the service and check that
# it stays up. Called by scripts/deploy.sh (which installs the unit first) and by
# tools/setup-remote.sh; can also be run directly. It needs sudo for systemctl only: the unit
# file never comes from this tree, which belongs to the unprivileged service user.
set -euo pipefail
cd "$(dirname "$0")/.."

owner=$(stat -c %U .)
if [[ $owner != "$(id -un)" ]]; then
  sudo -u "$owner" make
else
  make
fi

sudo systemctl daemon-reload
sudo systemctl enable --quiet bc101
sudo systemctl restart bc101

pid=$(systemctl show -p MainPID --value bc101)
sleep 3
if [[ "$(systemctl is-active bc101)" != active || "$(systemctl show -p MainPID --value bc101)" != "$pid" ]]; then
  echo "bc101 did not stay up (journalctl -u bc101)" >&2
  exit 1
fi
echo "bc101 running, pid $pid, user $owner"
