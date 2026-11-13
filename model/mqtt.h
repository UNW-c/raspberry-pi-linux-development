#pragma once

#include <string>

namespace prts::model {

// Where the MQTT client connects and how it identifies itself.
struct MqttEndpoint {
    std::string host{"127.0.0.1"};
    int port{1883};
    std::string clientId{"prts-node"};
    int keepAliveSeconds{60};
};

// One received or to be sent message.
struct MqttMessage {
    std::string topic;
    std::string payload;
    bool retained{false};
    int qos{0};
};

}  // namespace prts::model
