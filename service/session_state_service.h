#pragma once

#include <cstdint>
#include <string>

namespace prts::service {

// Remembers the previous login so the banner can greet the operator instead of
// showing a cold boot every time. State lives in XDG_STATE_HOME.
class SessionStateService {
public:
    struct State {
        std::string lastLogin;
        std::uint64_t loginCount{0};
    };

    explicit SessionStateService(std::string statePath);

    static std::string defaultStatePath();

    State read() const;
    void record(const std::string& timestamp) const;

private:
    std::string path_;
};

}  // namespace prts::service
