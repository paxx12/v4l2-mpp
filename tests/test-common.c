// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
// SPDX-FileCopyrightText: Copyright (c) 2026 @paxx12

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "h264_frames.h"
#include "sock_ctx.h"

static void test_h264_starts_with_idr(void)
{
    static const uint8_t idr_4byte[] = {
        0, 0, 0, 1, 0x67, 1, 2,
        0, 0, 0, 1, 0x68, 3, 4,
        0, 0, 0, 1, 0x65, 0x80, 6,
    };
    static const uint8_t idr_3byte[] = {
        0, 0, 1, 0x09, 0xf0,
        0, 0, 1, 0x65, 0x80, 6,
    };
    static const uint8_t non_idr[] = {
        0, 0, 0, 1, 0x67, 1, 2,
        0, 0, 1, 0x41, 0x80, 6,
        0, 0, 1, 0x65, 0x80, 8,
    };
    static const uint8_t continued_slice[] = { 0, 0, 1, 0x41, 0x40, 6 };
    static const uint8_t no_slice[] = { 0, 0, 0, 1, 0x67, 1, 2 };
    static const uint8_t short_data[] = { 0, 0, 1 };

    const uint8_t *idr_nal = h264_find_nal(idr_4byte + 14, sizeof(idr_4byte) - 14);
    const uint8_t *non_idr_nal = h264_find_nal(non_idr + 7, sizeof(non_idr) - 7);
    assert(idr_nal && h264_is_new_frame(idr_nal, sizeof(idr_4byte) - (size_t)(idr_nal - idr_4byte)));
    assert(idr_nal && h264_is_idr_frame(idr_nal, sizeof(idr_4byte) - (size_t)(idr_nal - idr_4byte)));
    assert(non_idr_nal && h264_is_new_frame(non_idr_nal, sizeof(non_idr) - (size_t)(non_idr_nal - non_idr)));
    assert(non_idr_nal && !h264_is_idr_frame(non_idr_nal, sizeof(non_idr) - (size_t)(non_idr_nal - non_idr)));
    assert(!h264_is_new_frame(continued_slice, sizeof(continued_slice)));

    assert(h264_starts_with_idr(idr_4byte, sizeof(idr_4byte)));
    assert(h264_starts_with_idr(idr_3byte, sizeof(idr_3byte)));
    assert(!h264_starts_with_idr(non_idr, sizeof(non_idr)));
    assert(!h264_starts_with_idr(no_slice, sizeof(no_slice)));
    assert(!h264_starts_with_idr(short_data, sizeof(short_data)));
}

static void assert_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);
    assert(flags >= 0);
    assert(flags & FD_CLOEXEC);
}

static void test_socket_cloexec(void)
{
    const char *path = "test-common.sock";
    sock_ctx_t ctx = DEFAULT_SOCK_CTX;
    int client = -1;

    unlink(path);
    assert(sock_open(&ctx, path) == 0);
    assert_cloexec(ctx.listen_fd);

    client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(client >= 0);

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    assert(connect(client, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(sock_accept_clients(&ctx));
    assert(ctx.num_clients == 1);

    for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
        if (ctx.clients[i].fd >= 0)
            assert_cloexec(ctx.clients[i].fd);
    }

    close(client);
    sock_close(&ctx);
    assert(access(path, F_OK) < 0);
}

int main(void)
{
    test_h264_starts_with_idr();
    test_socket_cloexec();
    puts("common helper tests passed");
    return 0;
}
