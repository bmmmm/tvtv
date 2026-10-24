#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# A stand-in for the control socket of the bridge tvtv-social (its docs/steuerung.md) in the
# tests: one request line per connection, as the show's own socket; every request goes to the
# log file, and the replies are canned ones of the right shape:
#   ping, status, list, mode [name], pause, resume, ban|unban <who>, bans, rm ..., submit <who> ...
# Usage: fake-social.py <socket> <logfile>
import os
import socket
import sys

path, logfile = sys.argv[1], sys.argv[2]
MODES = ["turbo", "party", "normal", "ruhig", "aus"]


def reply(req):
    w = req.split(" ")
    if w == ["ping"]:
        return "ok"
    if w == ["status"]:
        return ("ok\nmode=normal\npaused=0\nseq=12\nevents=34\nprograms=1\ntexts=2\npixels=56\n"
                "mastodon=ok\nmastodon_last=2026-10-24T13:37:05Z\nmastodon_error=\naccepted=42 rejected=7 ignored=3")
    if w == ["list"]:
        return ("ok\ncode 1 @alice@bonn.social 2026-10-24T13:30:00Z sin(x/9+t)*99\n"
                "text 1 GRÜSSE @carol@bonn.social 2026-10-24T13:31:02Z Hallo bitcircus!\n"
                "pixel @dave@bonn.social 2026-10-24T13:36:59Z 12 30 2")
    if w == ["mode"]:
        return "ok normal\n" + "\n".join("%s px=60 code=120 text=120 announce=1 loop=1" % m for m in MODES)
    if w[0] == "mode" and len(w) == 2:
        return "ok mode=" + w[1] if w[1] in MODES else "err mode unbekannter Modus"
    if w == ["pause"]:
        return "ok paused=1"
    if w == ["resume"]:
        return "ok paused=0"
    if w[0] in ("ban", "unban") and len(w) == 2:
        return "ok"
    if w == ["bans"]:
        return "ok\nmastodon:spam@example.social\nexample.social"
    if w[0] == "rm":
        return "ok"
    if w[0] == "submit" and len(w) >= 4 and w[2] == "px":
        return "ok accepted pixel 57" if w[3] != "99" else "err px_range x 0-63, y 0-47."
    if w[0] == "submit" and len(w) >= 4 and w[2] == "text":
        return "ok accepted text 58"
    return "err unknown unbekannter Befehl"


try:
    os.unlink(path)
except FileNotFoundError:
    pass
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(path)
srv.listen(4)
while True:
    conn, _ = srv.accept()
    with conn:
        req = b""
        while not req.endswith(b"\n") and len(req) < 300:
            b = conn.recv(1)
            if not b:
                break
            req += b
        req = req.decode(errors="replace").strip()
        with open(logfile, "a") as f:
            f.write(req + "\n")
        conn.sendall((reply(req) + "\n").encode())
