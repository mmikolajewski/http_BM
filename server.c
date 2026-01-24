// http_server.c
// Praca zaliczeniowa: Sieci komputerowe 2
// Bartosz Grabski - 59233
// Mikołaj Mikołajewski - 162364

// Linux + C + TCP + pthreads

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      
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

#include "include/utils.h"
#include "include/handlers.h"
#include "include/constants.h"

typedef struct {
    int client_fd;
    char root_dir[PATH_MAX];
} client_args_t;


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
    const char *root_dir = "public_html";

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

    printf("Server build at : %s\n", BUILD_TIME);
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
