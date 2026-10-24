# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Local settings for scripts/deploy.sh and scripts/tv: KEY=VALUE lines in .env at the top of the
# repo, not in git (.env.example lists the keys). Sourced, not run.
#   load_env <key>...   exports those keys from .env; a variable already set in the environment
#                       wins, so BC101_HOST=other scripts/deploy.sh still works
# Values: the rest of the line, without a trailing " # comment" and without surrounding quotes;
# nothing in the file is run. TVTV_ENV=<file> reads another file (a second Pi, tests).
load_env() {
  local file line key val k
  file=${TVTV_ENV:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.env}
  [[ -f $file ]] || return 0
  while IFS= read -r line || [[ -n $line ]]; do
    line=${line%$'\r'}
    [[ $line =~ ^[[:space:]]*([A-Z0-9_]+)[[:space:]]*=[[:space:]]*(.*)$ ]] || continue
    key=${BASH_REMATCH[1]}
    val=${BASH_REMATCH[2]}
    val=${val%%[[:space:]]#*}
    val=${val%"${val##*[![:space:]]}"}
    if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then val=${BASH_REMATCH[1]}; fi
    for k in "$@"; do
      if [[ $k == "$key" && -z ${!key+x} ]]; then export "$key=$val"; fi
    done
  done < "$file"
}
