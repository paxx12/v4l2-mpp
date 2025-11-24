#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/videodev2.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>

#define V4L2_BUFFERS 4

const char NAL_AUD_FRAME[] = {0x00, 0x00, 0x00, 0x01, 0x09, 0xf0};

static int debug = 0;

typedef struct {
    void *start;
    size_t length;
} buffer_t;

typedef struct {
    int fd;
    buffer_t *buffers;
    unsigned int n_buffers;
    unsigned int width;
    unsigned int height;
    unsigned int pixfmt;
} v4l2_ctx_t;

typedef struct {
    MppCtx ctx;
    MppApi *mpi;
    MppBufferGroup frm_grp;
    MppBufferGroup pkt_grp;
    unsigned int width;
    unsigned int height;
} mpp_dec_ctx_t;

typedef struct {
    MppCtx ctx;
    MppApi *mpi;
    MppBufferGroup buf_grp;
    MppEncCfg cfg;
    unsigned int width;
    unsigned int height;
} mpp_enc_ctx_t;

static int xioctl(int fd, int request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int v4l2_open(v4l2_ctx_t *ctx, const char *device, unsigned int width, unsigned int height, unsigned int fps)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;

    ctx->fd = open(device, O_RDWR | O_NONBLOCK);
    if (ctx->fd < 0) {
        perror("open video device");
        return -1;
    }

    if (xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Device does not support video capture\n");
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming\n");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
        if (xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
            perror("VIDIOC_S_FMT");
            return -1;
        }
    }

    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    ctx->pixfmt = fmt.fmt.pix.pixelformat;

    printf("V4L2: %ux%u format=0x%08x\n", ctx->width, ctx->height, ctx->pixfmt);

    if (fps > 0) {
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = fps;
        if (xioctl(ctx->fd, VIDIOC_S_PARM, &parm) < 0) {
            perror("VIDIOC_S_PARM (fps)");
        } else {
            printf("V4L2: fps=%u/%u\n",
                   parm.parm.capture.timeperframe.denominator,
                   parm.parm.capture.timeperframe.numerator);
        }
    }

    memset(&req, 0, sizeof(req));
    req.count = V4L2_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    ctx->buffers = calloc(req.count, sizeof(buffer_t));
    ctx->n_buffers = req.count;

    for (unsigned int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        ctx->buffers[i].length = buf.length;
        ctx->buffers[i].start = mmap(NULL, buf.length,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, ctx->fd,
                                     buf.m.offset);
        if (ctx->buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
    }

    return 0;
}

static int v4l2_start(v4l2_ctx_t *ctx)
{
    for (unsigned int i = 0; i < ctx->n_buffers; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    return 0;
}

static int v4l2_stop(v4l2_ctx_t *ctx)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    return 0;
}

static int v4l2_read_frame(v4l2_ctx_t *ctx, struct v4l2_buffer *buf)
{
    memset(buf, 0, sizeof(*buf));
    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->fd, VIDIOC_DQBUF, buf) < 0) {
        if (errno == EAGAIN)
            return 0;
        perror("VIDIOC_DQBUF");
        return -1;
    }

    return 1;
}

static int v4l2_release_frame(v4l2_ctx_t *ctx, struct v4l2_buffer *buf)
{
    if (xioctl(ctx->fd, VIDIOC_QBUF, buf) < 0) {
        perror("VIDIOC_QBUF");
        return -1;
    }
    return 0;
}

static void v4l2_close(v4l2_ctx_t *ctx)
{
    for (unsigned int i = 0; i < ctx->n_buffers; i++) {
        munmap(ctx->buffers[i].start, ctx->buffers[i].length);
    }
    free(ctx->buffers);
    close(ctx->fd);
}

