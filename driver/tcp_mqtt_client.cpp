#include "driver/tcp_mqtt_client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>

namespace prts::driver {
namespace {

constexpr int kConnectTimeoutMs = 4000;
constexpr int kIoTimeoutSeconds = 2;

std::string systemError(const char* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

bool setSocketTimeout(int socket, int option, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    return ::setsockopt(socket, SOL_SOCKET, option, &timeout, sizeof(timeout)) == 0;
}

}  // namespace

TcpMqttClient::~TcpMqttClient() { disconnect(); }

std::int64_t TcpMqttClient::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool TcpMqttClient::openSocket(const model::MqttEndpoint& endpoint, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* candidates = nullptr;
    const std::string service = std::to_string(endpoint.port);
    const int lookup = ::getaddrinfo(endpoint.host.c_str(), service.c_str(), &hints, &candidates);
    if (lookup != 0) {
        error = std::string("resolve failed: ") + ::gai_strerror(lookup);
        return false;
    }

    bool connected = false;
    for (addrinfo* candidate = candidates; candidate != nullptr && !connected;
         candidate = candidate->ai_next) {
        const int socket = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socket < 0) {
            continue;
        }

        const int flags = ::fcntl(socket, F_GETFL, 0);
        ::fcntl(socket, F_SETFL, flags | O_NONBLOCK);
        const int result = ::connect(socket, candidate->ai_addr, candidate->ai_addrlen);
        if (result == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(socket, &writable);
            timeval timeout{kConnectTimeoutMs / 1000, (kConnectTimeoutMs % 1000) * 1000};
            if (::select(socket + 1, nullptr, &writable, nullptr, &timeout) > 0) {
                int soError = 0;
                socklen_t length = sizeof(soError);
                if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, &soError, &length) == 0 && soError == 0) {
                    connected = true;
                }
            }
        }

        if (!connected) {
            ::close(socket);
            continue;
        }

        ::fcntl(socket, F_SETFL, flags);
        setSocketTimeout(socket, SO_RCVTIMEO, kIoTimeoutSeconds);
        setSocketTimeout(socket, SO_SNDTIMEO, kIoTimeoutSeconds);
        socket_ = socket;
    }

    ::freeaddrinfo(candidates);
    if (!connected) {
        error = "connect failed: " + std::string(std::strerror(errno)) + " (" + endpoint.host + ":" +
                service + ")";
        return false;
    }
    return true;
}

bool TcpMqttClient::sendAll(const std::string& data, std::string& error) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = ::send(socket_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            if (errno == EINTR) {
                continue;
            }
            error = systemError("send");
            disconnect();
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

bool TcpMqttClient::readPacket(std::string& packet, int timeoutMs, std::string& error) {
    packet.clear();
    if (socket_ < 0) {
        error = "not connected";
        return false;
    }

    setSocketTimeout(socket_, SO_RCVTIMEO, (timeoutMs + 999) / 1000);

    char byte = 0;
    ssize_t received = ::recv(socket_, &byte, 1, 0);
    if (received <= 0) {
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            error = "timeout";
            return false;
        }
        if (received < 0 && errno == EINTR) {
            error = "timeout";
            return false;
        }
        error = "connection closed";
        disconnect();
        return false;
    }
    packet.push_back(byte);

    std::size_t remaining = 0;
    std::size_t multiplier = 1;
    for (int index = 0; index < 4; ++index) {
        received = ::recv(socket_, &byte, 1, 0);
        if (received <= 0) {
            error = "connection closed";
            disconnect();
            return false;
        }
        packet.push_back(byte);
        const auto digit = static_cast<std::uint8_t>(byte);
        remaining += static_cast<std::size_t>(digit & 0x7F) * multiplier;
        if ((digit & 0x80) == 0) {
            break;
        }
        multiplier *= 128;
    }

    std::string body(remaining, '\0');
    std::size_t filled = 0;
    while (filled < remaining) {
        received = ::recv(socket_, body.data() + filled, remaining - filled, 0);
        if (received <= 0) {
            if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                error = "timeout";
                return false;
            }
            error = "connection closed";
            disconnect();
            return false;
        }
        filled += static_cast<std::size_t>(received);
    }
    packet += body;
    return true;
}

