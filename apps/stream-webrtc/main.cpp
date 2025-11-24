#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <list>
#include <vector>
#include <cstring>
#include <chrono>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static std::atomic<bool> g_running{true};
static int g_debug = 0;

static constexpr int PING_INTERVAL_MS = 1000;
static constexpr int CONNECT_TIMEOUT_MS = 30000;
static constexpr int PONG_TIMEOUT_MS = 30000;
static constexpr int MIN_FRAME_SIZE = 64 * 1024;
static constexpr int MAX_FRAME_SIZE = 2 * 1024 * 1024;

struct Client {
    std::string id;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video_track;
    std::shared_ptr<rtc::DataChannel> data_channel;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtp_config;
    std::shared_ptr<rtc::RtcpSrReporter> sr_reporter;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_ping;
    std::chrono::steady_clock::time_point last_pong;
    std::vector<std::string> pending_candidates;
    bool answer_received = false;
    int timeout_s = 0;
};

static std::mutex g_clients_mutex;
static std::list<std::shared_ptr<Client>> g_clients;
static std::atomic<uint64_t> g_client_counter{0};
static std::string g_h264_sock;
static std::vector<std::string> g_ice_servers;
static int g_max_clients = 4;

static std::shared_ptr<Client> find_client(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (auto& c : g_clients) {
        if (c->id == id) return c;
    }
    return nullptr;
}

static const uint8_t* find_nal(const uint8_t* data, size_t size) {
    for (size_t i = 0; i + 3 < size; i++) {
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1 && size - i > 4) {
            return data + i;
        }
    }
    return nullptr;
}

static bool is_new_frame_start(const uint8_t* nal, size_t nal_size) {
    if (nal_size < 5) return false;
    uint8_t nal_type = nal[4] & 0x1f;
    if (nal_type == 1 || nal_type == 5) {
        if (nal_size < 6) return true;
        uint8_t first_byte = nal[5];
        return (first_byte & 0x80) != 0;
    }
    return false;
}

static bool is_nal_aud_frame(const uint8_t* nal, size_t nal_size) {
    if (nal_size < 5) return false;
    uint8_t nal_type = nal[4] & 0x1f;
    return nal_type == 9 && nal_size == 6 && (nal[5] & 0x80) != 0;
}

static bool has_clients() {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    for (auto& client : g_clients) {
        if (client->video_track && client->video_track->isOpen()) {
            return true;
        }
    }

    return false;
}

static void send_frame(const uint8_t *data, size_t size) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    auto now = std::chrono::steady_clock::now();

    for (auto& client : g_clients) {
        if (!client->video_track || !client->video_track->isOpen()) {
            continue;
        }

        try {
            auto elapsed = std::chrono::duration<double>(now - client->start_time).count();
            client->sr_reporter->rtpConfig->timestamp =
                client->sr_reporter->rtpConfig->startTimestamp +
                client->sr_reporter->rtpConfig->secondsToTimestamp(elapsed);

            rtc::binary frame(
                reinterpret_cast<const std::byte*>(data),
                reinterpret_cast<const std::byte*>(data + size));
            client->video_track->send(frame);
        } catch (...) {}
    }
}

static const uint8_t *process_frames(const uint8_t *data, const uint8_t *end) {
    while ((end - data) >= 8) {
        const uint8_t* start = find_nal(data, end - data);
        if (!start) {
            if (end - data > 4) {
                // Leave only 4 bytes for next check
                return end - 4;
            }
            return NULL;
        }

        const uint8_t* next = start;
        bool found_slice = false;

        while ((next = find_nal(next + 4, end - next - 4)) != NULL) {
            if (is_nal_aud_frame(next, end - next)) {
                break;
            } else if (is_new_frame_start(next, end - next)) {
                if (found_slice)
                    break;
                found_slice = true;
            }
        }
        if (!next) {
            return NULL;
        }

        if (g_debug) {
            printf("Found AU of size %zu bytes\n", next - start);
        }

        send_frame(start, next - start);
        data = next;
    }

    return data;
}

