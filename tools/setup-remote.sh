#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Setup of the Pi as the base of every project on it (docs/pi.md); run as root, safe to run again.
# Creates the service user zapper (TVTV_USER to change it), moves the show to /opt/tvtv (owned by
# it), installs the dispatcher, the keys file, the sshd drop-in, the systemd units, tvtv-pi, the
# USB stick's mount and the limits that keep the SD card from filling up, gives the deploying user
# exact sudo rules for later deploys, records all of it in the component list
# /var/lib/tvtv/components/show, then builds and (re)starts the show through tools/install.sh.
# A Pi set up before docs/pi.md (bc101.service, the stick on /mnt) is moved over on the way.
# Called by `scripts/deploy.sh --setup`; by hand: sudo tools/setup-remote.sh [--deployer <user>]
# (from a synced copy that does not belong to the service user). The deployer defaults to the
# sudo caller.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=$PWD
DEST=${TVTV_HOME:-/opt/tvtv}
U=${TVTV_USER:-zapper} # the service user: runs the show and takes the ssh commands
LIB=/usr/local/lib/tvtv
SRV=/srv/tvtv                  # the USB stick, ext4 with the label TVTV
DB=/var/lib/tvtv/components    # one list per component of what it installed (docs/pi.md)
SUDOERS=/etc/sudoers.d/tvtv-show
deployer=${SUDO_USER:-}
if [[ ${1:-} == --deployer && -n ${2:-} ]]; then deployer=$2; fi

[[ $EUID -eq 0 ]] || { echo "run as root (sudo)" >&2; exit 1; }
for t in useradd sshd rsync make gcc logger visudo blkid; do
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
if [[ -n $deployer && $deployer != root ]] && ! getent passwd "$deployer" > /dev/null; then
  echo "warning: no user $deployer on this Pi; going on without a deploying user" >&2
  deployer=""
fi

install -d -m 0755 -o "$U" -g "$U" "$DEST" "$DEST/visuals"
if [[ $SRC != "$DEST" ]]; then
  # the same parts as scripts/deploy.sh; third_party (Doom) and VERSION only where the copy has them
  parts=(src slides system tools Makefile)
  for p in third_party VERSION; do [[ ! -e $p ]] || parts+=("$p"); done
  rsync -a --delete --chown="$U:$U" "${parts[@]}" "$DEST/"
  echo "show copied to $DEST"
fi

install -d -m 0755 /etc/tvtv "$LIB" /var/lib/tvtv "$DB"
[[ -f /etc/tvtv/authorized_keys ]] || install -m 0644 /dev/null /etc/tvtv/authorized_keys
install -m 0755 tools/tv-dispatch "$LIB/tv-dispatch"
install -m 0755 tools/tv-keys.sh /usr/local/sbin/tv-keys
install -m 0755 tools/tvtv-pi.sh /usr/local/sbin/tvtv-pi

conf=/etc/ssh/sshd_config.d/50-tvtv.conf
drop=$(mktemp)
sed "s|^Match User zapper$|Match User $U|" system/sshd-tv.conf > "$drop"
if ! cmp -s "$drop" "$conf"; then
  # The one in place stays until sshd takes the new one: without any, zapper would be a plain
  # login again after the next sshd restart.
  [[ ! -f $conf ]] || cp -p "$conf" "$conf.prev"
  install -D -m 0644 "$drop" "$conf"
  if ! sshd -t; then
    if [[ -f $conf.prev ]]; then mv -f "$conf.prev" "$conf"; else rm -f "$conf"; fi
    echo "sshd rejects the new drop-in; the previous one is back" >&2
    exit 1
  fi
  rm -f "$conf.prev"
  systemctl reload ssh 2> /dev/null || systemctl reload sshd
  echo "sshd: user $U restricted to the dispatcher"
fi
rm -f "$drop"

# ---------- before docs/pi.md: the show was bc101, the stick vfat on /mnt ----------
# Off with the old units before the new ones take tty1; the socket goes with them, and the
# deploying user's old sudo rules, which name them, too.
old=()
for unit in bc101.socket bc101.service; do
  [[ ! -e /etc/systemd/system/$unit ]] || old+=("$unit")
