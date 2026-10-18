#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# One-time setup of the remote on the Pi; run as root, safe to run again. Creates the service user
# tv, moves the show to /opt/tvtv (owned by tv), installs the dispatcher, the keys file and the sshd
# drop-in, then builds and (re)starts the service through tools/install.sh.
# Called by `scripts/deploy.sh --setup`; by hand: sudo tools/setup-remote.sh (from a synced copy).
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=$PWD
DEST=${TVTV_HOME:-/opt/tvtv}
LIB=/usr/local/lib/tvtv

[[ $EUID -eq 0 ]] || { echo "run as root (sudo)" >&2; exit 1; }
for t in useradd sshd rsync make gcc logger; do
  command -v "$t" > /dev/null || { echo "missing: $t" >&2; exit 1; }
done
if ! sshd -T 2> /dev/null | grep -qi '^exposeauthinfo'; then
  echo "this sshd lacks ExposeAuthInfo (OpenSSH >= 7.6 needed)" >&2
  exit 1
fi

if ! getent passwd tv > /dev/null; then
  useradd --system --user-group --home-dir "$DEST" --no-create-home --shell /bin/sh tv
  echo "user tv created"
fi
usermod -aG video,tty tv

install -d -m 0755 -o tv -g tv "$DEST" "$DEST/visuals"
if [[ $SRC != "$DEST" ]]; then
  rsync -a --delete --chown=tv:tv src slides system tools Makefile "$DEST/"
  echo "show copied to $DEST"
fi

install -d -m 0755 /etc/tvtv
[[ -f /etc/tvtv/authorized_keys ]] || install -m 0644 /dev/null /etc/tvtv/authorized_keys
install -D -m 0755 tools/tv-dispatch "$LIB/tv-dispatch"
install -m 0755 tools/tv-keys.sh /usr/local/sbin/tv-keys

conf=/etc/ssh/sshd_config.d/50-tvtv.conf
if ! cmp -s system/sshd-tv.conf "$conf"; then
  install -D -m 0644 system/sshd-tv.conf "$conf"
  if ! sshd -t; then
    rm -f "$conf"
    echo "sshd rejects the drop-in; removed it again" >&2
    exit 1
  fi
  systemctl reload ssh 2> /dev/null || systemctl reload sshd
  echo "sshd: user tv restricted to the dispatcher"
fi

"$DEST/tools/install.sh"

cat <<TXT

remote ready. Next:
  key:    sudo tv-keys add <who> admin < <who>.pub      (or op: zap only)
  ssh:    Host tv / HostName <this pi> / User tv / IdentityFile ~/.ssh/<key>   in ~/.ssh/config
  use:    ssh tv          the zapper        ssh tv 7 | next | status | help
  log:    journalctl -t tvtv
TXT
