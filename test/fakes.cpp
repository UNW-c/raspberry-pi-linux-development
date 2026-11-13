#include <filesystem>
#include <string>

#include "test/fakes.h"

namespace prts::test {

std::string temporaryDirectory() {
    static const std::filesystem::path directory = [] {
        const std::filesystem::path candidate =
            std::filesystem::temp_directory_path() / "prts-tests";
        std::error_code error;
        std::filesystem::create_directories(candidate, error);
        return candidate;
    }();
    return directory.string();
}

void clearTemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(temporaryDirectory(), error);
}

}  // namespace prts::test