static int mpp_decoder_init(mpp_dec_ctx_t *ctx, unsigned int width, unsigned int height)
{
    MPP_RET ret;

    ctx->width = width;
    ctx->height = height;

    ret = mpp_create(&ctx->ctx, &ctx->mpi);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_create failed: %d\n", ret);
        return -1;
    }

    ret = mpp_init(ctx->ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_init decoder failed: %d\n", ret);
        return -1;
    }

    MppDecCfg cfg = NULL;
    mpp_dec_cfg_init(&cfg);
    mpp_dec_cfg_set_u32(cfg, "base:out_fmt", MPP_FMT_YUV420SP);
    ret = ctx->mpi->control(ctx->ctx, MPP_DEC_SET_CFG, cfg);
    mpp_dec_cfg_deinit(cfg);

    if (ret != MPP_OK) {
        fprintf(stderr, "MPP_DEC_SET_CFG failed: %d\n", ret);
        return -1;
    }

    ret = mpp_buffer_group_get_internal(&ctx->pkt_grp, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_buffer_group_get_internal pkt failed: %d\n", ret);
        return -1;
    }

    ret = mpp_buffer_group_get_internal(&ctx->frm_grp, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_buffer_group_get_internal frm failed: %d\n", ret);
        return -1;
    }

    return 0;
}

static MppFrame mpp_decode_jpeg(mpp_dec_ctx_t *ctx, void *data, size_t size)
{
    MPP_RET ret;
    MppTask task = NULL;
    MppBuffer pkt_buf = NULL;
    MppBuffer frm_buf = NULL;
    MppPacket packet = NULL;
    MppFrame frame = NULL;
    size_t frame_size;

    frame_size = ctx->width * ctx->height * 2;

    ret = mpp_buffer_get(ctx->pkt_grp, &pkt_buf, size);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_buffer_get pkt failed: %d\n", ret);
        return NULL;
    }

    memcpy(mpp_buffer_get_ptr(pkt_buf), data, size);

    ret = mpp_packet_init_with_buffer(&packet, pkt_buf);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_packet_init_with_buffer failed: %d\n", ret);
        mpp_buffer_put(pkt_buf);
        return NULL;
    }
    mpp_packet_set_length(packet, size);

    ret = mpp_buffer_get(ctx->frm_grp, &frm_buf, frame_size);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_buffer_get frm failed: %d\n", ret);
        mpp_packet_deinit(&packet);
        mpp_buffer_put(pkt_buf);
        return NULL;
    }

    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_frame_init failed: %d\n", ret);
        mpp_packet_deinit(&packet);
        mpp_buffer_put(pkt_buf);
        mpp_buffer_put(frm_buf);
        return NULL;
    }
    mpp_frame_set_buffer(frame, frm_buf);

    ret = ctx->mpi->poll(ctx->ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret != MPP_OK) {
        fprintf(stderr, "poll input failed: %d\n", ret);
        goto error;
    }

    ret = ctx->mpi->dequeue(ctx->ctx, MPP_PORT_INPUT, &task);
    if (ret != MPP_OK || !task) {
        fprintf(stderr, "dequeue input failed: %d\n", ret);
        goto error;
    }

    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);

    ret = ctx->mpi->enqueue(ctx->ctx, MPP_PORT_INPUT, task);
    if (ret != MPP_OK) {
        fprintf(stderr, "enqueue input failed: %d\n", ret);
        goto error;
    }

    ret = ctx->mpi->poll(ctx->ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret != MPP_OK) {
        fprintf(stderr, "poll output failed: %d\n", ret);
        goto error;
    }

    ret = ctx->mpi->dequeue(ctx->ctx, MPP_PORT_OUTPUT, &task);
    if (ret != MPP_OK || !task) {
        fprintf(stderr, "dequeue output failed: %d\n", ret);
        goto error;
    }

    MppFrame output_frame = NULL;
    mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &output_frame);

    ret = ctx->mpi->enqueue(ctx->ctx, MPP_PORT_OUTPUT, task);
    if (ret != MPP_OK) {
        fprintf(stderr, "enqueue output failed: %d\n", ret);
    }

    mpp_packet_deinit(&packet);
    mpp_buffer_put(pkt_buf);
    mpp_buffer_put(frm_buf);

    return output_frame;

error:
    if (frame) mpp_frame_deinit(&frame);
    if (packet) mpp_packet_deinit(&packet);
    if (pkt_buf) mpp_buffer_put(pkt_buf);
    if (frm_buf) mpp_buffer_put(frm_buf);
    return NULL;
}

static void mpp_decoder_close(mpp_dec_ctx_t *ctx)
{
    if (ctx->pkt_grp) {
        mpp_buffer_group_put(ctx->pkt_grp);
    }
    if (ctx->frm_grp) {
        mpp_buffer_group_put(ctx->frm_grp);
    }
    if (ctx->ctx) {
        ctx->mpi->reset(ctx->ctx);
        mpp_destroy(ctx->ctx);
    }
}

