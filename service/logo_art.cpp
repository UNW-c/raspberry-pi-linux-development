#include "service/logo_art.h"

#include <algorithm>
#include <cstddef>

#include "utils/text.h"
#include "utils/utf8.h"

namespace prts::service {
namespace {

constexpr int kDiamondCells = 20;
const char* kDiagonalUp = "\u2571";    // light diagonal, rising
const char* kDiagonalDown = "\u2572";  // light diagonal, falling

}  // namespace

LogoArt::Artwork LogoArt::diamond(int rows, const std::string& wordmark, int width) {
    Artwork artwork;
    if (rows < 3 || rows % 2 == 0 || width < kDiamondCells) {
        return artwork;
    }

    const int middle = rows / 2;
    const std::vector<std::string> mark = utils::splitUtf8(wordmark);
    const int markLength = static_cast<int>(mark.size());
    const int markStart = (kDiamondCells - markLength) / 2;
    const int leftPad = (width - kDiamondCells) / 2;

    for (int row = 0; row < rows; ++row) {
        const int step = std::min(row, rows - 1 - row);
        const int leftCell = 9 - 2 * step;
        const int rightCell = 10 + 2 * step;
        const bool upperHalf = row <= middle;

        std::vector<std::string> cells(static_cast<std::size_t>(kDiamondCells), " ");
        cells[static_cast<std::size_t>(leftCell)] = upperHalf ? kDiagonalUp : kDiagonalDown;
        cells[static_cast<std::size_t>(rightCell)] = upperHalf ? kDiagonalDown : kDiagonalUp;

        if (row == middle) {
            for (int index = 0; index < markLength && markStart + index < kDiamondCells; ++index) {
                cells[static_cast<std::size_t>(markStart + index)] = mark[static_cast<std::size_t>(index)];
            }
        }

        std::string line(static_cast<std::size_t>(leftPad), ' ');
        for (const std::string& cell : cells) {
            line.append(cell);
        }
        line = utils::Text::padRight(line, static_cast<std::size_t>(width));
        artwork.rows.push_back(line);
    }

    artwork.wordmarkRow = middle;
    artwork.wordmarkStart = leftPad + markStart;
    artwork.wordmarkLength = markLength;
    return artwork;
}

std::string LogoArt::compactLine(const std::string& systemName, const std::string& localName,
                                 const std::string& accessNote) {
    return "\u25c6 " + systemName + " \u25c6   " + localName + "  \u00b7  " + accessNote;
}

int LogoArt::gridWidth(const std::vector<std::string>& rows) {
    int width = 0;
    for (const std::string& row : rows) {
        width = std::max(width, static_cast<int>(utils::Text::displayWidth(row)));
    }
    return width;
}

}  // namespace prts::service
