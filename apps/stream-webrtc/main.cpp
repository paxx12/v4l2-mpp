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

#include "h264_frames.h"
#include "h264_stream.h"
#include "log.h"

using json = nlohmann::json;

static std::atomic<bool> g_running{true};
static int g_debug = 0;
static h264_stream_t g_h264_stream = H264_STREAM_INIT;

static constexpr int PING_INTERVAL_MS = 1000;
static constexpr int CONNECT_TIMEOUT_MS = 30000;
static constexpr int PONG_TIMEOUT_MS = 30000;
static constexpr int DEFAULT_SESSION_S = 60 * 60;
static constexpr int MAX_SESSION_WITHOUT_TIMEOUT_S = 15 * 60;

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
    bool keepAlive = false;
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

static void cleanup_clients() {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    g_clients.remove_if([](const std::shared_ptr<Client>& c) {
        if (!c->pc || c->pc->state() == rtc::PeerConnection::State::Closed ||
            c->pc->state() == rtc::PeerConnection::State::Failed) {
            log_errorf("Removed client %s\n", c->id.c_str());
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
            log_errorf("Client %s session timeout\n", c->id.c_str());
            c->pc->close();
            continue;
        }

        if (!c->data_channel || !c->data_channel->isOpen()) {
            if (elapsed * 1000 >= CONNECT_TIMEOUT_MS) {
                log_errorf("Client %s connection timeout\n", c->id.c_str());
                c->pc->close();
            }
            continue;
        }

        auto since_pong = std::chrono::duration_cast<std::chrono::milliseconds>(now - c->last_pong).count();
        if (since_pong >= PONG_TIMEOUT_MS) {
            if (c->keepAlive) {
                log_errorf("Client %s pong timeout\n", c->id.c_str());
                c->pc->close();
                continue;
            }

            log_errorf("Client %s pong timeout, but keepAlive is false\n", c->id.c_str());
            c->last_pong = now;
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

static std::shared_ptr<Client> create_client(const json& request) {
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
            log_errorf("Client %s state: %d\n", c->id.c_str(), static_cast<int>(state));
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

    if (request.contains("timeout_s") && request["timeout_s"].is_number()) {
        client->timeout_s = request["timeout_s"].get<int>();
    }

    if (client->timeout_s <= 0) {
        client->timeout_s = DEFAULT_SESSION_S;
    }

    client->keepAlive = request.value("keepAlive", false);

    if (!client->keepAlive && client->timeout_s > MAX_SESSION_WITHOUT_TIMEOUT_S) {
        log_errorf("Capping client timeout to %d seconds since keepAlive is false\n", MAX_SESSION_WITHOUT_TIMEOUT_S);
        client->timeout_s = MAX_SESSION_WITHOUT_TIMEOUT_S;
    }

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

        auto client = create_client(request);
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

        auto client = create_client(request);
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

static void signal_handler(int) {
    g_running = false;
}

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --webrtc-sock <path>   Unix socket for WebRTC signaling\n");
    printf("  --h264-sock <path>     H264 stream input socket\n");
    printf("  --max-clients <n>      Max concurrent clients (default: 4)\n");
    printf("  --stun <url>           STUN server URL (can be repeated)\n");
    printf("  --debug                Enable debug output\n");
    printf("  --help                 Show this help\n");
}

int main(int argc, char* argv[]) {
    log_printf("stream-webrtc - built %s (%s)\n", __DATE__, __FILE__);

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
        log_errorf("Error: --webrtc-sock and --h264-sock are required\n");
        print_usage(argv[0]);
        return 1;
    }

    if (g_ice_servers.empty()) {
        g_ice_servers.push_back("stun:stun.l.google.com:19302");
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    log_printf("WebRTC socket: %s\n", webrtc_sock.c_str());
    log_printf("H264 socket: %s\n", g_h264_sock.c_str());
    log_printf("Max clients: %d\n", g_max_clients);

    unlink(webrtc_sock.c_str());

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        log_perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, webrtc_sock.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_perror("bind");
        return 1;
    }

    chmod(webrtc_sock.c_str(), 0777);

    if (listen(listen_fd, 16) < 0) {
        log_perror("listen");
        return 1;
    }

    log_printf("WebRTC server running...\n");

    while (g_running) {
        struct pollfd pfd[] = {
            {listen_fd, POLLIN, 0},
            {g_h264_stream.fd, POLLIN, 0}
        };
        int ret = poll(pfd, 2, 1000);

        if (ret > 0 && (pfd[0].revents & POLLIN)) {
            int client_fd = accept(listen_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                handle_connection(client_fd);
            }
        }
        if (ret > 0 && (pfd[1].revents & POLLIN)) {
            h264_stream_process(&g_h264_stream, send_frame);
        }

        ping_clients();
        cleanup_clients();

        if (has_clients()) {
            h264_stream_open(&g_h264_stream, g_h264_sock.c_str());
        } else {
            h264_stream_close(&g_h264_stream);
        }
    }

    log_printf("Shutting down...\n");

    h264_stream_close(&g_h264_stream);
    close(listen_fd);
    unlink(webrtc_sock.c_str());
    return 0;
}
