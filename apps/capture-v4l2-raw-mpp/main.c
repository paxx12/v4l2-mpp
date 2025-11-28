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

#include "v4l2_capture.h"
#include "sock_ctx.h"
#include "callback_chain.h"
#include "mpp_enc_ctx.h"

const char NAL_AUD_FRAME[] = {0x00, 0x00, 0x00, 0x01, 0x09, 0xf0};

int debug = 0;

static MppFrameFormat v4l2_to_mpp_format(unsigned int pixfmt)
{
    switch (pixfmt) {
    case V4L2_PIX_FMT_YUYV:
        return MPP_FMT_YUV422_YUYV;
    case V4L2_PIX_FMT_UYVY:
        return MPP_FMT_YUV422_UYVY;
    case V4L2_PIX_FMT_NV12:
        return MPP_FMT_YUV420SP;
    case V4L2_PIX_FMT_NV21:
        return MPP_FMT_YUV420SP_VU;
    case V4L2_PIX_FMT_YUV420:
        return MPP_FMT_YUV420P;
    case V4L2_PIX_FMT_RGB24:
        return MPP_FMT_RGB888;
    case V4L2_PIX_FMT_BGR24:
        return MPP_FMT_BGR888;
    default:
        return MPP_FMT_YUV420SP;
    }
}

static unsigned int parse_v4l2_format(const char *fmt)
{
    if (!fmt) return V4L2_PIX_FMT_YUYV;
    if (strcasecmp(fmt, "yuyv") == 0) return V4L2_PIX_FMT_YUYV;
    if (strcasecmp(fmt, "uyvy") == 0) return V4L2_PIX_FMT_UYVY;
    if (strcasecmp(fmt, "nv12") == 0) return V4L2_PIX_FMT_NV12;
    if (strcasecmp(fmt, "nv21") == 0) return V4L2_PIX_FMT_NV21;
    if (strcasecmp(fmt, "yuv420") == 0) return V4L2_PIX_FMT_YUV420;
    if (strcasecmp(fmt, "rgb24") == 0) return V4L2_PIX_FMT_RGB24;
    if (strcasecmp(fmt, "bgr24") == 0) return V4L2_PIX_FMT_BGR24;
    return V4L2_PIX_FMT_YUYV;
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

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --device <path>         V4L2 device path (default: /dev/video0)\n");
    printf("  --width <width>         Video width (default: 1920)\n");
    printf("  --height <height>       Video height (default: 1080)\n");
    printf("  --format <format>       Raw video format: yuyv, uyvy, nv12, nv21, yuv420, rgb24, bgr24 (default: yuyv)\n");
    printf("  --output <path>         JPEG output path (optional)\n");
    printf("  --jpeg-quality <0-10>   JPEG quality (default: 80)\n");
    printf("  --jpeg-sock <path>      JPEG snapshot socket path, write once and close (optional)\n");
    printf("  --mjpeg-sock <path>     MJPEG stream output socket path (optional)\n");
    printf("  --h264-sock <path>      H264 stream output socket path (optional)\n");
    printf("  --h264-bitrate <kbps>   H264 bitrate in kbps (default: 2000)\n");
    printf("  --fps <fps>             Frames per second (default: 30)\n");
    printf("  --count <count>         Number of frames to capture, 0 for unlimited (default: 0)\n");
    printf("  --num-planes <n>        Number of capture planes (default: 1)\n");
    printf("  --idle <ms>             Idle sleep in ms when no readers (default: 1000)\n");
    printf("  --debug                 Enable debug output\n");
    printf("  --help                  Show this help\n");
}

