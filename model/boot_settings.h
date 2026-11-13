#pragma once

#include <string>

#include "model/color.h"

namespace prts::model {

// Colour roles of the PRTS terminal. The system is monochrome first: white and
// greys carry the information, a single warm gold marks the active elements,
// and red is reserved for faults. Values follow the published Arknights design
// tokens (terminal theme) so the palette stays recognisable.
struct Palette {
    Color background{0x00, 0x00, 0x00};
    Color rule{0x33, 0x33, 0x33};
    Color primary{0xF2, 0xF2, 0xF2};
    Color secondary{0xC6, 0xC6, 0xC6};
    Color muted{0x8A, 0x8A, 0x8A};
    Color subtle{0x5C, 0x5C, 0x5C};
    Color accent{0xD9, 0xA4, 0x41};
    Color accentBright{0xF0, 0xC6, 0x74};
    Color danger{0xD4, 0x48, 0x3B};
};

// Textual identity of the terminal. Mirrors the [identity] config section.
struct Identity {
    std::string systemName{"PRTS"};
    std::string systemFullName{"PRIMARY RHODES ISLAND TERMINAL SYSTEM"};
    std::string systemLocalName{"原生罗德岛终端服务"};
    std::string systemTag{"RHODES ISLAND"};
    std::string accessNote{"访问权限已确认"};
    std::string operatorName{"seven"};
};

struct BootSettings {
    bool enabled{true};
    int charDelayMs{6};
    int frameDelayMs{24};
    int checkDelayMs{70};
    int targetDurationMs{4200};
    int minColumnsFull{62};
    int minRowsFull{27};
    int flashFrames{1};

    Palette palette;
    Identity identity;
};

}  // namespace prts::model
