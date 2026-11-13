#include "service/selfcheck_service.h"

#include <string>
#include <utility>

#include "utils/text.h"

namespace prts::service {
namespace {

model::CheckResult makeResult(std::string id, std::string label, std::string value,
                              model::CheckStatus status, std::int64_t elapsedMs) {
    model::CheckResult result;
    result.id = std::move(id);
    result.label = std::move(label);
    result.value = std::move(value);
    result.status = status;
    result.elapsedMs = elapsedMs;
    return result;
}

std::string temperatureText(double celsius) {
    if (celsius < 0.0) {
        return "n/a";
    }
    return utils::Text::formatFloat(celsius, 1) + " C";
}

}  // namespace

SelfcheckService::SelfcheckService(const driver::ISystemProbe& probe, const driver::IClock& clock,
                                   utils::Logger& logger)
    : probe_(probe), clock_(clock), logger_(logger) {}

std::vector<model::CheckResult> SelfcheckService::run(model::SystemFacts& facts) const {
    std::vector<model::CheckResult> results;
    results.reserve(7);

    auto measure = [this](model::ProbeDomain domain, model::SystemFacts& target) {
        const std::int64_t started = clock_.steadyMillis();
        probe_.collect(domain, target);
        return clock_.steadyMillis() - started;
    };

    // Kernel ---------------------------------------------------------------
    std::int64_t elapsed = measure(model::ProbeDomain::Kernel, facts);
    results.push_back(makeResult("kernel", "KERNEL", "Linux " + facts.kernelRelease,
                                 model::CheckStatus::Ok, elapsed));

    // Host -----------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Host, facts);
    std::string hostValue = facts.hostname;
    if (!facts.boardModel.empty() && facts.boardModel != "unknown") {
        std::string board = facts.boardModel;
        static const std::string kBoardPrefix = "Raspberry ";
        if (utils::Text::startsWith(board, kBoardPrefix)) {
            board = board.substr(kBoardPrefix.size());
        }
        hostValue += "  ·  " + board;
    }
    results.push_back(makeResult("host", "HOST", hostValue, model::CheckStatus::Ok, elapsed));

    // CPU ------------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Cpu, facts);
    std::string cpuValue = std::to_string(facts.cpuCores) + " cores";
    if (facts.cpuMhz > 1.0) {
        cpuValue += " @ " + utils::Text::formatFloat(facts.cpuMhz, 0) + " MHz";
    }
    cpuValue += "  ·  " + temperatureText(facts.cpuTemperatureC);
    model::CheckStatus cpuStatus = model::CheckStatus::Ok;
    if (facts.cpuTemperatureC >= 80.0) {
        cpuStatus = model::CheckStatus::Fail;
    } else if (facts.cpuTemperatureC >= 70.0) {
        cpuStatus = model::CheckStatus::Warn;
    }
    results.push_back(makeResult("cpu", "CPU", cpuValue, cpuStatus, elapsed));

    // Memory ---------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Memory, facts);
    std::string memoryValue = utils::Text::formatKib(facts.memoryAvailableKb) + " free of " +
                              utils::Text::formatKib(facts.memoryTotalKb);
    model::CheckStatus memoryStatus = model::CheckStatus::Ok;
    if (facts.memoryAvailablePercent() < 8.0) {
        memoryStatus = model::CheckStatus::Fail;
    } else if (facts.memoryAvailablePercent() < 15.0) {
        memoryStatus = model::CheckStatus::Warn;
    }
    results.push_back(makeResult("memory", "MEMORY", memoryValue, memoryStatus, elapsed));

    // Storage --------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Storage, facts);
    std::string storageValue = facts.rootMount + " " +
                               utils::Text::formatBytes(facts.diskFreeBytes) + " free of " +
                               utils::Text::formatBytes(facts.diskTotalBytes);
    model::CheckStatus storageStatus = model::CheckStatus::Ok;
    if (facts.diskFreePercent() < 5.0) {
        storageStatus = model::CheckStatus::Fail;
    } else if (facts.diskFreePercent() < 15.0) {
        storageStatus = model::CheckStatus::Warn;
    }
    results.push_back(makeResult("storage", "STORAGE", storageValue, storageStatus, elapsed));

    // Network --------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Network, facts);
    const std::string networkValue = facts.networkInterface + " " + facts.ipv4Address;
    const model::CheckStatus networkStatus = facts.ipv4Address == "unknown"
                                                 ? model::CheckStatus::Warn
                                                 : model::CheckStatus::Ok;
    results.push_back(makeResult("network", "NETWORK", networkValue, networkStatus, elapsed));

    // Session --------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Session, facts);
    std::string sessionValue = "sshd " + facts.sshServiceState + "  ·  " +
                               std::to_string(facts.sshSessionCount) + " session";
    if (facts.sshSessionCount != 1) {
        sessionValue += "s";
    }
    const model::CheckStatus sessionStatus =
        facts.sshServiceState == "ACTIVE" ? model::CheckStatus::Ok : model::CheckStatus::Warn;
    results.push_back(makeResult("session", "SESSION", sessionValue, sessionStatus, elapsed));

    logger_.info("self-check completed: " + std::to_string(results.size()) + " domains probed");
    return results;
}

int SelfcheckService::countByStatus(const std::vector<model::CheckResult>& results,
                                    model::CheckStatus status) {
    int count = 0;
    for (const model::CheckResult& result : results) {
        if (result.status == status) {
            ++count;
        }
    }
    return count;
}

}  // namespace prts::service
