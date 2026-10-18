#!/usr/bin/env bash
# Runs on the Pi: build, install/refresh the systemd unit, restart the show and check that it stays
# up. Called by scripts/deploy.sh and tools/setup-remote.sh; can also be run directly.
set -euo pipefail
cd "$(dirname "$0")/.."

# The directory ends up in the unit's ExecStart; sed and systemd only handle plain paths.
if [[ ! "$PWD" =~ ^[A-Za-z0-9_./-]+$ ]]; then
  echo "unsupported install path: $PWD" >&2
  exit 1
fi

# The directory belongs to the service user (tv after tools/setup-remote.sh); build as that user so
# the remote's uploads can rebuild later. Before the setup it is the deploying user, as it always was.
owner=$(stat -c %U .)
if [[ $owner != "$(id -un)" ]]; then
  sudo -u "$owner" make
else
  make
fi

unit=/etc/systemd/system/bc101.service
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
sed "s|@BC101_DIR@|$PWD|g; s|@BC101_USER@|$owner|g" system/bc101.service > "$tmp"
if ! cmp -s "$tmp" "$unit"; then
  # shellcheck disable=SC2024 # reading as the user is intended, only tee writes as root
  sudo tee "$unit" >/dev/null < "$tmp"
  sudo systemctl daemon-reload
  echo "unit updated"
fi
sudo systemctl enable --quiet bc101
sudo systemctl restart bc101

pid=$(systemctl show -p MainPID --value bc101)
sleep 3
if [[ "$(systemctl is-active bc101)" != active || "$(systemctl show -p MainPID --value bc101)" != "$pid" ]]; then
  echo "bc101 did not stay up (journalctl -u bc101)" >&2
  exit 1
fi
echo "bc101 running, pid $pid, user $owner"
