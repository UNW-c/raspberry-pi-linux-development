#include "service/camera_web_service.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "driver/http_request.h"

namespace prts::service {
namespace {

volatile std::sig_atomic_t g_running = 1;

void handleStopSignal(int) { g_running = 0; }

constexpr int kMaxStreamClients = 16;
constexpr int kRequestTimeoutMs = 300;
constexpr const char* kBoundary = "prtsframe";

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool sendAll(int socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

std::string httpResponse(const char* status, const char* contentType, const std::string& body,
                         bool includeBody = true) {
    std::string response = "HTTP/1.1 ";
    response += status;
    response += "\r\nContent-Type: ";
    response += contentType;
    response += "\r\nContent-Length: " + std::to_string(body.size());
    response += "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (includeBody) {
        response += body;
    }
    return response;
}

std::string streamHeaders() {
    std::string headers = "HTTP/1.1 200 OK\r\n";
    headers += "Content-Type: multipart/x-mixed-replace; boundary=";
    headers += kBoundary;
    headers += "\r\nCache-Control: no-store\r\nPragma: no-cache\r\nConnection: close\r\n\r\n";
    return headers;
}

std::string frameChunk(const std::string& jpeg) {
    std::string chunk = "--";
    chunk += kBoundary;
    chunk += "\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    chunk += std::to_string(jpeg.size());
    chunk += "\r\n\r\n";
    chunk += jpeg;
    chunk += "\r\n";
    return chunk;
}

// Reads the frame size out of the JPEG SOF marker so /status can report it
// without depending on the separate metadata topic.
bool jpegDimensions(const std::string& data, int& width, int& height) {
    if (data.size() < 4 || static_cast<unsigned char>(data[0]) != 0xFF ||
        static_cast<unsigned char>(data[1]) != 0xD8) {
        return false;
    }
    std::size_t index = 2;
    while (index + 9 < data.size()) {
        if (static_cast<unsigned char>(data[index]) != 0xFF) {
            ++index;
            continue;
        }
        const auto marker = static_cast<unsigned char>(data[index + 1]);
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            index += 2;
            continue;
        }
        const std::size_t length =
            (static_cast<std::size_t>(static_cast<unsigned char>(data[index + 2])) << 8) |
            static_cast<unsigned char>(data[index + 3]);
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
            marker != 0xCC) {
            height = (static_cast<int>(static_cast<unsigned char>(data[index + 5])) << 8) |
                     static_cast<int>(static_cast<unsigned char>(data[index + 6]));
            width = (static_cast<int>(static_cast<unsigned char>(data[index + 7])) << 8) |
                    static_cast<int>(static_cast<unsigned char>(data[index + 8]));
            return width > 0 && height > 0;
        }
        index += 2 + length;
    }
    return false;
}

}  // namespace

CameraWebService::CameraWebService(driver::IMqttClient& mqtt, utils::Logger& logger)
    : mqtt_(mqtt), logger_(logger) {}