int main(int argc, char *argv[])
{
    const char *device = "/dev/video0";
    const char *format = "yuyv";
    const char *jpeg_output = NULL;
    const char *jpeg_snapshot = NULL;
    const char *mjpeg_stream = NULL;
    const char *h264_stream = NULL;
    int width = 1920;
    int height = 1080;
    int quality = 80;
    int bitrate = 2000;
    int fps = 30;
    int count = 0;
    int num_planes = 1;
    int idle_ms = 1000;
    int opt;

    enum {
        OPT_DEVICE = 1,
        OPT_WIDTH,
        OPT_HEIGHT,
        OPT_FORMAT,
        OPT_OUTPUT,
        OPT_QUALITY,
        OPT_SNAPSHOT,
        OPT_MJPEG,
        OPT_H264,
        OPT_BITRATE,
        OPT_FPS,
        OPT_COUNT,
        OPT_NUM_PLANES,
        OPT_IDLE,
        OPT_DEBUG,
        OPT_HELP,
    };

    static struct option long_options[] = {
        {"device",        required_argument, 0, OPT_DEVICE},
        {"width",         required_argument, 0, OPT_WIDTH},
        {"height",        required_argument, 0, OPT_HEIGHT},
        {"format",        required_argument, 0, OPT_FORMAT},
        {"output",        required_argument, 0, OPT_OUTPUT},
        {"jpeg-quality",  required_argument, 0, OPT_QUALITY},
        {"jpeg-sock",     required_argument, 0, OPT_SNAPSHOT},
        {"mjpeg-sock",    required_argument, 0, OPT_MJPEG},
        {"h264-sock",     required_argument, 0, OPT_H264},
        {"h264-bitrate",  required_argument, 0, OPT_BITRATE},
        {"fps",           required_argument, 0, OPT_FPS},
        {"count",         required_argument, 0, OPT_COUNT},
        {"num-planes",    required_argument, 0, OPT_NUM_PLANES},
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
        case OPT_FORMAT:
            format = optarg;
            break;
        case OPT_OUTPUT:
            jpeg_output = optarg;
            break;
        case OPT_SNAPSHOT:
            jpeg_snapshot = optarg;
            break;
        case OPT_QUALITY:
            quality = atoi(optarg);
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
        case OPT_COUNT:
            count = atoi(optarg);
            break;
        case OPT_NUM_PLANES:
            num_planes = atoi(optarg);
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

    unsigned int pixfmt = parse_v4l2_format(format);
    v4l2_capture_t v4l2 = {0};
    mpp_enc_ctx_t mpp_jpeg = {0};
    mpp_enc_ctx_t mpp_h264 = {0};
    sock_ctx_t jpeg_sock = {0};
    sock_ctx_t mjpeg_sock = {0};
    sock_ctx_t h264_sock = {0};

    printf("Device: %s\n", device);
    printf("Resolution: %dx%d\n", width, height);
    printf("Format: %s\n", format);
    printf("JPEG output: %s\n", jpeg_output);
    if (jpeg_snapshot) printf("JPEG snapshot socket: %s\n", jpeg_snapshot);
    if (mjpeg_stream) printf("MJPEG stream socket: %s\n", mjpeg_stream);
    if (h264_stream) printf("H264 stream socket: %s\n", h264_stream);
    printf("FPS: %d\n", fps);
    printf("Frames: %d\n", count);

    if (v4l2_capture_open(&v4l2, device, width, height, pixfmt, fps, num_planes) < 0) {
        fprintf(stderr, "Failed to open V4L2 device\n");
        return 1;
    }

    MppFrameFormat mpp_fmt = v4l2_to_mpp_format(v4l2.pixfmt);

    if (mpp_jpeg_encoder_init(&mpp_jpeg, v4l2.width, v4l2.height, mpp_fmt, quality) < 0) {
        fprintf(stderr, "Failed to initialize JPEG encoder\n");
        goto error;
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
        if (mpp_h264_encoder_init(&mpp_h264, v4l2.width, v4l2.height, mpp_fmt, bitrate, fps) < 0) {
            fprintf(stderr, "Failed to initialize H264 encoder\n");
            goto error;
        }
        if (sock_open(&h264_sock, h264_stream) < 0) {
            fprintf(stderr, "Failed to open H264 socket\n");
            goto error;
        }
    }

    if (v4l2_capture_start(&v4l2) < 0) {
        fprintf(stderr, "Failed to start V4L2 streaming\n");
        goto error;
    }

    int frame_delay_us = 1000000 / fps;
    int frames_captured = 0;
    int continuous = (count == 0);

    struct timespec stats_time;
    struct timespec last_frame;
    clock_gettime(CLOCK_MONOTONIC, &stats_time);
    clock_gettime(CLOCK_MONOTONIC, &last_frame);
    int frames_this_second = 0;
    int frames_this_jpeg_captured = 0;
    int frames_this_h264_captured = 0;

    while (continuous || frames_captured < count) {
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
        struct v4l2_plane planes[V4L2_MAX_PLANES];
        int ret = v4l2_capture_read_frame(&v4l2, &buf, planes);
        if (ret < 0)
            break;
        if (ret == 0)
            continue;

        size_t bytesused = (v4l2.buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
            ? planes[0].bytesused : buf.bytesused;
        void *frame_data = v4l2.buffers[buf.index].start[0];

        sock_accept_clients(&jpeg_sock);
        sock_accept_clients(&mjpeg_sock);
        sock_accept_clients(&h264_sock);

        callback_chain_t jpeg_chain[] = {
            { write_output_rename_cb, (void*)jpeg_output, jpeg_output != NULL },
            { sock_write_cb, &jpeg_sock, jpeg_sock.num_clients > 0 },
            { sock_write_cb, &mjpeg_sock, mjpeg_sock.num_clients > 0 },
            { NULL, NULL, 0 }
        };

        frames_captured++;
        frames_this_second++;

        int encoded_any = 0;

        if (callback_chain_active(jpeg_chain)) {
            MppPacket packet = mpp_encode_frame(&mpp_jpeg, frame_data, bytesused, 0);
            if (packet) {
                callback_chain_write_cb(mpp_packet_get_pos(packet), mpp_packet_get_length(packet), (void *)jpeg_chain);
                mpp_packet_deinit(&packet);
            }
            frames_this_jpeg_captured++;
            encoded_any = 1;
        }

        if (h264_sock.num_clients > 0) {
            MppPacket packet = mpp_encode_frame(&mpp_h264, frame_data, bytesused, h264_sock.need_keyframe);
            if (packet) {
                sock_write_cb(mpp_packet_get_pos(packet), mpp_packet_get_length(packet), &h264_sock);
                sock_write_cb(NAL_AUD_FRAME, sizeof(NAL_AUD_FRAME), &h264_sock);
                mpp_packet_deinit(&packet);
            }
            h264_sock.need_keyframe = false;
            frames_this_h264_captured++;
            encoded_any = 1;
        }

        v4l2_capture_release_frame(&v4l2, &buf);

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

    v4l2_capture_stop(&v4l2);
    sock_close(&h264_sock);
    sock_close(&mjpeg_sock);
    sock_close(&jpeg_sock);
    mpp_encoder_close(&mpp_h264);
    mpp_encoder_close(&mpp_jpeg);
    v4l2_capture_close(&v4l2);

    printf("Captured %d frames\n", frames_captured);
    return 0;

error:
    sock_close(&h264_sock);
    sock_close(&mjpeg_sock);
    sock_close(&jpeg_sock);
    mpp_encoder_close(&mpp_h264);
    mpp_encoder_close(&mpp_jpeg);
    v4l2_capture_close(&v4l2);
    return 1;
}
