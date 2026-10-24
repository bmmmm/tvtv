#!/usr/bin/env bats
# scripts/env.sh: local settings from a KEY=VALUE file, the environment wins. Runs anywhere:
#   bats tests/env.bats
setup() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  export TVTV_ENV=$BATS_TEST_TMPDIR/settings
  unset BC101_HOST BC101_SETUP_HOST BC101_DIR BC101_USER TV_HOST OTHER
  printf '%s\n' \
    'BC101_HOST=pi-alias   # the alias' \
    'BC101_SETUP_HOST="you@192.0.2.10"' \
    '  BC101_DIR = /srv/x' \
    'OTHER=x' \
    "TV_HOST='q h'" \
    '# BC101_USER=nope' > "$TVTV_ENV"
  printf 'TV_USER=crlf\r\n' >> "$TVTV_ENV"
}

@test "values: comments, quotes, blanks and CR go, only the named keys come in" {
  source scripts/env.sh
  load_env BC101_HOST BC101_SETUP_HOST BC101_DIR BC101_USER TV_HOST TV_USER
  [ "$BC101_HOST" = pi-alias ]
  [ "$BC101_SETUP_HOST" = you@192.0.2.10 ]
  [ "$BC101_DIR" = /srv/x ]
  [ "$TV_HOST" = "q h" ]
  [ "$TV_USER" = crlf ]
  [ -z "${BC101_USER+x}" ]
  [ -z "${OTHER+x}" ]
}

@test "the environment wins over the file" {
  export BC101_HOST=from-env
  source scripts/env.sh
  load_env BC101_HOST
  [ "$BC101_HOST" = from-env ]
}

@test "nothing in the file runs" {
  printf 'BC101_HOST=$(touch %s/ran)\n' "$BATS_TEST_TMPDIR" > "$TVTV_ENV"
  source scripts/env.sh
  load_env BC101_HOST
  [ ! -e "$BATS_TEST_TMPDIR/ran" ]
  [ "$BC101_HOST" = "\$(touch $BATS_TEST_TMPDIR/ran)" ]
}

@test "no file: nothing set, no error" {
  export TVTV_ENV=$BATS_TEST_TMPDIR/missing
  source scripts/env.sh
  load_env BC101_HOST
  [ -z "${BC101_HOST+x}" ]
}

@test "deploy.sh without a host says where to set it" {
  export TVTV_ENV=$BATS_TEST_TMPDIR/missing
  run scripts/deploy.sh -n
  [ "$status" -eq 2 ]
  [[ $output == *"BC101_HOST"*".env.example"* ]]
}
