#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prts::model {

enum class CheckStatus {
    Ok,
    Warn,
    Fail,
};

inline const char* toString(CheckStatus status) {
    switch (status) {
        case CheckStatus::Ok:
            return "OK";
        case CheckStatus::Warn:
            return "WARN";
        case CheckStatus::Fail:
            return "FAIL";
    }
    return "?";
}

// One row of the self-check table: stable identifier used for ordering, the
// value that was measured, and the verdict.
struct CheckResult {
    std::string id;
    std::string label;
    std::string value;
    std::string note;
    CheckStatus status{CheckStatus::Ok};
    std::int64_t elapsedMs{0};
};

}  // namespace prts::model