const char* CameraWebService::pageHtml() {
    return R"HTML(<!doctype html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PRTS / CAMERA</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 20px 22px 16px;
    background: #000; color: #f0f0f0;
    font: 13px/1.6 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  }
  header { display: flex; align-items: baseline; gap: 12px; }
  .bar { color: #bfbfbf; }
  .name { font-weight: 700; letter-spacing: .12em; }
  .tag { margin-left: auto; color: #8a8a8a; letter-spacing: .18em; font-size: 11px; }
  .rule { height: 1px; background: #2e2e2e; margin: 10px 0 14px; }
  .frame { display: block; width: 100%; max-width: 960px; border: 1px solid #2e2e2e; background: #0a0a0a; }
  footer { display: flex; gap: 18px; flex-wrap: wrap; color: #8a8a8a; margin-top: 12px; }
  footer b { color: #bfbfbf; font-weight: 400; }
  .warn { color: #c9a227; }
</style>
</head>
<body>
<header>
  <span class="bar">&#9612;</span>
  <span class="name">PRTS</span>
  <span class="tag">CAMERA</span>
</header>
<div class="rule"></div>
<img class="frame" src="/stream" alt="camera stream">
<div class="rule"></div>
<footer>
  <span>FRAME <b id="frames">0</b></span>
  <span>FPS <b id="fps">0</b></span>
  <span>SIZE <b id="size">-</b></span>
  <span>BYTES <b id="bytes">0</b></span>
  <span>VIEWERS <b id="clients">0</b></span>
  <span>MQTT <b id="mqtt">-</b></span>
  <span id="note" class="warn"></span>
</footer>
<script>
async function refresh() {
  try {
    const response = await fetch('/status', { cache: 'no-store' });
    const status = await response.json();
    document.getElementById('frames').textContent = status.frames;
    document.getElementById('fps').textContent = status.fps;
    document.getElementById('size').textContent = status.width + 'x' + status.height;
    document.getElementById('bytes').textContent = status.bytes;
    document.getElementById('clients').textContent = status.clients;
    document.getElementById('mqtt').textContent = status.mqtt;
    document.getElementById('note').textContent = status.width ? '' : 'waiting for the first frame';
  } catch (error) {
    document.getElementById('mqtt').textContent = 'lost';
  }
}
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";
}

int CameraWebService::run(const Options& options) {
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);

    const int listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket < 0) {
        std::fprintf(stderr, "prts-web: socket: %s\n", std::strerror(errno));
        return 2;
    }
    int reuse = 1;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(options.port));
    if (::inet_pton(AF_INET, options.bindAddress.c_str(), &address.sin_addr) != 1) {
        std::fprintf(stderr, "prts-web: invalid bind address %s\n", options.bindAddress.c_str());
        ::close(listenSocket);
        return 2;
    }
    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::fprintf(stderr, "prts-web: bind %s:%d: %s\n", options.bindAddress.c_str(), options.port,
                     std::strerror(errno));
        ::close(listenSocket);
        return 2;
    }
    if (::listen(listenSocket, 16) < 0) {
        std::fprintf(stderr, "prts-web: listen: %s\n", std::strerror(errno));
        ::close(listenSocket);
        return 2;
    }
    const int flags = ::fcntl(listenSocket, F_GETFL, 0);
    ::fcntl(listenSocket, F_SETFL, flags | O_NONBLOCK);

    std::printf("prts-web: http://%s:%d/  (routes: / /stream /snapshot /status)\n",
                options.bindAddress.c_str(), options.port);
    std::printf("prts-web: frames from mqtt %s:%d topic=%s\n", options.mqttHost.c_str(),
                options.mqttPort, options.frameTopic.c_str());
    std::fflush(stdout);

    model::MqttEndpoint endpoint;
    endpoint.host = options.mqttHost;
    endpoint.port = options.mqttPort;
    endpoint.clientId = options.clientId;

    bool mqttReady = false;
    std::string error;
    mqttReady = mqtt_.connect(endpoint, error) && mqtt_.subscribe(options.frameTopic, error);
    if (!mqttReady) {
        std::printf("prts-web: mqtt unavailable (%s), retrying in the background\n", error.c_str());
        std::fflush(stdout);
    }
    std::int64_t lastReconnect = nowMs();

    std::vector<int> viewers;
    std::string latestFrame;
    std::uint64_t frameCount = 0;
    std::uint64_t byteCount = 0;
    int width = 0;
    int height = 0;
    int fps = 0;
    int framesThisSecond = 0;
    std::int64_t lastFpsMark = nowMs();

    while (g_running != 0) {
        const std::int64_t loopStart = nowMs();

        if (!mqttReady && loopStart - lastReconnect > 5000) {
            lastReconnect = loopStart;
            error.clear();
            if (mqtt_.connect(endpoint, error) && mqtt_.subscribe(options.frameTopic, error)) {
                mqttReady = true;
                std::printf("prts-web: mqtt connected\n");
                std::fflush(stdout);
            }
        }

        bool gotFrame = false;
        if (mqttReady) {
            model::MqttMessage message;
            error.clear();
            if (mqtt_.poll(15, message, error)) {
                latestFrame = message.payload;
                ++frameCount;
                ++framesThisSecond;
                byteCount += message.payload.size();
                gotFrame = true;
                if (width == 0) {
                    int parsedWidth = 0;
                    int parsedHeight = 0;
                    if (jpegDimensions(message.payload, parsedWidth, parsedHeight)) {
                        width = parsedWidth;
                        height = parsedHeight;
                    }
                    std::printf("prts-web: first frame, %zu bytes\n", message.payload.size());
                    std::fflush(stdout);
                }
            } else if (!error.empty()) {
                std::printf("prts-web: mqtt lost: %s\n", error.c_str());
                std::fflush(stdout);
                mqttReady = false;
            }
        }

        if (gotFrame) {
            const std::string chunk = frameChunk(latestFrame);
            for (std::size_t index = 0; index < viewers.size();) {
                if (!sendAll(viewers[index], chunk)) {
                    ::close(viewers[index]);
                    viewers.erase(viewers.begin() + static_cast<std::ptrdiff_t>(index));
                    continue;
                }
                ++index;
            }
        }

        const std::int64_t now = nowMs();
        if (now - lastFpsMark >= 1000) {
            fps = framesThisSecond;
            framesThisSecond = 0;
            lastFpsMark = now;
        }

        // Accept and answer one request per loop iteration.
        sockaddr_in peer{};
        socklen_t peerLength = sizeof(peer);
        const int client = ::accept(listenSocket, reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (client >= 0 && viewers.size() < kMaxStreamClients + 4) {
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = kRequestTimeoutMs * 1000;
            ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            char buffer[2048] = {0};
            const ssize_t received = ::recv(client, buffer, sizeof(buffer) - 1, 0);
            const std::string raw(buffer, received > 0 ? static_cast<std::size_t>(received) : 0);
            const driver::HttpRequest request = driver::parseHttpRequestHead(raw);
            const bool headOnly = request.method == "HEAD";

            if (!request.ok) {
                const std::string body = "bad request\n";
                sendAll(client, httpResponse("400 Bad Request", "text/plain", body, !headOnly));
                ::close(client);
            } else if (request.path == "/" || request.path == "/index.html") {
                const std::string page = pageHtml();
                sendAll(client, httpResponse("200 OK", "text/html; charset=utf-8", page, !headOnly));
                ::close(client);
            } else if (request.path == "/stream") {
                if (sendAll(client, streamHeaders())) {
                    viewers.push_back(client);
                } else {
                    ::close(client);
                }
            } else if (request.path == "/snapshot") {
                if (latestFrame.empty()) {
                    sendAll(client, httpResponse("503 Service Unavailable", "text/plain",
                                                 "no frame yet\n", !headOnly));
                } else {
                    sendAll(client, httpResponse("200 OK", "image/jpeg", latestFrame, !headOnly));
                }
                ::close(client);
            } else if (request.path == "/status") {
                std::string body = "{";
                body += "\"frames\":" + std::to_string(frameCount);
                body += ",\"fps\":" + std::to_string(fps);
                body += ",\"bytes\":" + std::to_string(byteCount);
                body += ",\"clients\":" + std::to_string(viewers.size());
                body += ",\"width\":" + std::to_string(width);
                body += ",\"height\":" + std::to_string(height);
                body += ",\"mqtt\":\"" + std::string(mqttReady ? "up" : "down") + "\"";
                body += ",\"last_bytes\":" + std::to_string(latestFrame.size());
                body += "}";
                sendAll(client, httpResponse("200 OK", "application/json", body, !headOnly));
                ::close(client);
            } else {
                sendAll(client, httpResponse("404 Not Found", "text/plain", "not found\n", !headOnly));
                ::close(client);
            }
        }

        // Keep the loop responsive without burning the Pi's CPU.
        const std::int64_t spent = nowMs() - loopStart;
        if (spent < 5) {
            pollfd idle{listenSocket, POLLIN, 0};
            ::poll(&idle, 1, static_cast<int>(5 - spent));
        }
    }

    for (int viewer : viewers) {
        ::close(viewer);
    }
    ::close(listenSocket);
    mqtt_.disconnect();
    std::printf("prts-web: stopped, %llu frames served\n",
                static_cast<unsigned long long>(frameCount));
    std::fflush(stdout);
    return 0;
}

}  // namespace prts::service
