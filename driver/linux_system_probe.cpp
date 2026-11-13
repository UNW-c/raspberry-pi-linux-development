#include "driver/linux_system_probe.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "utils/text.h"

namespace prts::driver {
namespace {

std::string readFirstLine(const std::string& path) {
    std::ifstream stream(path);
    std::string line;
    if (stream.good() && std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == '\0')) {
            line.pop_back();
        }
        return line;
    }
    return std::string();
}

std::string readWholeFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.good()) {
        return std::string();
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string content = buffer.str();
    content.erase(std::remove(content.begin(), content.end(), '\0'), content.end());
    return content;
}

bool parseDouble(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        return false;
    }
    value = parsed;
    return true;
}

// /proc/net/route stores the gateway as a little endian hex address.
std::string defaultGatewayAddress() {
    std::ifstream stream("/proc/net/route");
    std::string line;
    std::getline(stream, line);  // header
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        std::string interfaceName;
        std::string destination;
        std::string gateway;
        if (!(fields >> interfaceName >> destination >> gateway)) {
            continue;
        }
        if (destination != "00000000") {
            continue;
        }
        unsigned long value = 0;
        if (std::sscanf(gateway.c_str(), "%lx", &value) != 1) {
            continue;
        }
        const auto octet1 = static_cast<unsigned>(value & 0xFFUL);
        const auto octet2 = static_cast<unsigned>((value >> 8) & 0xFFUL);
        const auto octet3 = static_cast<unsigned>((value >> 16) & 0xFFUL);
        const auto octet4 = static_cast<unsigned>((value >> 24) & 0xFFUL);
        char text[32];
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", octet1, octet2, octet3, octet4);
        return std::string(text);
    }
    return std::string();
}

struct SshdCounts {
    int total{0};
    int sessions{0};
};

// Counts sshd processes. Children of PID 1 are the listener; anything else is a
// live session, which is exactly what a login banner wants to report.
SshdCounts countSshdProcesses() {
    SshdCounts counts;
    std::error_code error;
    const std::filesystem::path procRoot("/proc");
    if (!std::filesystem::exists(procRoot, error)) {
        return counts;
    }
    for (const auto& entry : std::filesystem::directory_iterator(procRoot, error)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        const std::string comm = readFirstLine(entry.path().string() + "/comm");
        if (comm != "sshd") {
            continue;
        }
        ++counts.total;
        const std::string statLine = readWholeFile(entry.path().string() + "/stat");
        const std::size_t closing = statLine.rfind(')');
        if (closing == std::string::npos || closing + 2 >= statLine.size()) {
            continue;
        }
        std::istringstream tail(statLine.substr(closing + 2));
        std::string state;
        long parentPid = 0;
        if (!(tail >> state >> parentPid)) {
            continue;
        }
        if (parentPid != 1) {
            ++counts.sessions;
        }
    }
    return counts;
}

}  // namespace

void LinuxSystemProbe::collect(model::ProbeDomain domain, model::SystemFacts& facts) const {
    switch (domain) {
        case model::ProbeDomain::Kernel:
            collectKernel(facts);
            break;
        case model::ProbeDomain::Host:
            collectHost(facts);
            break;
        case model::ProbeDomain::Cpu:
            collectCpu(facts);
            break;
        case model::ProbeDomain::Memory:
            collectMemory(facts);
            break;
        case model::ProbeDomain::Storage:
            collectStorage(facts);
            break;
        case model::ProbeDomain::Network:
            collectNetwork(facts);
            break;
        case model::ProbeDomain::Session:
            collectSession(facts);
            break;
    }
}

void LinuxSystemProbe::collectKernel(model::SystemFacts& facts) const {
    utsname info{};
    if (::uname(&info) == 0) {
        facts.kernelRelease = info.release;
        facts.kernelVersion = info.version;
        facts.machine = info.machine;
    }
}

void LinuxSystemProbe::collectHost(model::SystemFacts& facts) const {
    char hostname[256] = {0};
    if (::gethostname(hostname, sizeof(hostname) - 1) == 0) {
        facts.hostname = hostname;
    }
    const std::string model = utils::Text::trim(readWholeFile("/proc/device-tree/model"));
    if (!model.empty()) {
        facts.boardModel = model;
    }
}

