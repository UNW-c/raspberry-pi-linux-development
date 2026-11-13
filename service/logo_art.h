#pragma once

#include <string>
#include <vector>

namespace prts::service {

// Static artwork grids. Kept separate from the drawing logic so that the
// sequence service only deals with composition and timing.
class LogoArt {
public:
    // Seven row diamond emblem with a solid core, 13 cells wide.
    static const std::vector<std::string>& emblem();
    // Five row block wordmark, 21 cells wide.
    static const std::vector<std::string>& wordmark();
    // Single line fallback used by the compact layout.
    static std::string compactMark(const std::string& systemName);

    static int gridWidth(const std::vector<std::string>& rows);
};

}  // namespace prts::service