static int mpp_h264_encoder_init(mpp_enc_ctx_t *ctx, unsigned int width, unsigned int height, unsigned int bitrate, unsigned int fps)
{
    MPP_RET ret;

    ctx->width = width;
    ctx->height = height;

    ret = mpp_create(&ctx->ctx, &ctx->mpi);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_create failed: %d\n", ret);
        return -1;
    }

    ret = mpp_init(ctx->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_init failed: %d\n", ret);
        return -1;
    }

    ret = mpp_enc_cfg_init(&ctx->cfg);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_enc_cfg_init failed: %d\n", ret);
        return -1;
    }

    mpp_enc_cfg_set_s32(ctx->cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:hor_stride", width);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:ver_stride", height);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:format", MPP_FMT_YUV420SP);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_target", bitrate * 1000);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_max", bitrate * 1500);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_min", bitrate * 500);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:gop", fps * 2);
    mpp_enc_cfg_set_s32(ctx->cfg, "codec:type", MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:profile", 100);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:level", 41);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:cabac_idc", 0);

    ret = ctx->mpi->control(ctx->ctx, MPP_ENC_SET_CFG, ctx->cfg);
    if (ret != MPP_OK) {
        fprintf(stderr, "MPP_ENC_SET_CFG failed: %d\n", ret);
        return -1;
    }

    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    ret = ctx->mpi->control(ctx->ctx, MPP_ENC_SET_HEADER_MODE, &header_mode);
    if (ret != MPP_OK) {
        fprintf(stderr, "MPP_ENC_SET_HEADER_MODE failed: %d\n", ret);
    }

    ret = mpp_buffer_group_get_internal(&ctx->buf_grp, MPP_BUFFER_TYPE_DRM);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_buffer_group_get_internal failed: %d\n", ret);
        return -1;
    }

    return 0;
}

typedef void (*mpp_encode_cb)(const void *data, size_t size, void *arg);

static int mpp_encode_h264(mpp_enc_ctx_t *ctx, MppFrame frame, int force_idr, mpp_encode_cb cb, void *cb_arg)
{
    MPP_RET ret;
    MppPacket packet = NULL;
    MppMeta meta = NULL;

    if (force_idr) {
        meta = mpp_frame_get_meta(frame);
        if (meta) {
            mpp_meta_set_s32(meta, KEY_INPUT_IDR_REQ, 1);
            if (debug) printf("Requesting IDR frame via meta\n");
        }
    }

    ret = ctx->mpi->encode_put_frame(ctx->ctx, frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "encode_put_frame failed: %d\n", ret);
        return -1;
    }

    ret = ctx->mpi->encode_get_packet(ctx->ctx, &packet);
    if (ret != MPP_OK || !packet) {
        fprintf(stderr, "encode_get_packet failed: %d\n", ret);
        return -1;
    }

    cb(mpp_packet_get_pos(packet), mpp_packet_get_length(packet), cb_arg);

    mpp_packet_deinit(&packet);
    return 0;
}

static void mpp_encoder_close(mpp_enc_ctx_t *ctx)
{
    if (ctx->cfg) {
        mpp_enc_cfg_deinit(ctx->cfg);
    }
    if (ctx->buf_grp) {
        mpp_buffer_group_put(ctx->buf_grp);
    }
    if (ctx->ctx) {
        ctx->mpi->reset(ctx->ctx);
        mpp_destroy(ctx->ctx);
    }
}

static void write_output_rename_cb(const void *data, size_t size, void *arg)
{
    const char *output = arg;
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", output);
    FILE *fp = fopen(tmp_path, "wb");
    if (fp) {
        fwrite(data, 1, size, fp);
        fclose(fp);
        if (rename(tmp_path, output) < 0) {
            perror("rename");
        }
    } else {
        perror("fopen");
    }
}

typedef struct {
    void (*cb)(const void *data, size_t size, void *arg);
    void *arg;
    bool run;
} callback_chain_t;

static bool callback_chain_active(callback_chain_t *chain)
{
    while (chain->cb) {
        if (chain->run) {
            return true;
        }
        chain++;
    }
    return false;
}

