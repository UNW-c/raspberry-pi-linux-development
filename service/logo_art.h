#pragma once

#include <string>
#include <vector>

namespace prts::service {

// Geometry of the terminal artwork. Everything here is generated from a small
// set of rules (grid maths) instead of hand-drawn strings, so the emblem stays
// pixel-consistent at any size.
class LogoArt {
public:
    struct Artwork {
        std::vector<std::string> rows;  // each row padded to the requested width
        int wordmarkRow{-1};            // row that carries the wordmark
        int wordmarkStart{-1};          // first cell of the wordmark inside that row
        int wordmarkLength{0};
    };

    // A hollow diamond outline (odd row count) with the wordmark on its middle
    // row. The 45 degree slope is faked with 2 cells per step because terminal
    // cells are about twice as tall as they are wide.
    static Artwork diamond(int rows, const std::string& wordmark, int width);
};

}  // namespace prts::service
