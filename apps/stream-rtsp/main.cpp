#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <BasicUsageEnvironment.hh>
#include <liveMedia.hh>

#include "h264_frames.h"
#include "h264_stream.h"

using Buffer = std::vector<uint8_t>;
using BufferPtr = std::shared_ptr<Buffer>;

static int g_debug = 0;
static h264_stream_t g_h264_stream = H264_STREAM_INIT;
static std::set<class DynamicH264Stream *> g_streams;
static std::recursive_mutex g_streams_lock;
static std::atomic<int> g_watch_variable{1};
static std::atomic<int> g_dropped_frames{0};
static std::atomic<int> g_total_frames{0};

class DynamicH264Stream : public FramedSource
{
public:
  DynamicH264Stream(UsageEnvironment& env)
    : FramedSource(env)
  {
    isRunning = false;
    currentOffset = 0;
  }

  void sendNewFrame(const BufferPtr &buf)
  {
    std::unique_lock lk(lock);

    if (!isRunning) {
      return;
    }

    if (currentBuffer) {
        printf("Dropping frame, previous frame not sent yet\n");
        g_dropped_frames++;
        return;
    }

    setNewBuffer(buf);
  }

  void doGetNextFrame()
  {
    if (!isRunning) {
        std::unique_lock lk(g_streams_lock);
        g_streams.insert(this);
        isRunning = true;
    }

    std::unique_lock lk(lock);

    if (!currentBuffer) {
        return;
    }

    if (!isCurrentlyAwaitingData()) {
        return;
    }

    size_t remaining = currentBuffer->size() - currentOffset;
    fFrameSize = std::min<size_t>(fMaxSize, remaining);
    fNumTruncatedBytes = remaining - fFrameSize;

    memcpy(fTo, currentBuffer->data() + currentOffset, fFrameSize);

    if (g_debug) {
        printf("Sending frame at offset %u of size %u (truncated %u)\n", currentOffset, fFrameSize, fNumTruncatedBytes);
    }
    currentOffset += fFrameSize;

    if (currentBuffer->size() == currentOffset) {
        setNewBuffer(BufferPtr());
    }

    lk.unlock();
    afterGetting(this);
  }

private:
  void doStopGettingFrames()
  {
    if (isRunning) {
        std::unique_lock lk(g_streams_lock);
        g_streams.erase(this);
        isRunning = false;
    }

    std::unique_lock lk(lock);
    setNewBuffer(BufferPtr());
  }

  void setNewBuffer(const BufferPtr &buf)
  {
    currentBuffer = buf;
    currentOffset = 0;
  }

  bool isRunning;
  std::mutex lock;
  BufferPtr currentBuffer;
  unsigned currentOffset;
};

class H264LiveServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static H264LiveServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
        return new H264LiveServerMediaSubsession(env, reuseFirstSource);
    }

protected:
    H264LiveServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
        : OnDemandServerMediaSubsession(env, reuseFirstSource) {}

    virtual ~H264LiveServerMediaSubsession() {}

    virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
        (void)clientSessionId;
        estBitrate = 2000;
        auto framedSource = new DynamicH264Stream(envir());
        return H264VideoStreamFramer::createNew(envir(), framedSource);
    }

    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) {
        (void)inputSource;
        return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
    }
};

static void store_frame(const uint8_t *data, size_t size) {
    std::unique_lock lk(g_streams_lock);

    if (g_streams.empty()) {
        return;
    }

    g_total_frames++;

    BufferPtr buf = std::make_shared<Buffer>(data, data + size);
    for (auto *stream : g_streams) {
        stream->sendNewFrame(buf);
    }
}

static void h264_read_handler(void*, int) {
    h264_stream_process(&g_h264_stream, store_frame);
}

static void h264_stream_open_or_close(BasicTaskScheduler0* scheduler, const char *h264_sock) {
    std::unique_lock lk(g_streams_lock);

    if (g_streams.size() > 0) {
        if (h264_stream_open(&g_h264_stream, h264_sock)) {
            scheduler->setBackgroundHandling(g_h264_stream.fd, SOCKET_READABLE, h264_read_handler, nullptr);
            if (g_debug) {
                std::cerr << "H264 socket opened for streaming" << std::endl;
            }
        }
    } else if (g_h264_stream.fd >= 0) {
        scheduler->disableBackgroundHandling(g_h264_stream.fd);
        h264_stream_close(&g_h264_stream);
    }
}

static void rtsp_flush() {
    std::unique_lock lk(g_streams_lock);

    for (auto *stream : g_streams) {
        stream->doGetNextFrame();
    }
}

