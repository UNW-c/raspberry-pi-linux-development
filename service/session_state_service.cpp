#include "service/session_state_service.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "utils/text.h"

namespace prts::service {

SessionStateService::SessionStateService(std::string statePath) : path_(std::move(statePath)) {}

std::string SessionStateService::defaultStatePath() {
    const char* stateHome = std::getenv("XDG_STATE_HOME");
    if (stateHome != nullptr && stateHome[0] != '\0') {
        return std::string(stateHome) + "/prts/session.state";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.local/state/prts/session.state";
    }
    return "/tmp/prts-session.state";
}

SessionStateService::State SessionStateService::read() const {
    State state;
    std::ifstream stream(path_);
    if (!stream.good()) {
        return state;
    }
    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = utils::Text::trim(line.substr(0, equals));
        const std::string value = utils::Text::trim(line.substr(equals + 1));
        if (key == "last_login") {
            state.lastLogin = value;
        } else if (key == "login_count") {
            state.loginCount = static_cast<std::uint64_t>(std::strtoull(value.c_str(), nullptr, 10));
        }
    }
    return state;
}

void SessionStateService::record(const std::string& timestamp) const {
    const State previous = read();
    std::error_code error;
    const std::filesystem::path target(path_);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), error);
    }
    std::ofstream stream(path_, std::ios::trunc);
    if (!stream.good()) {
        return;
    }
    stream << "last_login=" << timestamp << '\n';
    stream << "login_count=" << (previous.loginCount + 1) << '\n';
}

}  // namespace prts::service
