#pragma once

#include <functional>
#include <string>

#include "model/mqtt.h"

namespace prts::driver {

// Minimal MQTT 3.1.1 client contract (QoS 0 publish / subscribe). Keeping it an
// interface lets the services be tested with a fake broker connection.
class IMqttClient {
public:
    virtual ~IMqttClient() = default;

    virtual bool connect(const model::MqttEndpoint& endpoint, std::string& error) = 0;
    virtual bool publish(const std::string& topic, const std::string& payload,
                         std::string& error) = 0;
    virtual bool subscribe(const std::string& topicFilter, std::string& error) = 0;

    // Waits up to timeoutMs for an incoming PUBLISH. Returns true when a message
    // was delivered; false with an empty error means "nothing arrived".
    virtual bool poll(int timeoutMs, model::MqttMessage& message, std::string& error) = 0;

    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
};

}  // namespace prts::driver
