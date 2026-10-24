#!/usr/bin/env bats
# tools/tvtv-pi.sh against a made-up Pi under TVTV_ROOT, with stand-ins for systemctl, apt-get and
# journalctl that only write down what they were asked. Runs anywhere: bats tests/pi.bats
setup() {
  cd "$BATS_TEST_DIRNAME/.." || exit 1
  T=$BATS_TEST_TMPDIR
  PI_LOGIN=deploy # the deploying login on the made-up Pi
  export TVTV_ROOT=$T/pi
  mkdir -p "$T/bin" "$TVTV_ROOT"/{var/lib/tvtv/components,usr/local/lib/tvtv,usr/local/sbin,etc/tvtv,etc/systemd/system,etc/sudoers.d,opt/tvtv/build,srv/tvtv,home/$PI_LOGIN}
  for t in systemctl apt-get journalctl findmnt; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/calls"\n' "$t" "$T" > "$T/bin/$t"
    chmod +x "$T/bin/$t"
  done
  # findmnt: the stick is never mounted here
  printf '#!/bin/sh\nexit 1\n' > "$T/bin/findmnt"
  export PATH="$T/bin:$PATH"
  # the base, as tools/setup-remote.sh writes it
  echo v1.2-3-gabc > "$TVTV_ROOT/opt/tvtv/VERSION"
  for f in usr/local/lib/tvtv/tv-dispatch usr/local/sbin/tv-keys usr/local/sbin/tvtv-pi \
    etc/tvtv/authorized_keys etc/systemd/system/tvtv-show.socket etc/systemd/system/tvtv-show.service; do
    : > "$TVTV_ROOT/$f"
  done
  cat > "$TVTV_ROOT/var/lib/tvtv/components/show" << 'LIST'
# the base
name show
repo https://github.com/bmmmm/tvtv
version-file /opt/tvtv/VERSION
unit tvtv-show.socket
unit tvtv-show.service
file /usr/local/lib/tvtv/tv-dispatch
file /usr/local/sbin/tv-keys
file /usr/local/sbin/tvtv-pi
keep /etc/tvtv/authorized_keys
keep /opt/tvtv
keep /srv/tvtv
LIST
}

social() { # a guest component, as dem02socialTV's install.sh writes it
  mkdir -p "$TVTV_ROOT/var/lib/tvtv-social"
  for f in usr/local/lib/tvtv/tvtv-social etc/tvtv/social.json etc/tvtv/social.token \
    etc/systemd/system/tvtv-social.socket etc/systemd/system/tvtv-social.service; do
    : > "$TVTV_ROOT/$f"
  done
  cat > "$TVTV_ROOT/var/lib/tvtv/components/social" << 'LIST'
name social
repo https://github.com/bmmmm/dem02socialTV
version 0123456789ab
needs show
unit tvtv-social.socket
unit tvtv-social.service
file /usr/local/lib/tvtv/tvtv-social
keep /etc/tvtv/social.json
keep /etc/tvtv/social.token
keep /var/lib/tvtv-social
LIST
}

@test "status: the base first, its version from the file, units and files" {
  social
  run tools/tvtv-pi.sh status
  [ "$status" -eq 0 ]
  [[ ${lines[0]} == "show "*"v1.2-3-gabc"*"github.com/bmmmm/tvtv" ]]
  [[ $output == *"tvtv-show.socket"* ]]
  [[ $output == *"files 3/3"* ]]
  [[ $output == *"social "*"0123456789ab"* ]]
}

@test "status: a missing file is named" {
  rm "$TVTV_ROOT/usr/local/sbin/tv-keys"
  run tools/tvtv-pi.sh status
  [[ $output == *"files 2/3, missing: /usr/local/sbin/tv-keys"* ]]
}

@test "check: a sound layout passes" {
  social
  run tools/tvtv-pi.sh check
  echo "$output"
  [ "$status" -eq 0 ]
  [[ $output == "ok: 2 component(s)"* ]]
}

