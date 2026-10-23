#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# One-time setup of the remote on the Pi; run as root, safe to run again. Creates the service user
# zapper (TVTV_USER to change it), moves the show to /opt/tvtv (owned by it), installs the
# dispatcher, the keys file, the sshd drop-in and the systemd unit, gives the deploying user exact
# sudo rules for later deploys, then builds and (re)starts the service through tools/install.sh.
# Called by `scripts/deploy.sh --setup`; by hand: sudo tools/setup-remote.sh [--deployer <user>]
# (from a synced copy that does not belong to the service user). The deployer defaults to the
# sudo caller.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=$PWD
DEST=${TVTV_HOME:-/opt/tvtv}
U=${TVTV_USER:-zapper} # the service user: runs the show and takes the ssh commands
LIB=/usr/local/lib/tvtv
deployer=${SUDO_USER:-}
if [[ ${1:-} == --deployer && -n ${2:-} ]]; then deployer=$2; fi

[[ $EUID -eq 0 ]] || { echo "run as root (sudo)" >&2; exit 1; }
for t in useradd sshd rsync make gcc logger visudo; do
  command -v "$t" > /dev/null || { echo "missing: $t" >&2; exit 1; }
done
# ExposeAuthInfo (OpenSSH 7.6+) is how the dispatcher learns which key logged in. A soft check
# only: `sshd -t` on the drop-in below is the one that decides.
if out=$(sshd -T -C user=root,host=localhost,addr=127.0.0.1 2>&1); then
  grep -qi '^exposeauthinfo' <<< "$out" || echo "warning: sshd -T shows no ExposeAuthInfo ($(ssh -V 2>&1))" >&2
else
  echo "warning: sshd -T failed, going on: ${out%%$'\n'*}" >&2
fi
if [[ $(stat -c %U "$SRC") == "$U" ]]; then
  echo "run the setup from a copy that does not belong to $U (scripts/deploy.sh --setup)" >&2
  exit 1
fi

# The account must be ours: a person's login of that name would be locked into the dispatcher.
if uid=$(id -u "$U" 2> /dev/null); then
  if ((uid >= 1000)) || [[ $(getent passwd "$U" | cut -d: -f6) == /home/* ]]; then
    echo "user $U exists and looks like a person's login; pick another name: TVTV_USER=<name>" >&2
    exit 1
  fi
else
  useradd --system --user-group --home-dir "$DEST" --no-create-home --shell /bin/sh "$U"
  echo "user $U created"
fi
usermod -aG video,tty "$U"
if [[ -n $deployer && $deployer == "$U" ]]; then
  echo "the deploying user cannot be the service user" >&2
  exit 1
fi

install -d -m 0755 -o "$U" -g "$U" "$DEST" "$DEST/visuals"
if [[ $SRC != "$DEST" ]]; then
  rsync -a --delete --chown="$U:$U" src slides system tools Makefile "$DEST/"
  echo "show copied to $DEST"
fi

install -d -m 0755 /etc/tvtv
[[ -f /etc/tvtv/authorized_keys ]] || install -m 0644 /dev/null /etc/tvtv/authorized_keys
install -D -m 0755 tools/tv-dispatch "$LIB/tv-dispatch"
install -m 0755 tools/tv-keys.sh /usr/local/sbin/tv-keys

conf=/etc/ssh/sshd_config.d/50-tvtv.conf
drop=$(mktemp)
sed "s|^Match User zapper$|Match User $U|" system/sshd-tv.conf > "$drop"
if ! cmp -s "$drop" "$conf"; then
  install -D -m 0644 "$drop" "$conf"
  if ! sshd -t; then
    rm -f "$conf"
    echo "sshd rejects the drop-in; removed it again" >&2
    exit 1
  fi
  systemctl reload ssh 2> /dev/null || systemctl reload sshd
  echo "sshd: user $U restricted to the dispatcher"
fi
rm -f "$drop"

# The unit, from this copy (not from the tree the service user owns).
sed "s|@BC101_DIR@|$DEST|g; s|@BC101_USER@|$U|g" system/bc101.service > /etc/systemd/system/bc101.service

# Exact sudo rules for the deploying user: build and sync as the service user, the unit file
# from stdin, the service, the keys, the USB stick /dev/sda1 on /mnt by hand (it also mounts by
# itself, see fstab below). Nothing that runs a file the service user can write.
if [[ -n $deployer && $deployer != root ]] && getent passwd "$deployer" > /dev/null; then
  rules=$(mktemp)
  cat > "$rules" <<RULES
# tvtv: scripts/deploy.sh and tv-keys for $deployer, written by tools/setup-remote.sh
Cmnd_Alias TVTV_AS_TV = $(command -v rsync), $(command -v make)
Cmnd_Alias TVTV_ROOT = $(command -v tee) /etc/systemd/system/bc101.service, \\
    $(command -v systemctl) daemon-reload, $(command -v systemctl) enable --quiet bc101, \\
    $(command -v systemctl) restart bc101, $(command -v systemctl) stop bc101, \\
    $(command -v systemctl) start bc101, /usr/local/sbin/tv-keys, \\
    $(command -v mount) -o uid=$(id -u "$deployer")\\,gid=$(id -g "$deployer")\\,nosuid\\,nodev\\,noexec /dev/sda1 /mnt, \\
    $(command -v umount) /mnt
$deployer ALL=($U) NOPASSWD: TVTV_AS_TV
$deployer ALL=(root) NOPASSWD: TVTV_ROOT
RULES
  if visudo -c -q -f "$rules"; then
    install -m 0440 "$rules" /etc/sudoers.d/tvtv-deploy
    echo "sudo: $deployer may deploy and manage keys without a password"
  else
    echo "sudoers rules did not validate, not installed" >&2
  fi
  rm -f "$rules"
else
  echo "no deploying user given (--deployer <user>): deploys will ask for sudo passwords" >&2
fi

# The USB stick (WADs for Doom): mounted when something looks at /mnt, unmounted after a minute
# idle so it can be pulled, and a missing stick does not hold up the boot.
owner=${deployer:-$U}
[[ $owner != root ]] || owner=$U
line="/dev/sda1 /mnt vfat uid=$(id -u "$owner"),gid=$(id -g "$owner"),nosuid,nodev,noexec,nofail,noauto"
line+=",x-systemd.automount,x-systemd.idle-timeout=60,x-systemd.device-timeout=5s 0 0"
if ! grep -qxF "$line" /etc/fstab; then
  sed -i '/^# tvtv: the USB stick/d; \|^/dev/sda1[[:space:]]|d' /etc/fstab
  printf '# tvtv: the USB stick, see tools/setup-remote.sh\n%s\n' "$line" >> /etc/fstab
  systemctl daemon-reload
  if [[ $(findmnt -n -o SOURCE /mnt 2> /dev/null) == /dev/sda1 ]]; then umount /mnt; fi
  systemctl restart mnt.automount
  echo "fstab: the USB stick mounts on /mnt when used"
fi

"$DEST/tools/install.sh"

cat <<TXT

remote ready. Next:
  key:    ssh <pi> sudo tv-keys add <who> admin < <who>.pub      (or op: zap only)
  ssh:    Host zapper / HostName <this pi> / User $U / IdentityFile ~/.ssh/<key>   in ~/.ssh/config
          or, without an alias: scripts/tv config host <this pi's ip>; scripts/tv config user $U;
                                scripts/tv config identity ~/.ssh/<key>
  use:    ssh zapper      the zapper        ssh zapper 7 | next | status | help
          (scripts/tv, scripts/tv 7 ... with or without the alias)
  log:    journalctl -t tvtv
TXT
