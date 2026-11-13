#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prts::model {

// Domains a probe can be asked for. Keeping them beside the fact structure
// makes it obvious which fields belong to which probe.
enum class ProbeDomain {
    Kernel,
    Host,
    Cpu,
    Memory,
    Storage,
    Network,
    Session,
};

// Snapshot of the machine the boot sequence reports on. One field group per
// probe domain so that driver implementations only fill in what they can see.
struct SystemFacts {
    // Kernel / host
    std::string kernelRelease{"unknown"};
    std::string kernelVersion{"unknown"};
    std::string machine{"unknown"};
    std::string hostname{"unknown"};
    std::string boardModel{"unknown"};

    // CPU
    std::string cpuModel{"unknown"};
    int cpuCores{0};
    double cpuMhz{0.0};
    double cpuTemperatureC{-1.0};
    double loadAverage1{0.0};
    double loadAverage5{0.0};
    double loadAverage15{0.0};

    // Memory (kibibytes)
    std::uint64_t memoryTotalKb{0};
    std::uint64_t memoryAvailableKb{0};

    // Storage
    std::string rootMount{"/"};
    std::uint64_t diskTotalBytes{0};
    std::uint64_t diskFreeBytes{0};

    // Network
    std::string networkInterface{"unknown"};
    std::string ipv4Address{"unknown"};
    std::string gatewayAddress{"unknown"};

    // Session / security
    int sshSessionCount{0};
    std::string sshServiceState{"unknown"};
    double uptimeSeconds{0.0};

    double memoryUsedPercent() const {
        if (memoryTotalKb == 0) {
            return 0.0;
        }
        return 100.0 * static_cast<double>(memoryTotalKb - memoryAvailableKb) /
               static_cast<double>(memoryTotalKb);
    }

    double memoryAvailablePercent() const {
        if (memoryTotalKb == 0) {
            return 0.0;
        }
        return 100.0 * static_cast<double>(memoryAvailableKb) /
               static_cast<double>(memoryTotalKb);
    }

    double diskUsedPercent() const {
        if (diskTotalBytes == 0) {
            return 0.0;
        }
        return 100.0 * static_cast<double>(diskTotalBytes - diskFreeBytes) /
               static_cast<double>(diskTotalBytes);
    }

    double diskFreePercent() const {
        if (diskTotalBytes == 0) {
            return 0.0;
        }
        return 100.0 * static_cast<double>(diskFreeBytes) /
               static_cast<double>(diskTotalBytes);
    }
};

}  // namespace prts::model