@test "check: unclaimed files, a unit drop-in is the unit's own" {
  : > "$TVTV_ROOT/usr/local/lib/tvtv/tvtv-social" # installed before it wrote a list
  mkdir -p "$TVTV_ROOT/etc/systemd/system/tvtv-show.service.d"
  : > "$TVTV_ROOT/etc/systemd/system/tvtv-show.service.d/override.conf"
  run tools/tvtv-pi.sh check
  [ "$status" -eq 1 ]
  [[ $output == *"unclaimed: /usr/local/lib/tvtv/tvtv-social"* ]]
  [[ $output != *"tvtv-show.service.d"* ]]
}

@test "check: what was there before docs/pi.md is named" {
  : > "$TVTV_ROOT/etc/systemd/system/bc101.service"
  : > "$TVTV_ROOT/etc/sudoers.d/tvtv-deploy"
  mkdir -p "$TVTV_ROOT/home/$PI_LOGIN/tvtv-social-check"
  printf '/dev/sda1 /mnt vfat noauto 0 0\n' > "$TVTV_ROOT/etc/fstab"
  run tools/tvtv-pi.sh check
  [ "$status" -eq 1 ]
  [[ $output == *"before docs/pi.md: /etc/systemd/system/bc101.service"* ]]
  [[ $output == *"/etc/sudoers.d/tvtv-deploy"* ]]
  [[ $output == *"left over: /home/$PI_LOGIN/tvtv-social-check"* ]]
  [[ $output == *"old /mnt line"* ]]
}

@test "check: a list naming something outside the layout, a missing dependency" {
  social
  printf 'file /etc/passwd\n' >> "$TVTV_ROOT/var/lib/tvtv/components/social"
  rm "$TVTV_ROOT/var/lib/tvtv/components/show"
  run tools/tvtv-pi.sh check
  [ "$status" -eq 1 ]
  [[ $output == *"social: file outside the layout of docs/pi.md: /etc/passwd"* ]]
  [[ $output == *"social needs show, which is not installed"* ]]
  [[ $output == *"no list for the base"* ]]
}

@test "check: paths that climb out or units of another name are refused" {
  printf 'file /usr/local/lib/tvtv/../../../etc/shadow\nunit getty@tty1.service\n' >> "$TVTV_ROOT/var/lib/tvtv/components/show"
  run tools/tvtv-pi.sh check
  [ "$status" -eq 1 ]
  [[ $output == *"outside the layout"*"etc/shadow"* ]]
  [[ $output == *"unit not named tvtv-*: getty@tty1.service"* ]]
}

@test "remove: units off, files gone, settings and state kept" {
  social
  run tools/tvtv-pi.sh remove social
  [ "$status" -eq 0 ]
  [ ! -e "$TVTV_ROOT/usr/local/lib/tvtv/tvtv-social" ]
  [ ! -e "$TVTV_ROOT/etc/systemd/system/tvtv-social.service" ]
  grep -qx "removed $(date +%F)" "$TVTV_ROOT/var/lib/tvtv/components/social"
  [ -e "$TVTV_ROOT/etc/tvtv/social.token" ]
  [ -d "$TVTV_ROOT/var/lib/tvtv-social" ]
  grep -qx 'systemctl disable --now --quiet tvtv-social.socket' "$T/calls"
  grep -qx 'systemctl daemon-reload' "$T/calls"
  # what stays is still named: check is content, status says so, --purge finds it later
  run tools/tvtv-pi.sh check
  [ "$status" -eq 0 ]
  [[ $output == "ok: 1 component(s)"* ]]
  run tools/tvtv-pi.sh status
  [[ $output == *"social   removed "*"kept: /etc/tvtv/social.json"* ]]
  run tools/tvtv-pi.sh remove social
  [ "$status" -eq 1 ]
  run tools/tvtv-pi.sh remove social --purge
  [ "$status" -eq 0 ]
  [ ! -e "$TVTV_ROOT/etc/tvtv/social.token" ]
  [ ! -e "$TVTV_ROOT/var/lib/tvtv/components/social" ]
}

