#!/usr/bin/env bash
# Pull the live TV show code from the Pi into local/, commit and push the diff.
# Usage: ./sync-from-pi.sh [-n]   (-n = dry run, shows what would change)
set -euo pipefail

SRC="tvtv-deploy:/home/deploy/tvtv/"  # path-guard:allow (path on the Pi)
cd "$(dirname "$0")"

if [[ "${1:-}" == "-n" ]]; then
  rsync -ani --delete --exclude .git "$SRC" local/
  exit 0
fi

mkdir -p local
rsync -a --delete --exclude .git "$SRC" local/

if [[ -z "$(git status --porcelain -- local)" ]]; then
  echo "Already in sync, nothing to commit."
  exit 0
fi

git add -- local
git status --short -- local
git commit -q -m "chore: sync from rpi1" -- local
git push origin main
git log --oneline -1
