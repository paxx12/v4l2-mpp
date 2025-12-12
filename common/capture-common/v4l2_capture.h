#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "log.h"

#define V4L2_BUFFERS 4
#define V4L2_MAX_PLANES 4

typedef struct {
    void *start[V4L2_MAX_PLANES];
    size_t length[V4L2_MAX_PLANES];
    unsigned int num_planes;
} v4l2_buffer_t;

typedef struct {
    int fd;
    v4l2_buffer_t *buffers;
    unsigned int n_buffers;
    unsigned int width;
    unsigned int height;
    unsigned int pixfmt;
    enum v4l2_buf_type buf_type;
    unsigned int num_planes;
} v4l2_capture_t;

static int v4l2_ioctl(int fd, int request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int v4l2_capture_open(v4l2_capture_t *ctx, const char *device, unsigned int width, unsigned int height, unsigned int pixfmt, unsigned int fps, unsigned int requested_planes)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    int use_mplane = 0;

    ctx->fd = open(device, O_RDWR | O_NONBLOCK);
    if (ctx->fd < 0) {
        log_perror("open video device");
        return -1;
    }

    if (v4l2_ioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        log_perror("VIDIOC_QUERYCAP");
        return -1;
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        use_mplane = 1;
        ctx->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        log_printf("Using multi-planar capture\n");
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        ctx->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        log_printf("Using single-planar capture\n");
    } else {
        log_errorf("Device does not support video capture\n");
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        log_errorf("Device does not support streaming\n");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = ctx->buf_type;

    if (use_mplane) {
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = pixfmt;
        fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
    } else {
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
    }

    if (v4l2_ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
        log_perror("VIDIOC_S_FMT");
        return -1;
    }

    if (use_mplane) {
        ctx->width = fmt.fmt.pix_mp.width;
        ctx->height = fmt.fmt.pix_mp.height;
        ctx->pixfmt = fmt.fmt.pix_mp.pixelformat;
        ctx->num_planes = (requested_planes > 0) ? requested_planes : fmt.fmt.pix_mp.num_planes;
    } else {
        ctx->width = fmt.fmt.pix.width;
        ctx->height = fmt.fmt.pix.height;
        ctx->pixfmt = fmt.fmt.pix.pixelformat;
        ctx->num_planes = (requested_planes > 0) ? requested_planes : 1;
    }

    log_printf("V4L2: %ux%u format=0x%08x planes=%u\n", ctx->width, ctx->height, ctx->pixfmt, ctx->num_planes);

    if (fps > 0) {
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = ctx->buf_type;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = fps;
        if (v4l2_ioctl(ctx->fd, VIDIOC_S_PARM, &parm) < 0) {
            log_perror("VIDIOC_S_PARM (fps)");
        } else {
            log_printf("V4L2: fps=%u/%u\n",
                   parm.parm.capture.timeperframe.denominator,
                   parm.parm.capture.timeperframe.numerator);
        }
    }

    memset(&req, 0, sizeof(req));
    req.count = V4L2_BUFFERS;
    req.type = ctx->buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (v4l2_ioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0) {
        log_perror("VIDIOC_REQBUFS");
        return -1;
    }

    ctx->buffers = calloc(req.count, sizeof(v4l2_buffer_t));
    ctx->n_buffers = req.count;

    for (unsigned int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[V4L2_MAX_PLANES];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type = ctx->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (use_mplane) {
            buf.m.planes = planes;
            buf.length = ctx->num_planes;
        }

        if (v4l2_ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            log_perror("VIDIOC_QUERYBUF");
            return -1;
        }

        if (use_mplane) {
            ctx->buffers[i].num_planes = ctx->num_planes;
            for (unsigned int p = 0; p < ctx->num_planes; p++) {
                ctx->buffers[i].length[p] = planes[p].length;
                ctx->buffers[i].start[p] = mmap(NULL, planes[p].length,
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED, ctx->fd,
                                                planes[p].m.mem_offset);
                if (ctx->buffers[i].start[p] == MAP_FAILED) {
                    log_perror("mmap");
                    return -1;
                }
            }
        } else {
            ctx->buffers[i].num_planes = 1;
            ctx->buffers[i].length[0] = buf.length;
            ctx->buffers[i].start[0] = mmap(NULL, buf.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED, ctx->fd,
                                            buf.m.offset);
            if (ctx->buffers[i].start[0] == MAP_FAILED) {
                log_perror("mmap");
                return -1;
            }
        }
    }

    return 0;
}

static int v4l2_capture_start(v4l2_capture_t *ctx)
{
    int use_mplane = (ctx->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

    for (unsigned int i = 0; i < ctx->n_buffers; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[V4L2_MAX_PLANES];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type = ctx->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (use_mplane) {
            buf.m.planes = planes;
            buf.length = ctx->num_planes;
        }

        if (v4l2_ioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0) {
            log_perror("VIDIOC_QBUF");
            return -1;
        }
    }

    if (v4l2_ioctl(ctx->fd, VIDIOC_STREAMON, &ctx->buf_type) < 0) {
        log_perror("VIDIOC_STREAMON");
        return -1;
    }

    return 0;
}

static int v4l2_capture_stop(v4l2_capture_t *ctx)
{
    v4l2_ioctl(ctx->fd, VIDIOC_STREAMOFF, &ctx->buf_type);
    return 0;
}

static int v4l2_capture_read_frame(v4l2_capture_t *ctx, struct v4l2_buffer *buf, struct v4l2_plane *planes)
{
    int use_mplane = (ctx->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

    memset(buf, 0, sizeof(*buf));
    buf->type = ctx->buf_type;
    buf->memory = V4L2_MEMORY_MMAP;

    if (use_mplane) {
        memset(planes, 0, sizeof(struct v4l2_plane) * V4L2_MAX_PLANES);
        buf->m.planes = planes;
        buf->length = ctx->num_planes;
    }

    if (v4l2_ioctl(ctx->fd, VIDIOC_DQBUF, buf) < 0) {
        if (errno == EAGAIN)
            return 0;
        log_perror("VIDIOC_DQBUF");
        return -1;
    }

    return 1;
}

static int v4l2_capture_release_frame(v4l2_capture_t *ctx, struct v4l2_buffer *buf)
{
    if (v4l2_ioctl(ctx->fd, VIDIOC_QBUF, buf) < 0) {
        log_perror("VIDIOC_QBUF");
        return -1;
    }
    return 0;
}

static void v4l2_capture_close(v4l2_capture_t *ctx)
{
    for (unsigned int i = 0; i < ctx->n_buffers; i++) {
        for (unsigned int p = 0; p < ctx->buffers[i].num_planes; p++) {
            munmap(ctx->buffers[i].start[p], ctx->buffers[i].length[p]);
        }
    }
    free(ctx->buffers);
    close(ctx->fd);
}

#endif
