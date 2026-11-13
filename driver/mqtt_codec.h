#pragma once

#include <cstdint>
#include <string>

namespace prts::driver::mqtt {

// MQTT 3.1.1 control packet types used by this project.
enum class PacketType {
    Connect = 1,
    ConnAck = 2,
    Publish = 3,
    PubAck = 4,
    Subscribe = 8,
    SubAck = 9,
    PingReq = 12,
    PingResp = 13,
    Disconnect = 14,
    Unknown = 0,
};

// One parsed control packet.
struct ParsedPacket {
    PacketType type{PacketType::Unknown};
    std::string topic;
    std::string payload;
    std::uint16_t packetId{0};
    std::uint8_t connAckCode{0};
    int qos{0};
    bool retained{false};
    std::size_t totalBytes{0};
    bool ok{false};
    std::string error;
};

// Encoders. All return the complete packet, fixed header included.
std::string connectPacket(const std::string& clientId, int keepAliveSeconds);
std::string publishPacket(const std::string& topic, const std::string& payload,
                          bool retain = false);
std::string subscribePacket(const std::string& topicFilter, std::uint16_t packetId);
std::string pingRequestPacket();
std::string disconnectPacket();

// Decodes one packet from the beginning of the buffer. When the buffer holds an
// incomplete packet, ok is false and error is "incomplete".
ParsedPacket parsePacket(const std::string& buffer);

// Helpers exposed for tests and for the socket reader.
std::string encodeRemainingLength(std::size_t length);
bool decodeRemainingLength(const std::string& buffer, std::size_t offset, std::size_t& length,
                           std::size_t& consumed);

}  // namespace prts::driver::mqtt
