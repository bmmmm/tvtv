// SPDX-License-Identifier: GPL-3.0-or-later
// tvctl: the remote's client, built without any scene code so that an uploaded visual can never
// take it down. tools/tv-dispatch uses it for every command and for the zapper.
//   tvctl [-s socket] <command> [args]      one command, prints the reply (see main.c for the list)
//   tvctl remote [-s socket] [options]      the zapper (see remote.c)
#include "ctl.h"
#include "remote.h"

#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "remote")) return remote_main(argc - 1, argv + 1);
    return ctl_main(argc, argv);
}
