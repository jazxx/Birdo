#define _XOPEN_SOURCE 700

#include "io_helper.h"
#include "request.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-p port] [-d web_root]\n", prog);
}

int main(int argc, char **argv) {
    int port = 8080;
    const char *web_root = ".";
    char resolved_root[PATH_MAX];
    int listen_fd;
    int opt;

    while ((opt = getopt(argc, argv, "p:d:h")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                web_root = optarg;
                break;
            case 'h':
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %d\n", port);
        return 1;
    }

    if (realpath(web_root, resolved_root) == NULL) {
        perror("realpath(web_root)");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    listen_fd = open_listen_fd(port);
    if (listen_fd < 0) {
        perror("open_listen_fd");
        return 1;
    }

    printf("Serving %s on port %d\n", resolved_root, port);

    while (true) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }

        handle_request(client_fd, resolved_root);
        close(client_fd);
    }

    close(listen_fd);
    return 0;
}
