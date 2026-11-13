#pragma once

#include <string>

#include "model/color.h"

namespace prts::model {

// Colour roles of the PRTS terminal. The interface is monochrome: white and
// greys carry every piece of information, and colour only ever appears as a
// state - amber for a warning, red for a fault. Structure is expressed with
// hairlines and inverse video, never with a decorative tint.
struct Palette {
    Color background{0x00, 0x00, 0x00};
    Color rule{0x2E, 0x2E, 0x2E};
    Color primary{0xF0, 0xF0, 0xF0};
    Color secondary{0xBF, 0xBF, 0xBF};
    Color muted{0x8A, 0x8A, 0x8A};
    Color subtle{0x5A, 0x5A, 0x5A};
    Color warn{0xC9, 0xA2, 0x27};
    Color danger{0xC0, 0x39, 0x2B};
};

// Textual identity of the terminal. Only names that exist in the setting are
// used here; everything else printed by the sequence is measured data.
struct Identity {
    std::string systemName{"PRTS"};
    std::string systemTag{"RHODES ISLAND"};
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