void LinuxSystemProbe::collectCpu(model::SystemFacts& facts) const {
    std::ifstream stream("/proc/cpuinfo");
    std::string line;
    while (std::getline(stream, line)) {
        if (utils::Text::startsWith(line, "model name") || utils::Text::startsWith(line, "Processor") ||
            utils::Text::startsWith(line, "Hardware")) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                facts.cpuModel = utils::Text::trim(line.substr(colon + 1));
            }
            if (utils::Text::startsWith(line, "model name")) {
                break;
            }
        }
    }
    if (facts.cpuModel.empty() || facts.cpuModel == "unknown") {
        facts.cpuModel = "ARM";
    }

    const long cores = ::sysconf(_SC_NPROCESSORS_ONLN);
    facts.cpuCores = cores > 0 ? static_cast<int>(cores) : 0;

    for (const char* path : {"/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",
                             "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq"}) {
        double kiloHertz = 0.0;
        if (parseDouble(utils::Text::trim(readFirstLine(path)), kiloHertz) && kiloHertz > 0.0) {
            facts.cpuMhz = kiloHertz / 1000.0;
            break;
        }
    }

    double milliCelsius = 0.0;
    if (parseDouble(utils::Text::trim(readFirstLine("/sys/class/thermal/thermal_zone0/temp")),
                    milliCelsius)) {
        facts.cpuTemperatureC = milliCelsius / 1000.0;
    }

    std::ifstream loads("/proc/loadavg");
    if (loads.good()) {
        loads >> facts.loadAverage1 >> facts.loadAverage5 >> facts.loadAverage15;
    }
}

void LinuxSystemProbe::collectMemory(model::SystemFacts& facts) const {
    std::ifstream stream("/proc/meminfo");
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    while (stream >> key >> value >> unit) {
        if (key == "MemTotal:") {
            facts.memoryTotalKb = value;
        } else if (key == "MemAvailable:") {
            facts.memoryAvailableKb = value;
        }
        if (facts.memoryTotalKb != 0 && facts.memoryAvailableKb != 0) {
            break;
        }
    }
}

void LinuxSystemProbe::collectStorage(model::SystemFacts& facts) const {
    struct statvfs info {};
    if (::statvfs("/", &info) == 0) {
        facts.rootMount = "/";
        const std::uint64_t blockSize = info.f_frsize != 0 ? info.f_frsize : info.f_bsize;
        facts.diskTotalBytes = static_cast<std::uint64_t>(info.f_blocks) * blockSize;
        facts.diskFreeBytes = static_cast<std::uint64_t>(info.f_bavail) * blockSize;
    }
}

void LinuxSystemProbe::collectNetwork(model::SystemFacts& facts) const {
    ifaddrs* addresses = nullptr;
    if (::getifaddrs(&addresses) == 0) {
        for (ifaddrs* entry = addresses; entry != nullptr; entry = entry->ifa_next) {
            if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            if ((entry->ifa_flags & IFF_LOOPBACK) != 0) {
                continue;
            }
            char buffer[INET_ADDRSTRLEN] = {0};
            const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            if (::inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof(buffer)) != nullptr) {
                facts.networkInterface = entry->ifa_name;
                facts.ipv4Address = buffer;
                break;
            }
        }
        ::freeifaddrs(addresses);
    }
    facts.gatewayAddress = defaultGatewayAddress();
}

void LinuxSystemProbe::collectSession(model::SystemFacts& facts) const {
    double uptime = 0.0;
    std::ifstream stream("/proc/uptime");
    if (stream.good() && (stream >> uptime)) {
        facts.uptimeSeconds = uptime;
    }

    const SshdCounts counts = countSshdProcesses();
    facts.sshSessionCount = counts.sessions > 0 ? counts.sessions : (counts.total > 0 ? 1 : 0);
    facts.sshServiceState = counts.total > 0 ? "ACTIVE" : "UNKNOWN";
}

}  // namespace prts::driver
