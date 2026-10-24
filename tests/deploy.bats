#!/usr/bin/env bats
# scripts/deploy.sh against stand-ins for ssh and rsync that write down what they were asked and
# play a Pi: its login is deploy, its sudo rules know tvtv-show unless SUDO_RC says otherwise.
# Runs anywhere: bats tests/deploy.bats
setup() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  T=$BATS_TEST_TMPDIR
  PI_LOGIN=deploy # the deploying login on the made-up Pi
  export TVTV_ENV=$T/no-settings TVTV_DEPLOY_HOST=pi-test
  unset TVTV_SETUP_HOST TVTV_HOME TVTV_USER BC101_HOST BC101_SETUP_HOST BC101_DIR BC101_USER
  mkdir -p "$T/bin" "$T/units"
  cat > "$T/bin/ssh" << SH
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$T/ssh.calls"
shift
case \$* in
  *'"\$USER"'*) printf deploy ;;
  *'"\$HOME"'*) printf /home/$PI_LOGIN ;;
  'sudo -n -l '*) exit \${SUDO_RC:-0} ;;
  'sudo tee '*) f=\${*#sudo tee }; f=\${f%% *}; cat > "$T/units/\${f##*/}" ;;
  'bash -s -- '*) cat > "$T/install.piped"; printf '%s\n' "\$4" > "$T/install.arg" ;;
esac
SH
  printf '#!/bin/sh\nprintf "%%s\\n" "$*" >> "%s/rsync.calls"\n' "$T" > "$T/bin/rsync"
  chmod +x "$T/bin/ssh" "$T/bin/rsync"
  export PATH="$T/bin:$PATH"
}

@test "--setup: the copy goes to ~/tvtv-deploy/show-setup, with VERSION, and the command for it" {
  run scripts/deploy.sh --setup
  [ "$status" -eq 0 ]
  [[ $(cat "$T/rsync.calls") == "-a --delete --rsync-path=mkdir -p tvtv-deploy/show-setup && rsync src slides system tools third_party Makefile "*"/VERSION pi-test:tvtv-deploy/show-setup/" ]]
  [[ $output == *"sudo /home/$PI_LOGIN/tvtv-deploy/show-setup/tools/setup-remote.sh --deployer deploy && sudo rm -rf /home/$PI_LOGIN/tvtv-deploy/show-setup"* ]]
}

@test "deploy: a Pi whose sudo rules predate tvtv-show is told to run --setup, nothing changes" {
  SUDO_RC=1 run scripts/deploy.sh
  [ "$status" -eq 1 ]
  [[ $output == *"predates it, run scripts/deploy.sh --setup"* ]]
  [ ! -e "$T/rsync.calls" ]
  [ -z "$(ls "$T/units")" ]
}

@test "deploy: tree as zapper, both units rendered, our own install.sh piped with the tree" {
  run scripts/deploy.sh
  [ "$status" -eq 0 ]
  [[ $(cat "$T/rsync.calls") == "-a --delete --rsync-path=sudo -u zapper rsync "*"/VERSION pi-test:/opt/tvtv/" ]]
  grep -qx 'ExecStart=/opt/tvtv/bc101' "$T/units/tvtv-show.service"
  grep -qx 'User=zapper' "$T/units/tvtv-show.service"
  grep -qx 'ListenStream=/run/tvtv-show.sock' "$T/units/tvtv-show.socket"
  grep -qx 'SocketUser=zapper' "$T/units/tvtv-show.socket"
  ! grep -q '@TVTV_' "$T/units/tvtv-show.service" "$T/units/tvtv-show.socket"
  cmp tools/install.sh "$T/install.piped"
  [ "$(cat "$T/install.arg")" = /opt/tvtv ]
  # the socket's unit file before the service's, which requires it
  [[ $(grep -n 'sudo tee' "$T/ssh.calls") == *"tvtv-show.socket"*"tvtv-show.service"* ]]
}

@test "deploy: the old .env names still lead to the same Pi" {
  unset TVTV_DEPLOY_HOST
  printf 'BC101_HOST=old-pi\nBC101_DIR=/opt/other\nBC101_USER=tv2\n' > "$T/settings"
  TVTV_ENV=$T/settings run scripts/deploy.sh
  [ "$status" -eq 0 ]
  [[ $output == *"note: BC101_HOST is now TVTV_DEPLOY_HOST"* ]]
  [[ $(cat "$T/rsync.calls") == "-a --delete --rsync-path=sudo -u tv2 rsync "*" old-pi:/opt/other/" ]]
  grep -qx 'User=tv2' "$T/units/tvtv-show.service"
}
