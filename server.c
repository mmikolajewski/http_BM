// http_server.c
// Bardzo prosty współbieżny serwer HTTP (GET, HEAD, PUT, DELETE)
// Linux + C + TCP + pthreads, zgodny w podstawowym zakresie z RFC2616.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      // strcasecmp, strncasecmp //kurwa jego mać
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <limits.h>

#define HEADER_BUF_SIZE 8192
#define IO_BUF_SIZE     8192

typedef struct {
    int client_fd;
    char root_dir[PATH_MAX];
} client_args_t;

static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++;
    if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) return "text/html";
    if (!strcasecmp(ext, "txt")  || !strcasecmp(ext, "log")) return "text/plain";
    if (!strcasecmp(ext, "jpg")  || !strcasecmp(ext, "jpeg")) return "image/jpeg";
    if (!strcasecmp(ext, "png")) return "image/png";
    if (!strcasecmp(ext, "gif")) return "image/gif";
    if (!strcasecmp(ext, "css")) return "text/css";
    if (!strcasecmp(ext, "js"))  return "application/javascript";
    return "application/octet-stream";
}

static void send_simple_response(int fd,
                                 int status_code,
                                 const char *reason,
                                 const char *content_type,
                                 const char *body,
                                 size_t body_len)
{
    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Server: SimpleCServer/0.1\r\n"
                     "Connection: close\r\n"
                     "Content-Length: %zu\r\n"
                     "%s%s\r\n",
                     status_code, reason,
                     body_len,
                     content_type ? "Content-Type: " : "",
                     content_type ? content_type : "");
    send(fd, header, n, 0);
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

static int build_full_path(const char *root_dir,
                           const char *url_path,
                           char *out, size_t out_size)
{
    // Zakazujemy ".." w ścieżce (bardzo prymitywne "zabezpieczenie").
    if (strstr(url_path, "..") != NULL) {
        return -1;
    }

    char rel[PATH_MAX];

    if (strcmp(url_path, "/") == 0) {
        // Domyślnie serwujemy index.html
        snprintf(rel, sizeof(rel), "/index.html");
    } else {
        snprintf(rel, sizeof(rel), "%s", url_path);
    }

    // Sklej root_dir + rel
    int n = snprintf(out, out_size, "%s%s", root_dir, rel);
    if (n < 0 || (size_t)n >= out_size) {
        return -1;
    }
    return 0;
}

static long parse_content_length(const char *headers, const char *headers_end) {
    const char *p = headers;
    while (p < headers_end) {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end || line_end > headers_end) {
            break;
        }
        if (!strncasecmp(p, "Content-Length:", 15)) {
            // p + 15 -> dalej będzie liczba
            const char *num_start = p + 15;
            while (num_start < line_end && (*num_start == ' ' || *num_start == '\t'))
                num_start++;
            char tmp[64];
            size_t len = (size_t)(line_end - num_start);
            if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
            memcpy(tmp, num_start, len);
            tmp[len] = '\0';
            char *endptr = NULL;
            long val = strtol(tmp, &endptr, 10);
            if (endptr == tmp) {
                return -1;
            }
            return val;
        }
        p = line_end + 2;
    }
    return -1; // nie znaleziono
}

static void handle_get_head(int client_fd,
                            const char *root_dir,
                            const char *url_path,
                            int is_head)
{
    char full_path[PATH_MAX];
    if (build_full_path(root_dir, url_path, full_path, sizeof(full_path)) != 0) {
        const char *msg = "Bad Request\r\n";
        send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        return;
    }

    struct stat st;
    if (stat(full_path, &st) < 0) {
        const char *msg = "Not Found\r\n";
        send_simple_response(client_fd, 404, "Not Found", "text/plain", msg, strlen(msg));
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        const char *msg = "Forbidden\r\n";
        send_simple_response(client_fd, 403, "Forbidden", "text/plain", msg, strlen(msg));
        return;
    }

    int file_fd = open(full_path, O_RDONLY);
    if (file_fd < 0) {
        const char *msg = "Internal Server Error\r\n";
        send_simple_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
        return;
    }

    const char *mime = get_mime_type(full_path);

    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Server: SimpleCServer/0.1\r\n"
                     "Connection: close\r\n"
                     "Content-Length: %ld\r\n"
                     "Content-Type: %s\r\n"
                     "\r\n",
                     (long)st.st_size,
                     mime);
    send(client_fd, header, n, 0);

    if (!is_head) {
        char buf[IO_BUF_SIZE];
        ssize_t r;
        while ((r = read(file_fd, buf, sizeof(buf))) > 0) {
            ssize_t off = 0;
            while (off < r) {
                ssize_t s = send(client_fd, buf + off, r - off, 0);
                if (s <= 0) {
                    close(file_fd);
                    return;
                }
                off += s;
            }
        }
    }

    close(file_fd);
}

