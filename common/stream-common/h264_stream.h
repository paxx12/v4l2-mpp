#pragma once

static constexpr int MIN_FRAME_SIZE = 64 * 1024;
static constexpr int MAX_FRAME_SIZE = 2 * 1024 * 1024;

typedef struct {
    int fd;
    std::vector<uint8_t> buf;
    size_t size;
} h264_stream_t;

#define H264_STREAM_INIT {.fd = -1, .buf = std::vector<uint8_t>(), .size = 0}

static bool h264_stream_open(h264_stream_t *stream, const char *path) {
    if (stream->fd >= 0) {
        return false;
    }

    stream->size = 0;
    stream->buf.resize(0);

    stream->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (stream->fd < 0) {
        std::perror("socket");
        return false;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(stream->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("connect");
        close(stream->fd);
        stream->fd = -1;
        return false;
    }

    std::cerr << "Connected to H264 socket" << std::endl;
    return true;
}

static bool h264_stream_close(h264_stream_t *stream) {
    if (stream->fd < 0) {
        return false;
    }

    close(stream->fd);
    stream->fd = -1;
    stream->buf.clear();
    stream->size = 0;
    std::cerr << "Disconnected from H264 socket" << std::endl;
    return true;
}

static ssize_t h264_stream_process(h264_stream_t *stream, void (*store_frame)(const uint8_t*, size_t)) {
    if (stream->fd < 0) {
        return -1;
    }

    if (stream->size >= MAX_FRAME_SIZE) {
        std::cerr << "Buffer overflow, resetting buffer" << std::endl;
        stream->size = 0;
    } else if (stream->size + MIN_FRAME_SIZE / 2 > stream->buf.size()) {
        stream->buf.resize(stream->size + MIN_FRAME_SIZE);
    }

    ssize_t n = read(stream->fd, stream->buf.data() + stream->size, stream->buf.size() - stream->size);
    if (n < 0) {
        if (n == EAGAIN || n == EWOULDBLOCK) {
            return -1;
        }
        std::perror("read");
        close(stream->fd);
        stream->fd = -1;
        return -1;
    }

    stream->size += n;

    const uint8_t* processed = h264_process_frames(stream->buf.data(), stream->buf.data() + stream->size, store_frame);
    if (!processed) {
      return 0;
    }

    size_t size = processed - stream->buf.data();
    size_t remaining = stream->size - size;
    if (remaining > 0) {
        std::memmove(stream->buf.data(), processed, remaining);
    }
    stream->size = remaining;

    if (stream->buf.size() > MIN_FRAME_SIZE) {
        stream->buf.resize(remaining + MIN_FRAME_SIZE);
    }

    return size;
}
