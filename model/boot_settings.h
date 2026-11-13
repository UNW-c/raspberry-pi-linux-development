#pragma once

#include <string>

#include "model/color.h"

namespace prts::model {

// Colour roles of the PRTS interface. Anything that draws must ask for a role,
// never for a raw RGB value, so that a palette change stays in one place.
struct Palette {
    Color background{0x00, 0x00, 0x00};
    Color frame{0x11, 0xAF, 0xCF};
    Color frameDim{0x0C, 0x54, 0x63};
    Color primary{0xC8, 0xD2, 0xD6};
    Color muted{0x67, 0x75, 0x7B};
    Color accent{0x11, 0xAF, 0xCF};
    Color accentBright{0x8F, 0xE9, 0xFA};
    Color ok{0x6F, 0xBF, 0x9B};
    Color warn{0xD9, 0xA4, 0x41};
    Color error{0xB4, 0x43, 0x3C};
};

// Textual identity of the terminal. Mirrors the [identity] config section.
struct Identity {
    std::string systemName{"PRTS"};
    std::string systemFullName{"PRIMARY RHODES ISLAND TERMINAL SYSTEM"};
    std::string systemLocalName{"原生罗德岛终端服务"};
    std::string operatorName{"seven"};
};

struct BootSettings {
    bool enabled{true};
    int charDelayMs{10};
    int frameDelayMs{30};
    int checkDelayMs{90};
    int targetDurationMs{5200};
    int minColumnsFull{78};
    int minRowsFull{30};
    int scanSweeps{3};

    Palette palette;
    Identity identity;
};

}  // namespace prts::model
