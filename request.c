#define _XOPEN_SOURCE 700

#include "request.h"
#include "io_helper.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAXLINE 8192
#define FILEBUF 8192

static void discard_headers(int client_fd) {
    char buf[MAXLINE];
    int n;

    while ((n = readline(client_fd, buf, MAXLINE)) > 0) {
        if (strcmp(buf, "\r\n") == 0 || strcmp(buf, "\n") == 0) {
            break;
        }
    }
}

static void send_error(int client_fd, int status, const char *shortmsg, const char *longmsg) {
    char body[MAXLINE];
    char header[MAXLINE];
    int body_len;

    body_len = snprintf(body, sizeof(body),
                        "<html><head><title>%d %s</title></head>"
                        "<body><h1>%d %s</h1><p>%s</p></body></html>",
                        status, shortmsg, status, shortmsg, longmsg);

    snprintf(header, sizeof(header),
             "HTTP/1.0 %d %s\r\n"
             "Server: pi-wserver\r\n"
             "Connection: close\r\n"
             "Content-Type: text/html; charset=utf-8\r\n"
             "Content-Length: %d\r\n\r\n",
             status, shortmsg, body_len);

    (void)writen(client_fd, header, strlen(header));
    (void)writen(client_fd, body, (size_t)body_len);
}

static const char *mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }

    ext++;
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, "css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(ext, "js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, "json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(ext, "txt") == 0) return "text/plain; charset=utf-8";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    if (strcasecmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, "ico") == 0) return "image/x-icon";
    if (strcasecmp(ext, "pdf") == 0) return "application/pdf";
    return "application/octet-stream";
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool url_decode(const char *src, char *dst, size_t dstsz) {
    size_t si = 0;
    size_t di = 0;

    while (src[si] != '\0') {
        if (di + 1 >= dstsz) {
            return false;
        }

        if (src[si] == '%') {
            int hi = hex_value(src[si + 1]);
            int lo = hex_value(src[si + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            dst[di++] = (char)((hi << 4) | lo);
            si += 3;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
            si++;
        } else {
            dst[di++] = src[si++];
        }
    }

    dst[di] = '\0';
    return true;
}

static bool contains_parent_ref(const char *path) {
    const char *p = path;

    while ((p = strstr(p, "..")) != NULL) {
        char left = (p == path) ? '/' : p[-1];
        char right = p[2];
        bool left_sep = (left == '/' || left == '\\');
        bool right_sep = (right == '/' || right == '\\' || right == '\0');
        if (left_sep && right_sep) {
            return true;
        }
        p += 2;
    }
    return false;
}

static bool build_path(const char *web_root, const char *uri, char *out, size_t outsz) {
    char path_only[MAXLINE];
    char decoded[MAXLINE];
    const char *query;
    const char *rel;

    query = strchr(uri, '?');
    if (query == NULL) {
        query = uri + strlen(uri);
    }

    if ((size_t)(query - uri) >= sizeof(path_only)) {
        return false;
    }

    memcpy(path_only, uri, (size_t)(query - uri));
    path_only[query - uri] = '\0';

    if (!url_decode(path_only, decoded, sizeof(decoded))) {
        return false;
    }

    if (decoded[0] != '/') {
        return false;
    }

    if (contains_parent_ref(decoded) || strchr(decoded, '\\') != NULL) {
        return false;
    }

    rel = decoded + 1;
    if (*rel == '\0') {
        rel = "index.html";
    }

    if (snprintf(out, outsz, "%s/%s", web_root, rel) >= (int)outsz) {
        return false;
    }

    return true;
}

static void serve_file(int client_fd, const char *path, bool head_only) {
    struct stat st;
    int file_fd;
    char header[MAXLINE];
    char buf[FILEBUF];

    if (stat(path, &st) < 0) {
        if (errno == EACCES) {
            send_error(client_fd, 403, "Forbidden", "Permission denied.");
        } else {
            send_error(client_fd, 404, "Not Found", "File not found.");
        }
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        char index_path[PATH_MAX];
        if (snprintf(index_path, sizeof(index_path), "%s/index.html", path) >= (int)sizeof(index_path)) {
            send_error(client_fd, 414, "URI Too Long", "Requested path is too long.");
            return;
        }
        if (stat(index_path, &st) < 0 || !S_ISREG(st.st_mode)) {
            send_error(client_fd, 403, "Forbidden", "Directory listing is disabled.");
            return;
        }
        path = index_path;
    }

    if (!S_ISREG(st.st_mode)) {
        send_error(client_fd, 403, "Forbidden", "Only regular files can be served.");
        return;
    }

    file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        send_error(client_fd, 403, "Forbidden", "Could not open file.");
        return;
    }

    snprintf(header, sizeof(header),
             "HTTP/1.0 200 OK\r\n"
             "Server: pi-wserver\r\n"
             "Connection: close\r\n"
             "Content-Length: %lld\r\n"
             "Content-Type: %s\r\n\r\n",
             (long long)st.st_size, mime_type(path));

    (void)writen(client_fd, header, strlen(header));

    if (!head_only) {
        ssize_t nread;
        while ((nread = read(file_fd, buf, sizeof(buf))) > 0) {
            if (writen(client_fd, buf, (size_t)nread) < 0) {
                break;
            }
        }
    }

    close(file_fd);
}

void handle_request(int client_fd, const char *web_root) {
    char buf[MAXLINE];
    char method[32];
    char uri[MAXLINE];
    char version[32];
    char path[PATH_MAX];
    int n;
    bool head_only = false;

    n = readline(client_fd, buf, MAXLINE);
    if (n <= 0) {
        return;
    }

    if (sscanf(buf, "%31s %8191s %31s", method, uri, version) != 3) {
        send_error(client_fd, 400, "Bad Request", "Malformed request line.");
        return;
    }


    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
        send_error(client_fd, 501, "Not Implemented", "Only GET and HEAD are supported.");
        return;
    }

    head_only = (strcasecmp(method, "HEAD") == 0);

    if (strncmp(version, "HTTP/1.", 7) != 0) {
        send_error(client_fd, 400, "Bad Request", "Unsupported HTTP version.");
        return;
    }

    if (!build_path(web_root, uri, path, sizeof(path))) {
        send_error(client_fd, 400, "Bad Request", "Invalid request path.");
        return;
    }

    serve_file(client_fd, path, head_only);
}