static void close_old_clients(size_t max_clients) {
    std::unique_lock lk(g_streams_lock);

    while (g_streams.size() > max_clients) {
        auto it = g_streams.begin();
        DynamicH264Stream* stream = *it;
        g_streams.erase(it);
        stream->handleClosure();
        std::cerr << "Closed old client, current clients: " << g_streams.size() << std::endl;
    }
}

static void signal_handler(int) {
    g_watch_variable = 0;
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --h264-sock <path>     H264 stream input socket\n"
              << "  --rtsp-port <port>     RTSP server port (default: 8554)\n"
              << "  --max-clients <n>      Max concurrent clients (default: 4)\n"
              << "  --debug                Enable debug output\n"
              << "  --help                 Show this help\n";
}

int main(int argc, char* argv[]) {
    printf("stream-rtsp - built %s (%s)\n", __DATE__, __FILE__);

    std::string h264_sock;
    int rtsp_port = 8554;
    int max_clients = 0;
    int buffer_size = 300000;

    enum {
        OPT_H264_SOCK = 1,
        OPT_RTSP_PORT,
        OPT_MAX_CLIENTS,
        OPT_BUFFER_SIZE,
        OPT_DEBUG,
        OPT_HELP,
    };

    static struct option long_options[] = {
        {"h264-sock",    required_argument, 0, OPT_H264_SOCK},
        {"rtsp-port",    required_argument, 0, OPT_RTSP_PORT},
        {"max-clients",  required_argument, 0, OPT_MAX_CLIENTS},
        {"buffer-size",  required_argument, 0, OPT_BUFFER_SIZE},
        {"debug",        no_argument,       0, OPT_DEBUG},
        {"help",         no_argument,       0, OPT_HELP},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
        switch (opt) {
        case OPT_H264_SOCK:
            h264_sock = optarg;
            break;
        case OPT_RTSP_PORT:
            rtsp_port = std::atoi(optarg);
            break;
        case OPT_MAX_CLIENTS:
            max_clients = std::atoi(optarg);
            break;
        case OPT_BUFFER_SIZE:
            buffer_size = std::atoi(optarg);
            break;
        case OPT_DEBUG:
            g_debug = 1;
            break;
        case OPT_HELP:
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (h264_sock.empty()) {
        std::cerr << "Error: --h264-sock is required\n";
        print_usage(argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    std::cout << "H264 socket: " << h264_sock << std::endl;
    std::cout << "RTSP port: " << rtsp_port << std::endl;
    std::cout << "Max clients: " << max_clients << std::endl;

    BasicTaskScheduler0* scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

    OutPacketBuffer::maxSize = buffer_size;

    UserAuthenticationDatabase* authDB = nullptr;

    RTSPServer* rtspServer = RTSPServer::createNew(*env, rtsp_port, authDB);
    if (rtspServer == nullptr) {
        *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
        return 1;
    }

    ServerMediaSession* sms = ServerMediaSession::createNew(*env, "stream", "H264 Live Stream", "H264 video stream");
    sms->addSubsession(H264LiveServerMediaSubsession::createNew(*env, True));
    rtspServer->addServerMediaSession(sms);

    std::cout << "RTSP server started" << std::endl;
    std::cout << "Access the stream at the following URL:\n";
    std::cout << "  rtsp://<IP_ADDRESS>:" << rtsp_port << "/stream";
    std::cout << std::endl;

    char* url = rtspServer->rtspURL(sms);
    std::cout << "RTSP URL: " << url << std::endl;
    delete[] url;

    struct timespec stats_time;
    clock_gettime(CLOCK_MONOTONIC, &stats_time);

    while (g_watch_variable) {
        scheduler->SingleStep(0);
        h264_stream_open_or_close(scheduler, h264_sock.c_str());
        close_old_clients(max_clients);
        rtsp_flush();

        if (g_debug) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ns = (now.tv_sec - stats_time.tv_sec) * 1000000000L +
                            (now.tv_nsec - stats_time.tv_nsec);
            if (elapsed_ns >= 1000000000L) {
                printf("Streams: %ld. Frames: %d. Dropped: %d\n",
                    g_streams.size(),
                    g_total_frames.load(),
                    g_dropped_frames.load()
                );
                stats_time = now;
            }
        }
    }

    h264_stream_close(&g_h264_stream);
    Medium::close(rtspServer);
    env->reclaim();
    delete scheduler;

    std::cout << "Shutting down..." << std::endl;
    return 0;
}