static void cleanup_clients() {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    g_clients.remove_if([](const std::shared_ptr<Client>& c) {
        if (!c->pc || c->pc->state() == rtc::PeerConnection::State::Closed ||
            c->pc->state() == rtc::PeerConnection::State::Failed) {
            std::cerr << "Removed client " << c->id << std::endl;
            return true;
        }
        return false;
    });
}

static void ping_clients() {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    auto now = std::chrono::steady_clock::now();

    for (auto& c : g_clients) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - c->start_time).count();

        if (c->timeout_s > 0 && elapsed >= c->timeout_s) {
            std::cerr << "Client " << c->id << " session timeout" << std::endl;
            c->pc->close();
            continue;
        }

        if (!c->data_channel || !c->data_channel->isOpen()) {
            if (elapsed * 1000 >= CONNECT_TIMEOUT_MS) {
                std::cerr << "Client " << c->id << " connection timeout" << std::endl;
                c->pc->close();
            }
            continue;
        }

        auto since_pong = std::chrono::duration_cast<std::chrono::milliseconds>(now - c->last_pong).count();
        if (since_pong >= PONG_TIMEOUT_MS) {
            std::cerr << "Client " << c->id << " pong timeout" << std::endl;
            c->pc->close();
            continue;
        }

        auto since_ping = std::chrono::duration_cast<std::chrono::milliseconds>(now - c->last_ping).count();
        if (since_ping >= PING_INTERVAL_MS) {
            try {
                c->data_channel->send("ping");
                c->last_ping = now;
            } catch (...) {}
        }
    }
}

static size_t client_count() {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    return g_clients.size();
}

static std::shared_ptr<Client> create_client() {
    auto client = std::make_shared<Client>();
    client->id = std::to_string(++g_client_counter);
    client->start_time = std::chrono::steady_clock::now();
    client->last_pong = client->start_time;

    rtc::Configuration config;
    for (const auto& server : g_ice_servers) {
        config.iceServers.emplace_back(server);
    }

    client->pc = std::make_shared<rtc::PeerConnection>(config);

    std::weak_ptr<Client> weak_client = client;
    client->pc->onStateChange([weak_client](rtc::PeerConnection::State state) {
        if (auto c = weak_client.lock()) {
            std::cerr << "Client " << c->id << " state: " << static_cast<int>(state) << std::endl;
        }
        if (state == rtc::PeerConnection::State::Connected) {

        }
    });

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(1, "video-stream");

    client->video_track = client->pc->addTrack(media);

    client->rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
        1, "video-stream", 96, rtc::H264RtpPacketizer::ClockRate);

    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::LongStartSequence, client->rtp_config);

    client->sr_reporter = std::make_shared<rtc::RtcpSrReporter>(client->rtp_config);
    packetizer->addToChain(client->sr_reporter);

    auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);

    client->video_track->setMediaHandler(packetizer);

    client->data_channel = client->pc->createDataChannel("keepalive");
    client->data_channel->onMessage([weak_client](auto) {
        if (auto c = weak_client.lock()) {
            c->last_pong = std::chrono::steady_clock::now();
        }
    });

    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_clients.push_back(client);
    }

    return client;
}

