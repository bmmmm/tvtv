#!/usr/bin/env bash
# Collects boot timing and network logs of the current boot for analysis.
# Run on the Pi:  sudo ~deploy/tvtv/tools/boot-report.sh
# Writes a fresh /tmp/boot-report.XXXXXX (world-readable) and prints its name.
# Values after password/psk/secret/key are redacted and WPA key dumps dropped;
# SSIDs, MAC and IP addresses remain.
set -u

if [[ $EUID -ne 0 ]]; then
  echo "run with sudo: the journal sections need root" >&2
  exit 1
fi

out=$(mktemp /tmp/boot-report.XXXXXX) || exit 1
exec 3>"$out" || exit 1

section() { printf '\n===== %s =====\n' "$*"; }
scrub() {
  grep -vE 'hexdump|PMK|PTK|GTK' |
    sed -E 's/((psk|passw|passphrase|secret|key)[^=:]*[=:]).*/\1 <redacted>/I'
}

{
  section "date / uptime"
  date
  uptime
  section "systemd-analyze"
  systemd-analyze
  section "blame (top 30)"
  systemd-analyze blame | head -30
  section "critical chain"
  systemd-analyze critical-chain
  section "critical chain NetworkManager"
  systemd-analyze critical-chain NetworkManager.service
  section "nmcli"
  nmcli -f DEVICE,TYPE,STATE,CONNECTION device
  nmcli -f NAME,TYPE,DEVICE,AUTOCONNECT connection show
  section "journal: NetworkManager, wait-online, wpa_supplicant"
  journalctl -b -o short-monotonic --no-pager \
    -u NetworkManager -u NetworkManager-wait-online -u wpa_supplicant | scrub
  section "kernel: wifi / usb / firmware"
  journalctl -b -k -o short-monotonic --no-pager |
    grep -iE 'wlan|wifi|80211|firmware|usb [0-9]|rtl|mt7|brcm|ath' | scrub
  section "lsusb"
  lsusb
  section "iw"
  iw dev
  iw reg get
} >&3 2>&1
exec 3>&-

chmod 644 "$out"
echo "wrote $out ($(wc -l < "$out") lines)"
