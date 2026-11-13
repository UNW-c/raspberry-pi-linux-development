#include "driver/mqtt_codec.h"

#include <cstddef>

namespace prts::driver::mqtt {
namespace {

void appendUint16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void appendString(std::string& out, const std::string& text) {
    appendUint16(out, static_cast<std::uint16_t>(text.size()));
    out.append(text);
}

std::uint16_t readUint16(const std::string& buffer, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint8_t>(buffer[offset]) << 8) |
                                      static_cast<std::uint8_t>(buffer[offset + 1]));
}

std::string frame(std::uint8_t header, const std::string& body) {
    std::string packet;
    packet.push_back(static_cast<char>(header));
    packet += encodeRemainingLength(body.size());
    packet += body;
    return packet;
}

}  // namespace

std::string encodeRemainingLength(std::size_t length) {
    std::string out;
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(length % 128);
        length /= 128;
        if (length > 0) {
            byte = static_cast<std::uint8_t>(byte | 0x80);
        }
        out.push_back(static_cast<char>(byte));
    } while (length > 0);
    return out;
}

bool decodeRemainingLength(const std::string& buffer, std::size_t offset, std::size_t& length,
                           std::size_t& consumed) {
    length = 0;
    consumed = 0;
    std::size_t multiplier = 1;
    for (int index = 0; index < 4; ++index) {
        if (offset + static_cast<std::size_t>(index) >= buffer.size()) {
            return false;
        }
        const auto byte = static_cast<std::uint8_t>(buffer[offset + static_cast<std::size_t>(index)]);
        length += static_cast<std::size_t>(byte & 0x7F) * multiplier;
        ++consumed;
        if ((byte & 0x80) == 0) {
            return true;
        }
        multiplier *= 128;
    }
    return false;
}

std::string connectPacket(const std::string& clientId, int keepAliveSeconds) {
    std::string body;
    appendString(body, "MQTT");
    body.push_back(0x04);  // protocol level 3.1.1
    body.push_back(0x02);  // clean session
    appendUint16(body, static_cast<std::uint16_t>(keepAliveSeconds < 0 ? 0 : keepAliveSeconds));
    appendString(body, clientId);
    return frame(0x10, body);
}

std::string publishPacket(const std::string& topic, const std::string& payload, bool retain) {
    std::string body;
    appendString(body, topic);
    body.append(payload);
    const std::uint8_t header = static_cast<std::uint8_t>(0x30 | (retain ? 0x01 : 0x00));
    return frame(header, body);
}

std::string subscribePacket(const std::string& topicFilter, std::uint16_t packetId) {
    std::string body;
    appendUint16(body, packetId);
    appendString(body, topicFilter);
    body.push_back(0x00);  // requested QoS 0
    return frame(0x82, body);
}

std::string pingRequestPacket() { return frame(0xC0, std::string()); }

std::string disconnectPacket() { return frame(0xE0, std::string()); }

ParsedPacket parsePacket(const std::string& buffer) {
    ParsedPacket packet;
    if (buffer.size() < 2) {
        packet.error = "incomplete";
        return packet;
    }

    const auto header = static_cast<std::uint8_t>(buffer[0]);
    const auto typeCode = static_cast<std::uint8_t>(header >> 4);
    std::size_t remaining = 0;
    std::size_t consumed = 0;
    if (!decodeRemainingLength(buffer, 1, remaining, consumed)) {
        packet.error = "incomplete";
        return packet;
    }

    const std::size_t bodyStart = 1 + consumed;
    if (buffer.size() < bodyStart + remaining) {
        packet.error = "incomplete";
        return packet;
    }
    packet.totalBytes = bodyStart + remaining;

    switch (typeCode) {
        case 2: {  // CONNACK
            packet.type = PacketType::ConnAck;
            if (remaining < 2) {
                packet.error = "short connack";
                return packet;
            }
            packet.connAckCode = static_cast<std::uint8_t>(buffer[bodyStart + 1]);
            packet.ok = true;
            return packet;
        }
        case 3: {  // PUBLISH
            packet.type = PacketType::Publish;
            if (remaining < 2) {
                packet.error = "short publish";
                return packet;
            }
            const std::size_t topicLength = readUint16(buffer, bodyStart);
            if (bodyStart + 2 + topicLength > buffer.size()) {
                packet.error = "short topic";
                return packet;
            }
            packet.topic = buffer.substr(bodyStart + 2, topicLength);
            std::size_t offset = bodyStart + 2 + topicLength;
            packet.qos = (header >> 1) & 0x03;
            if (packet.qos > 0) {
                if (offset + 2 > bodyStart + remaining) {
                    packet.error = "short packet id";
                    return packet;
                }
                packet.packetId = readUint16(buffer, offset);
                offset += 2;
            }
            packet.retained = (header & 0x01) != 0;
            packet.payload = buffer.substr(offset, bodyStart + remaining - offset);
            packet.ok = true;
            return packet;
        }
        case 4:    // PUBACK
        case 9: {  // SUBACK
            packet.type = typeCode == 4 ? PacketType::PubAck : PacketType::SubAck;
            if (remaining < 2) {
                packet.error = "short ack";
                return packet;
            }
            packet.packetId = readUint16(buffer, bodyStart);
            packet.ok = true;
            return packet;
        }
        case 13:
            packet.type = PacketType::PingResp;
            packet.ok = true;
            return packet;
        default:
            packet.type = PacketType::Unknown;
            packet.ok = true;
            return packet;
    }
}

}  // namespace prts::driver::mqtt
