#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Prove the social channels on the Pi before they go on air: copy what tools/social-pi-check.sh
# needs to ~/tvtv-social-check there and run it, as that login and without sudo. It builds in a
# temporary directory and only reads the show and /opt/tvtv (docs/remote.md, "Social channels").
# Usage: scripts/social-check.sh [-n] [--quick] [--target <ms>] [--budget <units>]
#   -n       dry run: what rsync would copy
# The other options are the check's; its exit code is this one's.
# Where to: BC101_HOST from the environment or from .env at the top of the repo, as for
# scripts/deploy.sh (see .env.example); the deploying login will do, as will any other on the Pi.
set -euo pipefail

# shellcheck source=scripts/env.sh
source "$(dirname "$0")/env.sh"
load_env BC101_HOST
if [[ -z ${BC101_HOST:-} ]]; then
  echo "social-check: no BC101_HOST: set a login on the Pi in .env (see .env.example)" >&2
  exit 2
fi
HOST=$BC101_HOST
DIR=tvtv-social-check # in that login's home, the check's own copy
# What the check builds and runs. src/ whole, as the Makefile takes every file in it, so --delete
# there: a file gone from the tree must not be built on the Pi.
PARTS=(Makefile src tools/social-pi-check.sh tools/preview.c tools/gen-visuals.sh tests/dem0-check.c
  tests/social-check.c tests/mksnap.py tests/dem0-vectors.txt)
cd "$(dirname "$0")/.."

if [[ ${1:-} == -n ]]; then
  rsync -aniR --delete "${PARTS[@]}" "$HOST:$DIR/"
  exit 0
fi
rsync -aR --delete "${PARTS[@]}" "$HOST:$DIR/"
args=""
(($#)) && args=$(printf ' %q' "$@")
# shellcheck disable=SC2029 # $DIR and the options are meant to expand here
exec ssh "$HOST" "bash $DIR/tools/social-pi-check.sh$args" < /dev/null