static json handle_request(const json& request) {
    std::string type = request.value("type", "");

    if (type == "request") {
        if (client_count() >= (size_t)g_max_clients) {
            return {{"error", "max clients reached"}};
        }

        auto client = create_client();
        if (request.contains("timeout_s") && request["timeout_s"].is_number()) {
            client->timeout_s = request["timeout_s"].get<int>();
        }
        client->pc->setLocalDescription();

        auto desc = client->pc->localDescription();
        if (!desc) {
            return {{"error", "failed to create offer"}};
        }

        return {{"type", "offer"}, {"id", client->id}, {"sdp", std::string(*desc)}};
    }

    if (type == "answer") {
        std::string id = request.value("id", "");
        std::string sdp = request.value("sdp", "");

        if (id.empty() || sdp.empty()) {
            return {{"error", "missing id or sdp"}};
        }

        auto client = find_client(id);
        if (!client) {
            return {{"error", "client not found"}};
        }

        client->pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
        client->answer_received = true;

        for (const auto& cand : client->pending_candidates) {
            client->pc->addRemoteCandidate(rtc::Candidate(cand));
        }
        client->pending_candidates.clear();

        return {{"type", "ok"}};
    }

    if (type == "offer") {
        std::string sdp = request.value("sdp", "");
        if (sdp.empty()) {
            return {{"error", "missing sdp"}};
        }

        cleanup_clients();

        if (client_count() >= (size_t)g_max_clients) {
            return {{"error", "max clients reached"}};
        }

        auto client = create_client();
        client->pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
        client->answer_received = true;

        auto desc = client->pc->localDescription();
        if (!desc) {
            return {{"error", "failed to create answer"}};
        }

        return {{"type", "answer"}, {"id", client->id}, {"sdp", std::string(*desc)}};
    }

    if (type == "remote_candidate") {
        std::string id = request.value("id", "");

        if (id.empty()) {
            return {{"error", "missing id"}};
        }

        auto client = find_client(id);
        if (!client) {
            return {{"error", "client not found"}};
        }

        auto add_candidate = [&](const std::string& cand) {
            if (cand.empty()) return;
            if (client->answer_received) {
                client->pc->addRemoteCandidate(rtc::Candidate(cand));
            } else {
                client->pending_candidates.push_back(cand);
            }
        };

        if (request.contains("candidates") && request["candidates"].is_array()) {
            for (const auto& c : request["candidates"]) {
                if (c.is_string()) {
                    add_candidate(c.get<std::string>());
                } else if (c.is_object() && c.contains("candidate")) {
                    add_candidate(c["candidate"].get<std::string>());
                }
            }
        } else if (request.contains("candidate")) {
            add_candidate(request.value("candidate", ""));
        }

        return {{"type", "ok"}};
    }

    return {{"error", "unknown type"}};
}

static void handle_connection(int fd) {
    std::string line;
    char buf[65536];

    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        line += buf;
        if (line.find('\n') != std::string::npos) break;
    }

    if (line.empty()) {
        close(fd);
        return;
    }

    if (line.back() == '\n') {
        line.pop_back();
    }

    std::string response;
    try {
        json request = json::parse(line);
        json result = handle_request(request);
        response = result.dump() + "\n";
    } catch (const std::exception& e) {
        response = json{{"error", e.what()}}.dump() + "\n";
    }

    write(fd, response.c_str(), response.size());
    close(fd);
}

static void open_h264(int &fd, std::vector<uint8_t>& buf, size_t &buf_size) {
    if (fd >= 0) {
        return;
    }

    buf_size = 0;
    buf.resize(0);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, g_h264_sock.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("connect");
        close(fd);
        fd = -1;
    } else {
        std::cerr << "Connected to H264 socket" << std::endl;
    }
}

static void close_h264(int &fd, std::vector<uint8_t>& buf, size_t &buf_size) {
    if (fd < 0) {
        return;
    }

    close(fd);
    fd = -1;
    buf.clear();
    buf_size = 0;
    std::cerr << "Disconnected from H264 socket" << std::endl;
}

static void process_h264(int &fd, std::vector<uint8_t>& buf, size_t &buf_size) {
    if (fd < 0) {
        return;
    }

    if (buf_size >= MAX_FRAME_SIZE) {
        std::cerr << "Buffer overflow, resetting buffer" << std::endl;
        buf_size = 0;
    } else if (buf_size + MIN_FRAME_SIZE / 2 > buf.size()) {
        buf.resize(buf_size + MIN_FRAME_SIZE);
    }

    ssize_t n = read(fd, buf.data() + buf_size, buf.size() - buf_size);
    if (n < 0) {
        if (n == EAGAIN || n == EWOULDBLOCK) {
            return;
        }
        std::perror("read");
        close(fd);
        fd = -1;
        return;
    }

    buf_size += n;

    const uint8_t* processed = process_frames(buf.data(), buf.data() + buf_size);
    if (processed) {
        size_t remaining = buf_size - (processed - buf.data());
        if (remaining > 0) {
            std::memmove(buf.data(), processed, remaining);
        }
        buf_size = remaining;

        if (buf.size() > MIN_FRAME_SIZE) {
            buf.resize(remaining + MIN_FRAME_SIZE);
        }
    }
}