static void handle_put(int client_fd,
                       const char *root_dir,
                       const char *url_path,
                       const char *body_start,
                       size_t body_in_buf,
                       long content_length)
{
    if (content_length < 0) {
        const char *msg = "Length Required\r\n";
        send_simple_response(client_fd, 411, "Length Required", "text/plain", msg, strlen(msg));
        return;
    }

    char full_path[PATH_MAX];
    if (build_full_path(root_dir, url_path, full_path, sizeof(full_path)) != 0) {
        const char *msg = "Bad Request\r\n";
        send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        return;
    }

    int existed_before = (access(full_path, F_OK) == 0);

    int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        const char *msg = "Internal Server Error\r\n";
        send_simple_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
        return;
    }

    long remaining = content_length;

    // Najpierw to, co już mamy w buforze (body_in_buf).
    if (body_in_buf > 0) {
        size_t to_write = (body_in_buf > (size_t)remaining) ? (size_t)remaining : body_in_buf;
        ssize_t w = write(fd, body_start, to_write);
        if (w < 0) {
            close(fd);
            const char *msg = "Internal Server Error\r\n";
            send_simple_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
            return;
        }
        remaining -= w;
    }

    char buf[IO_BUF_SIZE];
    while (remaining > 0) {
        ssize_t r = recv(client_fd, buf, (remaining > IO_BUF_SIZE) ? IO_BUF_SIZE : remaining, 0);
        if (r <= 0) {
            close(fd);
            const char *msg = "Bad Request\r\n";
            send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
            return;
        }
        ssize_t w = write(fd, buf, r);
        if (w < 0 || w != r) {
            close(fd);
            const char *msg = "Internal Server Error\r\n";
            send_simple_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
            return;
        }
        remaining -= r;
    }

    close(fd);

    if (existed_before) {
        // Nadpisano
        send_simple_response(client_fd, 200, "OK", "text/plain", "OK\r\n", 4);
    } else {
        // Utworzono nowy plik
        send_simple_response(client_fd, 201, "Created", "text/plain", "Created\r\n", 9);
    }
}

static void handle_delete(int client_fd,
                          const char *root_dir,
                          const char *url_path)
{
    char full_path[PATH_MAX];
    if (build_full_path(root_dir, url_path, full_path, sizeof(full_path)) != 0) {
        const char *msg = "Bad Request\r\n";
        send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        return;
    }

    if (unlink(full_path) == 0) {
        // 200 OK (można też 204 No Content)
        send_simple_response(client_fd, 200, "OK", "text/plain", "Deleted\r\n", 9);
    } else {
        if (errno == ENOENT) {
            const char *msg = "Not Found\r\n";
            send_simple_response(client_fd, 404, "Not Found", "text/plain", msg, strlen(msg));
        } else {
            const char *msg = "Internal Server Error\r\n";
            send_simple_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
        }
    }
}

static void *client_thread(void *arg) {
    client_args_t *cargs = (client_args_t *)arg;
    int client_fd = cargs->client_fd;
    char root_dir[PATH_MAX];
    snprintf(root_dir, sizeof(root_dir), "%s", cargs->root_dir);
    free(cargs);

    char buf[HEADER_BUF_SIZE];
    size_t total = 0;
    ssize_t r;
    char *header_end = NULL;

    // Czytamy nagłówki aż do "\r\n\r\n"
    while (total < sizeof(buf) - 1) {
        r = recv(client_fd, buf + total, sizeof(buf) - 1 - total, 0);
        if (r <= 0) {
            close(client_fd);
            return NULL;
        }
        total += r;
        buf[total] = '\0';

        header_end = strstr(buf, "\r\n\r\n");
        if (header_end) break;
    }

    if (!header_end) {
        // Za długie nagłówki albo brak końca
        const char *msg = "Bad Request\r\n";
        send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    // Parsowanie pierwszej linii: METHOD PATH VERSION
    char method[8], url_path[1024], version[16];
    if (sscanf(buf, "%7s %1023s %15s", method, url_path, version) != 3) {
        const char *msg = "Bad Request\r\n";
        send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    // Prosta walidacja wersji
    if (strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1")) {
        const char *msg = "HTTP Version Not Supported\r\n";
        send_simple_response(client_fd, 505, "HTTP Version Not Supported", "text/plain", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    // Wskaźniki na nagłówki
    char *headers_start = strstr(buf, "\r\n");
    if (!headers_start) {
        const char *msg = "Bad Request\r\n";
        send_simple_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }
    headers_start += 2; // po pierwszym CRLF (po request line)

    char *headers_end_ptr = header_end; // pokazuje na "\r\n\r\n"
    long content_length = parse_content_length(headers_start, headers_end_ptr);

    // Body zaczyna się po "\r\n\r\n"
    char *body_start = header_end + 4;
    size_t header_total_len = (size_t)(body_start - buf);
    size_t body_in_buf = total > header_total_len ? (total - header_total_len) : 0;

    // Obsługa metod
    if (!strcasecmp(method, "GET")) {
        handle_get_head(client_fd, root_dir, url_path, 0);
    } else if (!strcasecmp(method, "HEAD")) {
        handle_get_head(client_fd, root_dir, url_path, 1);
    } else if (!strcasecmp(method, "PUT")) {
        handle_put(client_fd, root_dir, url_path, body_start, body_in_buf, content_length);
    } else if (!strcasecmp(method, "DELETE")) {
        handle_delete(client_fd, root_dir, url_path);
    } else {
        const char *msg = "Method Not Allowed\r\n";
        send_simple_response(client_fd, 405, "Method Not Allowed", "text/plain", msg, strlen(msg));
    }

    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = 8080;
    const char *root_dir = ".";

    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port\n");
            return 1;
        }
    }

    if (argc >= 3) {
        root_dir = argv[2];
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("HTTP server listening on port %d, root dir: %s\n", port, root_dir);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        client_args_t *cargs = malloc(sizeof(client_args_t));
        if (!cargs) {
            fprintf(stderr, "Out of memory\n");
            close(client_fd);
            continue;
        }
        cargs->client_fd = client_fd;
        snprintf(cargs->root_dir, sizeof(cargs->root_dir), "%s", root_dir);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, cargs) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(cargs);
            continue;
        }
        pthread_detach(tid); // nie musimy joinować
    }

    close(server_fd);
    return 0;
}
