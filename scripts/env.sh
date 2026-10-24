# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Local settings for scripts/deploy.sh and scripts/tv: KEY=VALUE lines in .env at the top of the
# repo, not in git (.env.example lists the keys). Sourced, not run.
#   load_env <key>...   exports those keys from .env; a variable already set in the environment
#                       wins, so TVTV_DEPLOY_HOST=other scripts/deploy.sh still works
# Values: the rest of the line, without a trailing " # comment" and without surrounding quotes;
# nothing in the file is run. TVTV_ENV=<file> reads another file (a second Pi, tests).
# Keys renamed in docs/pi.md ("Names") still count under their old name, with a note, until every
# .env is rewritten: new=old. dem02socialTV's deploy/env.sh is this file; keep the two the same.
LEGACY_ENV=(TVTV_DEPLOY_HOST=BC101_HOST TVTV_SETUP_HOST=BC101_SETUP_HOST TVTV_HOME=BC101_DIR
  TVTV_USER=BC101_USER)
load_env() {
  local file line key val k pair new old want=("$@") from_env=()
  for pair in "${LEGACY_ENV[@]}"; do
    for k in "$@"; do [[ $k != "${pair%%=*}" ]] || want+=("${pair#*=}"); done
    # an old name set in the environment, the new one not: the environment still wins over .env
    new=${pair%%=*} old=${pair#*=}
    [[ -n ${!old+x} && -z ${!new+x} ]] && from_env+=("$new")
  done
  file=${TVTV_ENV:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.env}
  if [[ -f $file ]]; then
    while IFS= read -r line || [[ -n $line ]]; do
      line=${line%$'\r'}
      [[ $line =~ ^[[:space:]]*([A-Z0-9_]+)[[:space:]]*=[[:space:]]*(.*)$ ]] || continue
      key=${BASH_REMATCH[1]}
      val=${BASH_REMATCH[2]}
      val=${val%%[[:space:]]#*}
      val=${val%"${val##*[![:space:]]}"}
      if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then val=${BASH_REMATCH[1]}; fi
      for k in "${want[@]}"; do
        if [[ $k == "$key" && -z ${!key+x} ]]; then export "$key=$val"; fi
      done
    done < "$file"
  fi
  for pair in "${LEGACY_ENV[@]}"; do
    new=${pair%%=*} old=${pair#*=}
    for k in "$@"; do
      [[ $k == "$new" && -n ${!old+x} ]] || continue
      if [[ " ${from_env[*]} " == *" $new "* ]]; then
        export "$new=${!old}"
        echo "note: $old is now $new (docs/pi.md, Names)" >&2
      elif [[ -z ${!new+x} ]]; then
        export "$new=${!old}"
        echo "note: $old is now $new (docs/pi.md, Names): rename it in .env" >&2
      fi
    done
  done
}