bool TcpMqttClient::expectAck(mqtt::PacketType expected, int timeoutMs, std::string& error) {
    std::string packet;
    if (!readPacket(packet, timeoutMs, error)) {
        return false;
    }
    const mqtt::ParsedPacket parsed = mqtt::parsePacket(packet);
    if (!parsed.ok) {
        error = "malformed packet: " + parsed.error;
        return false;
    }
    if (parsed.type != expected) {
        error = "unexpected packet type";
        return false;
    }
    return true;
}

bool TcpMqttClient::connect(const model::MqttEndpoint& endpoint, std::string& error) {
    disconnect();
    if (!openSocket(endpoint, error)) {
        return false;
    }
    keepAliveSeconds_ = endpoint.keepAliveSeconds;

    if (!sendAll(mqtt::connectPacket(endpoint.clientId, endpoint.keepAliveSeconds), error)) {
        return false;
    }

    std::string packet;
    if (!readPacket(packet, kConnectTimeoutMs, error)) {
        disconnect();
        return false;
    }
    const mqtt::ParsedPacket parsed = mqtt::parsePacket(packet);
    if (!parsed.ok || parsed.type != mqtt::PacketType::ConnAck) {
        error = "no CONNACK: " + parsed.error;
        disconnect();
        return false;
    }
    if (parsed.connAckCode != 0) {
        error = "broker refused connection, code " + std::to_string(parsed.connAckCode);
        disconnect();
        return false;
    }

    lastPingMs_ = nowMs();
    return true;
}

bool TcpMqttClient::publish(const std::string& topic, const std::string& payload,
                            std::string& error) {
    if (socket_ < 0) {
        error = "not connected";
        return false;
    }
    return sendAll(mqtt::publishPacket(topic, payload), error);
}

bool TcpMqttClient::subscribe(const std::string& topicFilter, std::string& error) {
    if (socket_ < 0) {
        error = "not connected";
        return false;
    }
    const std::uint16_t packetId = nextPacketId_++;
    if (!sendAll(mqtt::subscribePacket(topicFilter, packetId), error)) {
        return false;
    }
    return expectAck(mqtt::PacketType::SubAck, kConnectTimeoutMs, error);
}

bool TcpMqttClient::poll(int timeoutMs, model::MqttMessage& message, std::string& error) {
    error.clear();
    if (socket_ < 0) {
        error = "not connected";
        return false;
    }
    maybePing();

    std::string packet;
    if (!readPacket(packet, timeoutMs, error)) {
        if (error == "timeout") {
            error.clear();
        }
        return false;
    }

    const mqtt::ParsedPacket parsed = mqtt::parsePacket(packet);
    if (!parsed.ok) {
        error = "malformed packet: " + parsed.error;
        return false;
    }
    if (parsed.type != mqtt::PacketType::Publish) {
        return false;
    }

    message.topic = parsed.topic;
    message.payload = parsed.payload;
    message.retained = parsed.retained;
    message.qos = parsed.qos;
    return true;
}

void TcpMqttClient::maybePing() {
    if (keepAliveSeconds_ <= 0) {
        return;
    }
    const std::int64_t now = nowMs();
    if (now - lastPingMs_ < static_cast<std::int64_t>(keepAliveSeconds_) * 500) {
        return;
    }
    std::string ignored;
    sendAll(mqtt::pingRequestPacket(), ignored);
    lastPingMs_ = now;
}

void TcpMqttClient::disconnect() {
    if (socket_ >= 0) {
        std::string ignored;
        sendAll(mqtt::disconnectPacket(), ignored);
        ::close(socket_);
        socket_ = -1;
    }
}

}  // namespace prts::driver