static void signal_handler(int) {
    g_running = false;
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --webrtc-sock <path>   Unix socket for WebRTC signaling\n"
              << "  --h264-sock <path>     H264 stream input socket\n"
              << "  --max-clients <n>      Max concurrent clients (default: 4)\n"
              << "  --stun <url>           STUN server URL (can be repeated)\n"
              << "  --debug                Enable debug output\n"
              << "  --help                 Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string webrtc_sock;

    enum {
        OPT_WEBRTC_SOCK = 1,
        OPT_H264_SOCK,
        OPT_MAX_CLIENTS,
        OPT_STUN,
        OPT_DEBUG,
        OPT_HELP,
    };

    static struct option long_options[] = {
        {"webrtc-sock",  required_argument, 0, OPT_WEBRTC_SOCK},
        {"h264-sock",    required_argument, 0, OPT_H264_SOCK},
        {"max-clients",  required_argument, 0, OPT_MAX_CLIENTS},
        {"stun",         required_argument, 0, OPT_STUN},
        {"debug",        no_argument,       0, OPT_DEBUG},
        {"help",         no_argument,       0, OPT_HELP},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
        switch (opt) {
        case OPT_WEBRTC_SOCK:
            webrtc_sock = optarg;
            break;
        case OPT_H264_SOCK:
            g_h264_sock = optarg;
            break;
        case OPT_MAX_CLIENTS:
            g_max_clients = std::atoi(optarg);
            break;
        case OPT_STUN:
            g_ice_servers.push_back(optarg);
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

    if (webrtc_sock.empty() || g_h264_sock.empty()) {
        std::cerr << "Error: --webrtc-sock and --h264-sock are required\n";
        print_usage(argv[0]);
        return 1;
    }

    if (g_ice_servers.empty()) {
        g_ice_servers.push_back("stun:stun.l.google.com:19302");
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    std::cout << "WebRTC socket: " << webrtc_sock << std::endl;
    std::cout << "H264 socket: " << g_h264_sock << std::endl;
    std::cout << "Max clients: " << g_max_clients << std::endl;

    unlink(webrtc_sock.c_str());

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, webrtc_sock.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("bind");
        return 1;
    }

    chmod(webrtc_sock.c_str(), 0777);

    if (listen(listen_fd, 16) < 0) {
        std::perror("listen");
        return 1;
    }

    std::cout << "WebRTC server running..." << std::endl;

    int h264_fd = -1;
    std::vector<uint8_t> h264_buf;
    size_t h264_buf_size = 0;

    while (g_running) {
        struct pollfd pfd[] = {
            {listen_fd, POLLIN, 0},
            {h264_fd, POLLIN, 0}
        };
        int ret = poll(pfd, 2, 100);

        if (ret > 0 && (pfd[0].revents & POLLIN)) {
            int client_fd = accept(listen_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                handle_connection(client_fd);
            }
        }
        if (ret > 0 && (pfd[1].revents & POLLIN)) {
            process_h264(h264_fd, h264_buf, h264_buf_size);
        }

        ping_clients();
        cleanup_clients();

        if (has_clients()) {
            open_h264(h264_fd, h264_buf, h264_buf_size);
        } else {
            close_h264(h264_fd, h264_buf, h264_buf_size);
        }
    }

    std::cout << "Shutting down..." << std::endl;

    close(listen_fd);
    unlink(webrtc_sock.c_str());
    return 0;
}
