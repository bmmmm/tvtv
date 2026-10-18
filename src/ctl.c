// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "ctl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

const char *ctl_default_path(void) {
    static char path[108];
    const char *env = getenv("BC101_CTL");
    if (env && *env) return env;
    const char *rt = getenv("RUNTIME_DIRECTORY");
    if (rt && *rt) {
        snprintf(path, sizeof path, "%s/ctl", rt);
        return path;
    }
    return "/run/bc101/ctl";
}

static int set_addr(struct sockaddr_un *sa, const char *path) {
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    if (strlen(path) >= sizeof sa->sun_path) { errno = ENAMETOOLONG; return -1; }
    strcpy(sa->sun_path, path);
    return 0;
}

int ctl_listen(const char *path) {
    struct sockaddr_un sa;
    if (set_addr(&sa, path)) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    unlink(path); // a stale socket from an earlier run; the directory's mode guards access
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) || listen(fd, 8)) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

// Reads until newline, EOF or the deadline; a client that stalls cannot hold up the frame loop.
static int read_line(int fd, char *buf, size_t n, int ms) {
    size_t len = 0;
    while (len + 1 < n) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, ms) <= 0) break;
        ssize_t r = read(fd, buf + len, n - 1 - len);
        if (r <= 0) break;
        len += (size_t)r;
        if (memchr(buf, '\n', len)) break;
    }
    buf[len] = 0;
    char *nl = strpbrk(buf, "\r\n");
    if (nl) *nl = 0;
    return (int)strlen(buf);
}

int ctl_accept(int lfd, char *req, size_t n) {
    int cfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
    if (cfd < 0) return -1;
    if (read_line(cfd, req, n, 200) == 0) {
        ctl_reply(cfd, "err empty request\n");
        return -1;
    }
    return cfd;
}

static void write_all(int fd, const char *s, size_t n) {
    while (n) {
        struct pollfd p = {fd, POLLOUT, 0};
        if (poll(&p, 1, 500) <= 0) return;
        ssize_t w = write(fd, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

void ctl_reply(int cfd, const char *text) {
    write_all(cfd, text, strlen(text));
    close(cfd);
}

int ctl_request(const char *path, const char *req, char *reply, size_t n) {
    struct sockaddr_un sa;
    reply[0] = 0;
    if (set_addr(&sa, path)) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
        close(fd);
        return -1;
    }
    char line[CTL_MAX_REQ + 1];
    snprintf(line, sizeof line, "%s\n", req);
    write_all(fd, line, strlen(line));
    shutdown(fd, SHUT_WR);
    size_t len = 0;
    for (;;) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 3000) <= 0) break;
        ssize_t r = read(fd, reply + len, n - 1 - len);
        if (r <= 0) break;
        len += (size_t)r;
        if (len + 1 >= n) break;
    }
    reply[len] = 0;
    close(fd);
    if (!len) return -1;
    return strncmp(reply, "ok", 2) == 0 ? 0 : 1;
}

// The command line client: tvctl [-s socket] <command> [args], also `bc101 ctl ...`.
int ctl_main(int argc, char **argv) {
    const char *path = ctl_default_path();
    int i = 1;
    if (i + 1 < argc && !strcmp(argv[i], "-s")) { path = argv[i + 1]; i += 2; }
    if (i >= argc) {
        fprintf(stderr, "usage: bc101 ctl [-s socket] <command> [args]\n");
        return 2;
    }
    char req[CTL_MAX_REQ] = "", reply[CTL_MAX_REPLY];
    for (; i < argc; i++) {
        if (strlen(req) + strlen(argv[i]) + 2 >= sizeof req) break;
        if (*req) strcat(req, " ");
        strcat(req, argv[i]);
    }
    int rc = ctl_request(path, req, reply, sizeof reply);
    if (rc < 0) {
        fprintf(stderr, "show not reachable at %s\n", path);
        return 2;
    }
    fputs(reply, stdout);
    return rc;
}
