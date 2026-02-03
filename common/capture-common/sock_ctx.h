#ifndef SOCK_CTX_H
#define SOCK_CTX_H

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <time.h>
#include "log.h"

#define SOCK_MAX_CLIENTS 8
#define SOCK_IDLE_TIMEOUT_MS 3000
#define SOCK_WRITE_TIMEOUT_MS 100

typedef struct {
    int fd;
    size_t last_size;
    struct timespec last_time;
    int num_frames;
    int num_dropped;
} sock_client_t;

#define DEFAULT_SOCK_CLIENT {.fd = -1}

typedef struct {
    const char *path;
    int listen_fd;
    sock_client_t clients[SOCK_MAX_CLIENTS];
    int num_clients;
    bool one_frame;
    bool need_keyframe;
    bool allow_drops;
} sock_ctx_t;

#define DEFAULT_SOCK_CTX {.path = NULL, .listen_fd = -1}

static int sock_open(sock_ctx_t *ctx, const char *path)
{
    ctx->path = path;
    ctx->listen_fd = -1;
    ctx->num_clients = 0;

    for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
        ctx->clients[i].fd = -1;
    }

    unlink(path);

    ctx->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ctx->listen_fd < 0) {
        log_perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_perror("bind");
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
        return -1;
    }

    chmod(path, 0777);

    if (listen(ctx->listen_fd, SOCK_MAX_CLIENTS) < 0) {
        log_perror("listen");
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
        return -1;
    }

    int flags = fcntl(ctx->listen_fd, F_GETFL, 0);
    fcntl(ctx->listen_fd, F_SETFL, flags | O_NONBLOCK);

    return 0;
}

static void sock_close(sock_ctx_t *ctx)
{
    for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
        if (ctx->clients[i].fd >= 0) {
            close(ctx->clients[i].fd);
            ctx->clients[i].fd = -1;
        }
    }
    ctx->num_clients = 0;

    if (ctx->listen_fd >= 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
    }

    if (ctx->path) {
        unlink(ctx->path);
    }
}

static bool sock_accept_clients(sock_ctx_t *ctx)
{
    if (ctx->listen_fd < 0)
        return false;

    bool accepted = false;

    while (1) {
        int client_fd = accept(ctx->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            log_perror("accept");
            break;
        }

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        sock_client_t *slot = NULL;
        for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
            if (ctx->clients[i].fd < 0) {
                slot = &ctx->clients[i];
                break;
            }
        }

        if (slot) {
            slot->fd = client_fd;
            slot->last_size = 0;
            slot->num_frames = 0;
            slot->num_dropped = 0;
            clock_gettime(CLOCK_MONOTONIC, &slot->last_time);
            ctx->num_clients++;
            ctx->need_keyframe = true;
            accepted = true;
            log_printf("Socket %s: client connected (total %d)\n", ctx->path, ctx->num_clients);
        } else {
            close(client_fd);
            log_printf("Socket %s: rejected client, max reached\n", ctx->path);
        }
    }

    return accepted;
}

static void sock_wait_fds(sock_ctx_t *socks[], int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    int maxfd = -1;

    FD_ZERO(&rfds);
    for (int i = 0; socks[i]; i++) {
        if (socks[i]->listen_fd < 0)
            continue;

        FD_SET(socks[i]->listen_fd, &rfds);
        if (socks[i]->listen_fd > maxfd)
            maxfd = socks[i]->listen_fd;
    }

    if (maxfd < 0) {
        usleep(timeout_ms * 1000);
        return;
    }

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    select(maxfd + 1, &rfds, NULL, NULL, &tv);
}

static ssize_t sock_write_client_fd(int fd, const void *data, size_t size)
{
    const char *ptr = data;
    size_t remaining = size;
    struct timespec start, now;

    clock_gettime(CLOCK_MONOTONIC, &start);

    while (remaining > 0) {
        ssize_t written = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                clock_gettime(CLOCK_MONOTONIC, &now);
                long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                                  (now.tv_nsec - start.tv_nsec) / 1000000;

                if (elapsed_ms >= SOCK_WRITE_TIMEOUT_MS) {
                    errno = ETIMEDOUT;
                    return -1;
                }

                usleep(1000);
                continue;
            }
            return written;
        }
        ptr += written;
        remaining -= written;
    }

    return size;
}

static void sock_close_client(sock_ctx_t *ctx, int i, const char *reason)
{
    assert(i >= 0 && i < SOCK_MAX_CLIENTS);
    assert(ctx->clients[i].fd >= 0);
    assert(ctx->num_clients > 0);
    log_printf("Socket %s: client %d %s, closing (frames=%d, dropped=%d)\n",
               ctx->path, i, reason, ctx->clients[i].num_frames, ctx->clients[i].num_dropped);
    close(ctx->clients[i].fd);
    ctx->clients[i].fd = -1;
    ctx->num_clients--;
}

static void sock_write_cb(const void *data, size_t size, void *arg)
{
    sock_ctx_t *ctx = arg;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
        sock_client_t *client = &ctx->clients[i];
        if (client->fd < 0)
            continue;

        long idle_ms = (now.tv_sec - client->last_time.tv_sec) * 1000 +
                       (now.tv_nsec - client->last_time.tv_nsec) / 1000000;
        if (idle_ms >= SOCK_IDLE_TIMEOUT_MS) {
            sock_close_client(ctx, i, "idle timeout");
            continue;
        }

        if (ctx->allow_drops && client->last_size) {
            int unsent = 0;
            if (ioctl(client->fd, SIOCOUTQ, &unsent) == 0) {
                if ((size_t)unsent >= client->last_size) {
                    client->num_dropped++;
                    continue;
                }
            }
        }

        if (sock_write_client_fd(client->fd, data, size) < 0) {
            if (errno == ETIMEDOUT) {
                sock_close_client(ctx, i, "write timeout");
            } else {
                sock_close_client(ctx, i, "write error");
            }
            continue;
        }

        client->last_size = size;
        client->last_time = now;
        client->num_frames++;

        if (ctx->one_frame) {
            sock_close_client(ctx, i, "one frame sent");
        }
    }
}

#endif
