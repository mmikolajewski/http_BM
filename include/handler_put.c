#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>

#include "handlers.h"
#include "constants.h"
#include "utils.h"

void handle_put(int client_fd,
                const char *root_dir,
                const char *url_path,
                const char *body_start,
                size_t body_in_buf,
                long content_length)
{
    if (content_length < 0)
    {
        const char *msg = "Length Required\r\n";
        send_response(client_fd, 411, "Length Required", "text/plain", msg, strlen(msg));
        return;
    }

    char full_path[PATH_MAX];
    if (build_full_path(root_dir, url_path, full_path, sizeof(full_path)) != 0)
    {
        const char *msg = "Bad Request\r\n";
        send_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
        return;
    }

    int existed_before = (access(full_path, F_OK) == 0);

    int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        const char *msg = "Internal Server Error\r\n";
        send_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
        return;
    }

    long remaining = content_length;

    // Najpierw to, co już mamy w buforze (body_in_buf).
    if (body_in_buf > 0)
    {
        size_t to_write = (body_in_buf > (size_t)remaining) ? (size_t)remaining : body_in_buf;
        ssize_t w = write(fd, body_start, to_write);
        if (w < 0)
        {
            close(fd);
            const char *msg = "Internal Server Error\r\n";
            send_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
            return;
        }
        remaining -= w;
    }

    char buf[IO_BUF_SIZE];
    while (remaining > 0)
    {
        ssize_t r = recv(client_fd, buf, (remaining > IO_BUF_SIZE) ? IO_BUF_SIZE : remaining, 0);
        if (r <= 0)
        {
            close(fd);
            const char *msg = "Bad Request\r\n";
            send_response(client_fd, 400, "Bad Request", "text/plain", msg, strlen(msg));
            return;
        }
        ssize_t w = write(fd, buf, r);
        if (w < 0 || w != r)
        {
            close(fd);
            const char *msg = "Internal Server Error\r\n";
            send_response(client_fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
            return;
        }
        remaining -= r;
    }

    close(fd);

    if (existed_before)
    {
        // Nadpisano
        send_response(client_fd, 200, "OK", "text/plain", "OK\r\n", 4);
    }
    else
    {
        // Utworzono nowy plik
        send_response(client_fd, 201, "Created", "text/plain", "Created\r\n", 9);
    }
}
