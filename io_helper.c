#include "io_helper.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define LISTENQ 1024

ssize_t writen(int fd, const void *usrbuf, size_t n) {
    size_t nleft = n;
    const char *bufp = (const char *)usrbuf;

    while (nleft > 0) {
        ssize_t nwritten = write(fd, bufp, nleft);
        if (nwritten <= 0) {
            if (errno == EINTR) {
                nwritten = 0;
            } else {
                return -1;
            }
        }
        nleft -= (size_t)nwritten;
        bufp += nwritten;
    }

    return (ssize_t)n;
}

int readline(int fd, char *usrbuf, int maxlen) {
    int n;
    int rc;
    char c;
    char *bufp = usrbuf;

    for (n = 1; n < maxlen; n++) {
        rc = (int)read(fd, &c, 1);
        if (rc == 1) {
            *bufp++ = c;
            if (c == '\n') {
                break;
            }
        } else if (rc == 0) {
            if (n == 1) {
                return 0;
            }
            break;
        } else {
            if (errno == EINTR) {
                n--;
                continue;
            }
            return -1;
        }
    }

    *bufp = '\0';
    return n;
}

int open_listen_fd(int port) {
    char portstr[16];
    struct addrinfo hints;
    struct addrinfo *listp = NULL;
    struct addrinfo *p;
    int listenfd = -1;
    int optval = 1;
    int rc;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV;

    if ((rc = getaddrinfo(NULL, portstr, &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(rc));
        return -1;
    }

    for (p = listp; p != NULL; p = p->ai_next) {
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenfd < 0) {
            continue;
        }

        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            close(listenfd);
            listenfd = -1;
            continue;
        }

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }

        close(listenfd);
        listenfd = -1;
    }

    freeaddrinfo(listp);

    if (listenfd < 0) {
        return -1;
    }

    if (listen(listenfd, LISTENQ) < 0) {
        close(listenfd);
        return -1;
    }

    return listenfd;
}