@test "remove --purge: settings, state and drop-ins go too" {
  social
  mkdir -p "$TVTV_ROOT/etc/systemd/system/tvtv-social.service.d"
  run tools/tvtv-pi.sh remove social --purge
  [ "$status" -eq 0 ]
  [ ! -e "$TVTV_ROOT/etc/tvtv/social.token" ]
  [ ! -e "$TVTV_ROOT/var/lib/tvtv-social" ]
  [ ! -e "$TVTV_ROOT/etc/systemd/system/tvtv-social.service.d" ]
  [ -e "$TVTV_ROOT/etc/tvtv/authorized_keys" ]
}

@test "remove: not the base, not what another needs, not an unsound list" {
  social
  run tools/tvtv-pi.sh remove show
  [ "$status" -eq 1 ]
  [[ $output == *"the base"* ]]
  printf 'name extra\nneeds social\n' > "$TVTV_ROOT/var/lib/tvtv/components/extra"
  run tools/tvtv-pi.sh remove social
  [ "$status" -eq 1 ]
  [[ $output == *"extra needs social"* ]]
  rm "$TVTV_ROOT/var/lib/tvtv/components/extra"
  printf 'file /etc/passwd\n' >> "$TVTV_ROOT/var/lib/tvtv/components/social"
  run tools/tvtv-pi.sh remove social
  [ "$status" -eq 1 ]
  [[ $output == *"nothing removed"* ]]
  [ -e "$TVTV_ROOT/usr/local/lib/tvtv/tvtv-social" ]
  [ ! -e "$T/calls" ] || ! grep -q disable "$T/calls"
}

@test "clean: apt's cache, old copies; the old social copy stays until its new one exists" {
  mkdir -p "$TVTV_ROOT/var/cache/apt/archives" "$TVTV_ROOT/var/log/journal" \
    "$TVTV_ROOT/home/$PI_LOGIN/tvtv-setup" "$TVTV_ROOT/home/$PI_LOGIN/tvtv-social-setup"
  : > "$TVTV_ROOT/var/cache/apt/archives/gcc.deb"
  run tools/tvtv-pi.sh clean -n
  [ "$status" -eq 0 ]
  [[ $output == *"would: apt-get clean"* ]]
  [[ $output == *"kept: /home/$PI_LOGIN/tvtv-social-setup"* ]]
  [ -d "$TVTV_ROOT/home/$PI_LOGIN/tvtv-setup" ]
  run tools/tvtv-pi.sh clean
  [ "$status" -eq 0 ]
  grep -qx 'apt-get clean' "$T/calls"
  grep -qx 'journalctl --vacuum-size=16M' "$T/calls"
  [ ! -e "$TVTV_ROOT/home/$PI_LOGIN/tvtv-setup" ]
  [ -d "$TVTV_ROOT/home/$PI_LOGIN/tvtv-social-setup" ]
  mkdir -p "$TVTV_ROOT/home/$PI_LOGIN/tvtv-deploy/social"
  run tools/tvtv-pi.sh clean
  [ ! -e "$TVTV_ROOT/home/$PI_LOGIN/tvtv-social-setup" ]
}

@test "du: the card, each component, the show's parts, staging" {
  social
  head -c 300000 /dev/zero > "$TVTV_ROOT/opt/tvtv/build/x.o"
  mkdir -p "$TVTV_ROOT/home/$PI_LOGIN/tvtv-deploy/social"
  run tools/tvtv-pi.sh du
  [ "$status" -eq 0 ]
  [[ ${lines[0]} == "SD card /"*"used of"*"free"* ]]
  [[ $output == *"stick          not mounted"* ]]
  [[ $output == *"  show "* ]]
  [[ $output == *"build/"*"/opt/tvtv/build"* ]]
  [[ $output == *"staging"*"/home/$PI_LOGIN/tvtv-deploy"* ]]
}

@test "no command: the usage" {
  run tools/tvtv-pi.sh
  [ "$status" -eq 2 ]
  [[ $output == *"tvtv-pi status"* ]]
}
