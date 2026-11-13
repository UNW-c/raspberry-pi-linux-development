#include <string>

#include "driver/mqtt_codec.h"
#include "model/system_facts.h"
#include "service/telemetry_service.h"
#include "test/test_framework.h"

PRTS_TEST(mqtt_remaining_length_round_trip) {
    const std::size_t values[] = {0, 1, 127, 128, 16383, 16384, 2097151};
    for (std::size_t value : values) {
        const std::string encoded = prts::driver::mqtt::encodeRemainingLength(value);
        std::size_t decoded = 0;
        std::size_t consumed = 0;
        PRTS_CHECK(prts::driver::mqtt::decodeRemainingLength(encoded, 0, decoded, consumed));
        PRTS_CHECK_EQ(decoded, value);
        PRTS_CHECK_EQ(consumed, encoded.size());
    }
}

PRTS_TEST(mqtt_connect_packet_layout) {
    const std::string packet = prts::driver::mqtt::connectPacket("prts-node", 60);
    PRTS_CHECK(packet.size() > 12);
    PRTS_CHECK_EQ(static_cast<int>(static_cast<unsigned char>(packet[0])), 0x10);
    // Variable header starts with the 4 byte "MQTT" string.
    PRTS_CHECK_EQ(packet.substr(4, 4), std::string("MQTT"));
    PRTS_CHECK_EQ(static_cast<int>(static_cast<unsigned char>(packet[8])), 4);   // protocol level
    PRTS_CHECK_EQ(static_cast<int>(static_cast<unsigned char>(packet[9])), 2);   // clean session
}

PRTS_TEST(mqtt_publish_round_trip) {
    const std::string topic = "prts/telemetry";
    const std::string payload = "{\"ok\":true}";
    const std::string packet = prts::driver::mqtt::publishPacket(topic, payload);

    const auto parsed = prts::driver::mqtt::parsePacket(packet);
    PRTS_CHECK(parsed.ok);
    PRTS_CHECK(parsed.type == prts::driver::mqtt::PacketType::Publish);
    PRTS_CHECK_EQ(parsed.topic, topic);
    PRTS_CHECK_EQ(parsed.payload, payload);
    PRTS_CHECK_EQ(parsed.totalBytes, packet.size());
    PRTS_CHECK(parsed.qos == 0);
}

PRTS_TEST(mqtt_parse_flags_incomplete_packets) {
    const std::string packet = prts::driver::mqtt::publishPacket("prts/camera", "frame");
    const auto parsed = prts::driver::mqtt::parsePacket(packet.substr(0, packet.size() - 2));
    PRTS_CHECK(!parsed.ok);
    PRTS_CHECK_EQ(parsed.error, std::string("incomplete"));
}

PRTS_TEST(mqtt_parse_connack) {
    std::string packet;
    packet.push_back(static_cast<char>(0x20));
    packet.push_back(static_cast<char>(0x02));
    packet.push_back(static_cast<char>(0x00));
    packet.push_back(static_cast<char>(0x00));
    const auto parsed = prts::driver::mqtt::parsePacket(packet);
    PRTS_CHECK(parsed.ok);
    PRTS_CHECK(parsed.type == prts::driver::mqtt::PacketType::ConnAck);
    PRTS_CHECK_EQ(static_cast<int>(parsed.connAckCode), 0);
}

PRTS_TEST(telemetry_payload_is_json) {
    prts::model::SystemFacts facts;
    facts.hostname = "PRTS";
    facts.kernelRelease = "6.18.50+rpt-rpi-v8";
    facts.uptimeSeconds = 3723.4;
    facts.cpuTemperatureC = 43.5;
    facts.memoryTotalKb = 927232;
    facts.memoryAvailableKb = 766000;
    facts.diskTotalBytes = 14ULL * 1024 * 1024 * 1024;
    facts.diskFreeBytes = 11ULL * 1024 * 1024 * 1024;
    facts.loadAverage1 = 1.29;

    const std::string payload =
        prts::service::TelemetryService::buildPayload(facts, "2026-11-13T21:11:25");
    PRTS_CHECK(!payload.empty());
    PRTS_CHECK_EQ(payload.front(), '{');
    PRTS_CHECK_EQ(payload.back(), '}');
    PRTS_CHECK(payload.find("\"host\":\"PRTS\"") != std::string::npos);
    PRTS_CHECK(payload.find("\"ts\":\"2026-11-13T21:11:25\"") != std::string::npos);
    PRTS_CHECK(payload.find("\"cpu_temp_c\":43.5") != std::string::npos);
}

PRTS_TEST(telemetry_payload_marks_missing_temperature) {
    prts::model::SystemFacts facts;
    facts.cpuTemperatureC = -1.0;
    const std::string payload = prts::service::TelemetryService::buildPayload(facts, "t");
    PRTS_CHECK(payload.find("\"cpu_temp_c\":null") != std::string::npos);
}