done
if ((${#old[@]})); then
  systemctl disable --now --quiet "${old[@]}" 2> /dev/null || true
  for unit in "${old[@]}"; do rm -f "/etc/systemd/system/$unit"; done
  # the site's settings for the show (systemctl edit bc101: BC101_DEM0_BUDGET) go along
  d=/etc/systemd/system
  if [[ -d $d/bc101.service.d && ! -e $d/tvtv-show.service.d ]]; then
    mv "$d/bc101.service.d" "$d/tvtv-show.service.d"
    echo "moved over: the drop-ins of bc101.service now belong to tvtv-show.service"
  fi
  rm -rf /run/bc101
  systemctl daemon-reload
  echo "moved over: ${old[*]} are gone, the show is tvtv-show now"
fi
# the stick's old line: only the one under the setup's own comment, never a line of a person's
if grep -A1 '^# tvtv: the USB stick' /etc/fstab | grep -qE '^/dev/sda1[[:space:]]+/mnt[[:space:]]'; then
  systemctl stop mnt.automount 2> /dev/null || true
  if mountpoint -q /mnt && ! umount /mnt; then
    echo "warning: /mnt is busy; the old stick comes off it at the next reboot" >&2
  fi
  echo "moved over: the stick no longer mounts on /mnt, but on $SRV (see below)"
fi

# The units, from this copy (not from the tree the service user owns): the show and the socket
# that starts it on the first remote command.
for unit in tvtv-show.service tvtv-show.socket; do
  sed "s|@TVTV_HOME@|$DEST|g; s|@TVTV_USER@|$U|g" "system/$unit" > "/etc/systemd/system/$unit"
done

# Exact sudo rules for the deploying user: build and sync as the service user, the unit files
# from stdin, the service and its socket, the keys, the component list. Nothing that runs a file
# the service user can write.
if [[ -n $deployer && $deployer != root ]]; then
  rules=$(mktemp)
  cat > "$rules" << RULES
# tvtv: scripts/deploy.sh and tv-keys for $deployer, written by tools/setup-remote.sh
Cmnd_Alias TVTV_AS_TV = $(command -v rsync), $(command -v make)
Cmnd_Alias TVTV_ROOT = $(command -v tee) /etc/systemd/system/tvtv-show.service, \\
    $(command -v tee) /etc/systemd/system/tvtv-show.socket, \\
    $(command -v systemctl) daemon-reload, $(command -v systemctl) enable --quiet tvtv-show, \\
    $(command -v systemctl) restart tvtv-show, $(command -v systemctl) stop tvtv-show, \\
    $(command -v systemctl) start tvtv-show, $(command -v systemctl) stop tvtv-show.socket, \\
    $(command -v systemctl) start tvtv-show.socket, /usr/local/sbin/tv-keys, \\
    /usr/local/sbin/tvtv-pi status, /usr/local/sbin/tvtv-pi du, /usr/local/sbin/tvtv-pi check
$deployer ALL=($U) NOPASSWD: TVTV_AS_TV
$deployer ALL=(root) NOPASSWD: TVTV_ROOT
RULES
  if visudo -c -q -f "$rules"; then
    install -m 0440 "$rules" "$SUDOERS"
    rm -f /etc/sudoers.d/tvtv-deploy # the rules from before docs/pi.md, now that these are in place
    echo "sudo: $deployer may deploy and manage keys without a password"
  else
    echo "sudoers rules did not validate, not installed (the old ones, if any, stay)" >&2
  fi
  rm -f "$rules"
else
  echo "no deploying user given (--deployer <user>): deploys will ask for sudo passwords" >&2
fi

# ---------- the USB stick: Doom's WADs and the clips, one ext4 stick with the label TVTV ----------
# Mounted on $SRV when something looks there, unmounted after a minute idle so it can be pulled,
# and a missing stick does not hold up the boot. Make one: docs/pi.md, "The USB stick".
install -d -m 0755 "$SRV"
line="LABEL=TVTV $SRV ext4 nosuid,nodev,noexec,nofail,noauto"
line+=",x-systemd.automount,x-systemd.idle-timeout=60,x-systemd.device-timeout=2s 0 2"
if ! grep -qxF "$line" /etc/fstab; then
  # the setup's own lines only: its comment and the line under it (the old /mnt one or an older /srv)
  sed -i '/^# tvtv: the USB stick/{N;d}' /etc/fstab
  if [[ -s /etc/fstab && -n $(tail -c 1 /etc/fstab) ]]; then echo >> /etc/fstab; fi
  printf '# tvtv: the USB stick, see docs/pi.md and tools/setup-remote.sh\n%s\n' "$line" >> /etc/fstab
  systemctl daemon-reload
  systemctl stop srv-tvtv.automount 2> /dev/null || true # the old options, if any, go with it
fi
systemctl start srv-tvtv.automount 2> /dev/null ||
  echo "warning: srv-tvtv.automount did not start (systemctl status srv-tvtv.automount)" >&2
if dev=$(blkid -L TVTV 2> /dev/null); then
  # Looking at it mounts it; the folders are made on the stick itself. doom/ is the deploying
  # login's (it copies WADs there), clips/ the service user's (uploads land there).
  if [[ $(blkid -o value -s TYPE "$dev") == ext4 ]] && ls "$SRV" > /dev/null 2>&1 && mountpoint -q "$SRV"; then
    install -d -m 0755 -o "${deployer:-root}" -g "$U" "$SRV/doom"
    install -d -m 0755 -o "$U" -g "$U" "$SRV/clips"
    echo "stick: $dev on $SRV, with doom/ and clips/"
  else
    echo "warning: the stick labelled TVTV ($dev) is not ext4 or does not mount; docs/pi.md, \"The USB stick\"" >&2
  fi
else
  for dev in /dev/sd[a-z]1; do
    [[ -b $dev ]] || continue
    echo "note: $dev is not the TVTV stick ($(blkid -o value -s TYPE "$dev" 2> /dev/null || echo unknown)); to use it, docs/pi.md, \"The USB stick\""
  done
fi

# ---------- room on the SD card ----------
# The journal stays small (it lives in RAM unless /var/log/journal exists), apt keeps no .deb
# files after an install. `tvtv-pi du` shows where the space goes, `tvtv-pi clean` frees it.
jconf=/etc/systemd/journald.conf.d/50-tvtv.conf
want=$(printf '# tvtv: docs/pi.md, "Room on the SD card"\n[Journal]\nSystemMaxUse=32M\nRuntimeMaxUse=8M\n')
if [[ $(cat "$jconf" 2> /dev/null) != "$want" ]]; then
  install -d -m 0755 "${jconf%/*}"
  printf '%s\n' "$want" > "$jconf"
  systemctl restart systemd-journald
  echo "journal: at most 32 MB on the card, 8 MB in RAM"
fi
aconf=/etc/apt/apt.conf.d/50-tvtv
printf '%s\n' '// tvtv: docs/pi.md, "Room on the SD card": no .deb files kept after an install' \
  'APT::Keep-Downloaded-Packages "false";' 'Binary::apt::APT::Keep-Downloaded-Packages "false";' > "$aconf"

# ---------- what this component installed (tvtv-pi reads it) ----------
cat > "$DB/show.new" << LIST
# The show, its remote and the base for every other component; written by tools/setup-remote.sh
name show
repo https://github.com/bmmmm/tvtv
version-file $DEST/VERSION
unit tvtv-show.socket
unit tvtv-show.service
file $LIB/tv-dispatch
file /usr/local/sbin/tv-keys
file /usr/local/sbin/tvtv-pi
file /etc/ssh/sshd_config.d/50-tvtv.conf
$([[ -f $SUDOERS ]] && echo "file $SUDOERS")
file $jconf
file $aconf
keep /etc/tvtv/authorized_keys
keep $DEST
keep $SRV
LIST
mv -f "$DB/show.new" "$DB/show"

# This copy's install.sh, not the one in $DEST: that tree is the service user's.
"$SRC/tools/install.sh" "$DEST"

cat << TXT

remote ready. Next:
  key:    ssh <pi> sudo tv-keys add <who> admin < <who>.pub      (or op: zap only)
  ssh:    Host zapper / HostName <this pi> / User $U / IdentityFile ~/.ssh/<key>   in ~/.ssh/config
          or, without an alias: scripts/tv config host <this pi's ip>; scripts/tv config user $U;
                                scripts/tv config identity ~/.ssh/<key>
  use:    ssh zapper      the zapper        ssh zapper 7 | next | status | help
          (scripts/tv, scripts/tv 7 ... with or without the alias)
  log:    journalctl -t tvtv
  layout: tvtv-pi status | du | check     (docs/pi.md)
TXT