static void write_callback_chain_cb(void *data, size_t size, void *arg)
{
    callback_chain_t *chain = arg;

    while (chain->cb) {
        if (chain->run) {
            chain->cb(data, size, chain->arg);
        }
        chain++;
    }
}

#define SOCK_MAX_CLIENTS 8

typedef struct {
    const char *path;
    int listen_fd;
    int client_fds[SOCK_MAX_CLIENTS];
    int num_clients;
    bool one_frame;
    bool need_keyframe;
} sock_ctx_t;

static int sock_open(sock_ctx_t *ctx, const char *path)
{
    ctx->path = path;
    ctx->listen_fd = -1;
    ctx->num_clients = 0;

    for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
        ctx->client_fds[i] = -1;
    }

    unlink(path);

    ctx->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ctx->listen_fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
        return -1;
    }

    chmod(path, 0777);

    if (listen(ctx->listen_fd, SOCK_MAX_CLIENTS) < 0) {
        perror("listen");
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
        if (ctx->client_fds[i] >= 0) {
            close(ctx->client_fds[i]);
            ctx->client_fds[i] = -1;
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
            perror("accept");
            break;
        }

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        int slot = -1;
        for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
            if (ctx->client_fds[i] < 0) {
                slot = i;
                break;
            }
        }

        if (slot >= 0) {
            ctx->client_fds[slot] = client_fd;
            ctx->num_clients++;
            ctx->need_keyframe = true;
            accepted = true;
            if (debug) printf("Socket %s: client connected (slot %d, total %d)\n", ctx->path, slot, ctx->num_clients);
        } else {
            close(client_fd);
            if (debug) printf("Socket %s: rejected client, max reached\n", ctx->path);
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

static ssize_t write_client_fd(int fd, const void *data, size_t size)
{
    const char *ptr = data;
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t written = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
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

static void write_sock_cb(const void *data, size_t size, void *arg)
{
    sock_ctx_t *ctx = arg;

    for (int i = 0; i < SOCK_MAX_CLIENTS; i++) {
        if (ctx->client_fds[i] < 0)
            continue;

        if (write_client_fd(ctx->client_fds[i], data, size) < 0) {
            printf("Socket %s: error writing to client %d, closing\n", ctx->path, i);
            close(ctx->client_fds[i]);
            ctx->client_fds[i] = -1;
            ctx->num_clients--;
            continue;
        }

        if (ctx->one_frame) {
            printf("Socket %s: closing client %d after one frame\n", ctx->path, i);
            close(ctx->client_fds[i]);
            ctx->client_fds[i] = -1;
            ctx->num_clients--;
        }
    }

    if (debug && ctx->num_clients > 0) {
        printf("Wrote %zu bytes to socket %s (%d clients)\n", size, ctx->path, ctx->num_clients);
    }
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --device <path>         V4L2 device path (default: /dev/video0)\n");
    printf("  --width <width>         Video width (default: 1920)\n");
    printf("  --height <height>       Video height (default: 1080)\n");
    printf("  --output <path>         JPEG output path (optional)\n");
    printf("  --jpeg-sock <path>      JPEG snapshot socket path, write once and close (optional)\n");
    printf("  --mjpeg-sock <path>     MJPEG stream output socket path (optional)\n");
    printf("  --h264-sock <path>      H264 stream output socket path (optional)\n");
    printf("  --h264-bitrate <kbps>   H264 bitrate in kbps (default: 2000)\n");
    printf("  --fps <fps>             Frames per second (default: 30)\n");
    printf("  --idle <ms>             Idle sleep in ms when no readers (default: 1000)\n");
    printf("  --debug                 Enable debug output\n");
    printf("  --help                  Show this help\n");
}

int main(int argc, char *argv[])
{
    const char *device = "/dev/video0";
    const char *jpeg_output = NULL;
    const char *jpeg_snapshot = NULL;
    const char *mjpeg_stream = NULL;
    const char *h264_stream = NULL;
    int width = 1920;
    int height = 1080;
    int bitrate = 2000;
    int fps = 30;
    int idle_ms = 1000;
    int opt;

    enum {
        OPT_DEVICE = 1,
        OPT_WIDTH,
        OPT_HEIGHT,
        OPT_OUTPUT,
        OPT_SNAPSHOT,
        OPT_MJPEG,
        OPT_H264,
        OPT_BITRATE,
        OPT_FPS,
        OPT_IDLE,
        OPT_DEBUG,
        OPT_HELP,
    };

    static struct option long_options[] = {
        {"device",        required_argument, 0, OPT_DEVICE},
        {"width",         required_argument, 0, OPT_WIDTH},
        {"height",        required_argument, 0, OPT_HEIGHT},
        {"output",        required_argument, 0, OPT_OUTPUT},
        {"jpeg-sock",     required_argument, 0, OPT_SNAPSHOT},
        {"mjpeg-sock",    required_argument, 0, OPT_MJPEG},
        {"h264-sock",     required_argument, 0, OPT_H264},
        {"h264-bitrate",  required_argument, 0, OPT_BITRATE},
        {"fps",           required_argument, 0, OPT_FPS},
        {"idle",          required_argument, 0, OPT_IDLE},
        {"debug",         no_argument,       0, OPT_DEBUG},
        {"help",          no_argument,       0, OPT_HELP},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
        case OPT_DEVICE:
            device = optarg;
            break;
        case OPT_WIDTH:
            width = atoi(optarg);
            break;
        case OPT_HEIGHT:
            height = atoi(optarg);
            break;
        case OPT_OUTPUT:
            jpeg_output = optarg;
            break;
        case OPT_SNAPSHOT:
            jpeg_snapshot = optarg;
            break;
        case OPT_MJPEG:
            mjpeg_stream = optarg;
            break;
        case OPT_H264:
            h264_stream = optarg;
            break;
        case OPT_BITRATE:
            bitrate = atoi(optarg);
            break;
        case OPT_FPS:
            fps = atoi(optarg);
            break;
        case OPT_IDLE:
            idle_ms = atoi(optarg);
            break;
        case OPT_DEBUG:
            debug = 1;
            break;
        case OPT_HELP:
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    v4l2_ctx_t v4l2 = {0};
    mpp_dec_ctx_t mpp_dec = {0};
    mpp_enc_ctx_t mpp_enc = {0};
    sock_ctx_t jpeg_sock = {0};
    sock_ctx_t mjpeg_sock = {0};
    sock_ctx_t h264_sock = {0};

    printf("Device: %s\n", device);
    printf("Resolution: %dx%d\n", width, height);
    printf("JPEG output: %s\n", jpeg_output);
    if (jpeg_snapshot) printf("JPEG snapshot socket: %s\n", jpeg_snapshot);
    if (mjpeg_stream) printf("MJPEG stream socket: %s\n", mjpeg_stream);
    if (h264_stream) printf("H264 stream socket: %s\n", h264_stream);
    printf("FPS: %d\n", fps);

    if (v4l2_open(&v4l2, device, width, height, fps) < 0) {
        fprintf(stderr, "Failed to open V4L2 device\n");
        return 1;
    }

    if (jpeg_snapshot && sock_open(&jpeg_sock, jpeg_snapshot) < 0) {
        fprintf(stderr, "Failed to open JPEG snapshot socket\n");
        goto error;
    }
    jpeg_sock.one_frame = true;

    if (mjpeg_stream && sock_open(&mjpeg_sock, mjpeg_stream) < 0) {
        fprintf(stderr, "Failed to open MJPEG socket\n");
        goto error;
    }

    if (h264_stream) {
        if (mpp_decoder_init(&mpp_dec, v4l2.width, v4l2.height) < 0) {
            fprintf(stderr, "Failed to initialize JPEG decoder\n");
            goto error;
        }

        if (mpp_h264_encoder_init(&mpp_enc, v4l2.width, v4l2.height, bitrate, fps) < 0) {
            fprintf(stderr, "Failed to initialize H264 encoder\n");
            goto error;
        }

        if (sock_open(&h264_sock, h264_stream) < 0) {
            fprintf(stderr, "Failed to open H264 socket\n");
            goto error;
        }
    }

    if (v4l2_start(&v4l2) < 0) {
        fprintf(stderr, "Failed to start V4L2 streaming\n");
        goto error;
    }

    int frame_delay_us = 1000000 / fps;
    int frames_captured = 0;

    struct timespec stats_time;
    struct timespec last_frame;
    clock_gettime(CLOCK_MONOTONIC, &stats_time);
    clock_gettime(CLOCK_MONOTONIC, &last_frame);
    int frames_this_second = 0;
    int frames_this_jpeg_captured = 0;
    int frames_this_h264_captured = 0;

    while (1) {
        fd_set fds;
        struct timeval tv;

        FD_ZERO(&fds);
        FD_SET(v4l2.fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(v4l2.fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }
        if (r == 0) {
            fprintf(stderr, "select timeout\n");
            break;
        }

        struct v4l2_buffer buf;
        int ret = v4l2_read_frame(&v4l2, &buf);
        if (ret < 0)
            break;
        if (ret == 0)
            continue;

        void *frame_data = v4l2.buffers[buf.index].start;
        size_t bytesused = buf.bytesused;

        sock_accept_clients(&jpeg_sock);
        sock_accept_clients(&mjpeg_sock);
        sock_accept_clients(&h264_sock);

        callback_chain_t jpeg_chain[] = {
            { write_output_rename_cb, (void*)jpeg_output, jpeg_output != NULL },
            { write_sock_cb, &jpeg_sock, jpeg_sock.num_clients > 0 },
            { write_sock_cb, &mjpeg_sock, mjpeg_sock.num_clients > 0 },
            { NULL, NULL, 0 }
        };

        frames_captured++;
        frames_this_second++;

        int encoded_any = 0;

        if (callback_chain_active(jpeg_chain)) {
            write_callback_chain_cb(frame_data, bytesused, (void *)jpeg_chain);
            frames_this_jpeg_captured++;
            encoded_any = 1;
        }

        if (h264_sock.num_clients > 0) {
            MppFrame decoded = mpp_decode_jpeg(&mpp_dec, frame_data, bytesused);
            if (decoded) {
                if (mpp_encode_h264(&mpp_enc, decoded, h264_sock.need_keyframe, write_sock_cb, &h264_sock) == 0) {
                    write_sock_cb(NAL_AUD_FRAME, sizeof(NAL_AUD_FRAME), &h264_sock);
                }
                h264_sock.need_keyframe = false;
                frames_this_h264_captured++;
                encoded_any = 1;
                mpp_frame_deinit(&decoded);
            }
        }

        v4l2_release_frame(&v4l2, &buf);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ns = (now.tv_sec - stats_time.tv_sec) * 1000000000L +
                          (now.tv_nsec - stats_time.tv_nsec);
        if (elapsed_ns >= 1000000000L) {
            printf("FPS: %d (JPEG: %d, H264: %d) (total: %d). JPEG: %d, MJPEG: %d, H264: %d\n",
                   frames_this_second, frames_this_jpeg_captured, frames_this_h264_captured,
                   frames_captured,
                   jpeg_sock.num_clients,
                   mjpeg_sock.num_clients,
                   h264_sock.num_clients
            );
            frames_this_second = 0;
            frames_this_jpeg_captured = 0;
            frames_this_h264_captured = 0;
            stats_time = now;
        }

        long frame_elapsed_ns = (now.tv_sec - last_frame.tv_sec) * 1000000000L +
                                (now.tv_nsec - last_frame.tv_nsec);
        if (frame_elapsed_ns < frame_delay_us * 1000L) {
            usleep(frame_delay_us - frame_elapsed_ns / 1000L);
        }
        last_frame = now;

        if (!encoded_any && idle_ms > 0) {
            sock_ctx_t *socks[] = { &jpeg_sock, &mjpeg_sock, &h264_sock, NULL };
            sock_wait_fds(socks, idle_ms);
        }
    }

    v4l2_stop(&v4l2);
    sock_close(&h264_sock);
    sock_close(&mjpeg_sock);
    sock_close(&jpeg_sock);
    mpp_encoder_close(&mpp_enc);
    mpp_decoder_close(&mpp_dec);
    v4l2_close(&v4l2);

    printf("Captured %d frames\n", frames_captured);
    return 0;

error:
    sock_close(&h264_sock);
    sock_close(&mjpeg_sock);
    sock_close(&jpeg_sock);
    mpp_encoder_close(&mpp_enc);
    mpp_decoder_close(&mpp_dec);
    v4l2_close(&v4l2);
    return 1;
}
