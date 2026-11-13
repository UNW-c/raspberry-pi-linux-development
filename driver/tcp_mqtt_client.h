#pragma once

#include <cstdint>
#include <string>

#include "driver/i_mqtt_client.h"
#include "driver/mqtt_codec.h"

namespace prts::driver {

// MQTT 3.1.1 over a plain TCP socket. Implemented by hand so the project keeps
// no external dependency on the Pi: CONNECT, PUBLISH (QoS 0), SUBSCRIBE (QoS 0),
// PINGREQ and DISCONNECT are all this project needs.
class TcpMqttClient final : public IMqttClient {
public:
    TcpMqttClient() = default;
    ~TcpMqttClient() override;

    TcpMqttClient(const TcpMqttClient&) = delete;
    TcpMqttClient& operator=(const TcpMqttClient&) = delete;

    bool connect(const model::MqttEndpoint& endpoint, std::string& error) override;
    bool publish(const std::string& topic, const std::string& payload,
                 std::string& error) override;
    bool subscribe(const std::string& topicFilter, std::string& error) override;
    bool poll(int timeoutMs, model::MqttMessage& message, std::string& error) override;

    void disconnect() override;
    bool isConnected() const override { return socket_ >= 0; }

private:
    bool openSocket(const model::MqttEndpoint& endpoint, std::string& error);
    bool sendAll(const std::string& data, std::string& error);
    bool readPacket(std::string& packet, int timeoutMs, std::string& error);
    bool expectAck(mqtt::PacketType expected, int timeoutMs, std::string& error);
    void maybePing();
    static std::int64_t nowMs();

    int socket_{-1};
    std::uint16_t nextPacketId_{1};
    int keepAliveSeconds_{60};
    std::int64_t lastPingMs_{0};
};

}  // namespace prts::driver
