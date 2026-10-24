#!/usr/bin/env bats
# scripts/env.sh: local settings from a KEY=VALUE file, the environment wins. Runs anywhere:
#   bats tests/env.bats
setup() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  export TVTV_ENV=$BATS_TEST_TMPDIR/settings
  unset TVTV_DEPLOY_HOST TVTV_SETUP_HOST TVTV_HOME TVTV_USER BC101_HOST BC101_SETUP_HOST BC101_DIR \
    BC101_USER TV_HOST OTHER
  printf '%s\n' \
    'TVTV_DEPLOY_HOST=pi-alias   # the alias' \
    'TVTV_SETUP_HOST="you@192.0.2.10"' \
    '  TVTV_HOME = /srv/x' \
    'OTHER=x' \
    "TV_HOST='q h'" \
    '# TVTV_USER=nope' > "$TVTV_ENV"
  printf 'TV_USER=crlf\r\n' >> "$TVTV_ENV"
}

@test "values: comments, quotes, blanks and CR go, only the named keys come in" {
  source scripts/env.sh
  load_env TVTV_DEPLOY_HOST TVTV_SETUP_HOST TVTV_HOME TVTV_USER TV_HOST TV_USER
  [ "$TVTV_DEPLOY_HOST" = pi-alias ]
  [ "$TVTV_SETUP_HOST" = you@192.0.2.10 ]
  [ "$TVTV_HOME" = /srv/x ]
  [ "$TV_HOST" = "q h" ]
  [ "$TV_USER" = crlf ]
  [ -z "${TVTV_USER+x}" ]
  [ -z "${OTHER+x}" ]
}

@test "the environment wins over the file" {
  export TVTV_DEPLOY_HOST=from-env
  source scripts/env.sh
  load_env TVTV_DEPLOY_HOST
  [ "$TVTV_DEPLOY_HOST" = from-env ]
}

@test "nothing in the file runs" {
  printf 'TVTV_DEPLOY_HOST=$(touch %s/ran)\n' "$BATS_TEST_TMPDIR" > "$TVTV_ENV"
  source scripts/env.sh
  load_env TVTV_DEPLOY_HOST
  [ ! -e "$BATS_TEST_TMPDIR/ran" ]
  [ "$TVTV_DEPLOY_HOST" = "\$(touch $BATS_TEST_TMPDIR/ran)" ]
}

@test "no file: nothing set, no error" {
  export TVTV_ENV=$BATS_TEST_TMPDIR/missing
  source scripts/env.sh
  load_env TVTV_DEPLOY_HOST
  [ -z "${TVTV_DEPLOY_HOST+x}" ]
}

@test "deploy.sh without a host says where to set it" {
  export TVTV_ENV=$BATS_TEST_TMPDIR/missing
  run scripts/deploy.sh -n
  [ "$status" -eq 2 ]
  [[ $output == *"TVTV_DEPLOY_HOST"*".env.example"* ]]
}

@test "an old name still counts, with a note; the new one wins when both are there" {
  printf 'BC101_HOST=old-alias\nBC101_DIR=/opt/x\n' > "$TVTV_ENV"
  source scripts/env.sh
  load_env TVTV_DEPLOY_HOST TVTV_HOME 2> "$BATS_TEST_TMPDIR/err"
  [ "$TVTV_DEPLOY_HOST" = old-alias ]
  [ "$TVTV_HOME" = /opt/x ]
  grep -qF 'note: BC101_HOST is now TVTV_DEPLOY_HOST' "$BATS_TEST_TMPDIR/err"
  unset TVTV_DEPLOY_HOST TVTV_HOME BC101_HOST BC101_DIR
  printf 'BC101_HOST=old-alias\nTVTV_DEPLOY_HOST=new-alias\n' > "$TVTV_ENV"
  load_env TVTV_DEPLOY_HOST 2> "$BATS_TEST_TMPDIR/err"
  [ "$TVTV_DEPLOY_HOST" = new-alias ]
  [ ! -s "$BATS_TEST_TMPDIR/err" ]
}

@test "an old name asked for by its own name is read as before" {
  printf 'BC101_DEM0_BUDGET=5\n' > "$TVTV_ENV"
  source scripts/env.sh
  load_env BC101_DEM0_BUDGET
  [ "$BC101_DEM0_BUDGET" = 5 ]
}
