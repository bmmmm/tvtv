// SPDX-License-Identifier: GPL-3.0-or-later
// The control socket: a Unix stream socket next to the show (default /run/bc101/ctl), one request
// per connection. Request: one line. Reply: a first line "ok [text]" or "err <reason>", then
// optional payload lines; the server closes when done. Used by `bc101 ctl`, the remote and
// tools/tv-dispatch; the sequencer polls it once per frame.
#pragma once
#include <stddef.h>

#define CTL_MAX_REQ 256
#define CTL_MAX_REPLY 8192

const char *ctl_default_path(void); // $BC101_CTL, else $RUNTIME_DIRECTORY/ctl, else /run/bc101/ctl

// server
int ctl_listen(const char *path);                   // listening fd (non-blocking) or -1 with errno
int ctl_inherited(void);                            // the socket systemd passed (bc101.socket) or -1
int ctl_accept(int lfd, char *req, size_t n);       // a client fd with its request line, or -1 if none
void ctl_reply(int cfd, const char *text);          // writes the reply and closes cfd

// client: 0 if the reply starts with "ok", 1 for "err", -1 if the show is not reachable
int ctl_request(const char *path, const char *req, char *reply, size_t n);
int ctl_main(int argc, char **argv); // tvctl [-s socket] <command> [args]: prints the reply, exit 0/1/2
