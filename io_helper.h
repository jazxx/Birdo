#ifndef IO_HELPER_H
#define IO_HELPER_H

#include <stddef.h>
#include <sys/types.h>

int open_listen_fd(int port);
ssize_t writen(int fd, const void *usrbuf, size_t n);
int readline(int fd, char *usrbuf, int maxlen);

#endif
