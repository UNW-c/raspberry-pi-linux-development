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

std::string shortBoardName(const std::string& board) {
    static const std::string kPrefix = "Raspberry ";
    if (utils::Text::startsWith(board, kPrefix)) {
        return board.substr(kPrefix.size());
    }
    return board;
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
        hostValue += " \u00b7 " + shortBoardName(facts.boardModel);
    }
    results.push_back(makeResult("host", "HOST", hostValue, model::CheckStatus::Ok, elapsed));

    // Core -----------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Cpu, facts);
    std::string coreValue = std::to_string(facts.cpuCores) + " threads";
    if (facts.cpuMhz > 1.0) {
        coreValue += " \u00b7 " + utils::Text::formatFloat(facts.cpuMhz, 0) + " MHz";
    }
    coreValue += " \u00b7 " + temperatureText(facts.cpuTemperatureC);
    model::CheckStatus coreStatus = model::CheckStatus::Ok;
    if (facts.cpuTemperatureC >= 80.0) {
        coreStatus = model::CheckStatus::Fail;
    } else if (facts.cpuTemperatureC >= 70.0) {
        coreStatus = model::CheckStatus::Warn;
    }
    results.push_back(makeResult("core", "CORE", coreValue, coreStatus, elapsed));

    // Memory ---------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Memory, facts);
    const std::string memoryValue = utils::Text::formatKib(facts.memoryAvailableKb) +
                                    " free / " + utils::Text::formatKib(facts.memoryTotalKb);
    model::CheckStatus memoryStatus = model::CheckStatus::Ok;
    if (facts.memoryAvailablePercent() < 8.0) {
        memoryStatus = model::CheckStatus::Fail;
    } else if (facts.memoryAvailablePercent() < 15.0) {
        memoryStatus = model::CheckStatus::Warn;
    }
    results.push_back(makeResult("memory", "MEMORY", memoryValue, memoryStatus, elapsed));

    // Archive --------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Storage, facts);
    const std::string archiveValue = utils::Text::formatBytes(facts.diskFreeBytes) + " free / " +
                                     utils::Text::formatBytes(facts.diskTotalBytes);
    model::CheckStatus archiveStatus = model::CheckStatus::Ok;
    if (facts.diskFreePercent() < 5.0) {
        archiveStatus = model::CheckStatus::Fail;
    } else if (facts.diskFreePercent() < 15.0) {
        archiveStatus = model::CheckStatus::Warn;
    }
    results.push_back(makeResult("archive", "ARCHIVE", archiveValue, archiveStatus, elapsed));

    // Link -----------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Network, facts);
    const std::string linkValue = facts.networkInterface + " \u00b7 " + facts.ipv4Address;
    const model::CheckStatus linkStatus = facts.ipv4Address == "unknown"
                                              ? model::CheckStatus::Warn
                                              : model::CheckStatus::Ok;
    results.push_back(makeResult("link", "LINK", linkValue, linkStatus, elapsed));

    // Access ---------------------------------------------------------------
    elapsed = measure(model::ProbeDomain::Session, facts);
    std::string accessValue = "sshd " + utils::Text::lower(facts.sshServiceState);
    if (accessValue == "sshd active") {
        accessValue = "sshd online";
    }
    accessValue += " \u00b7 " + std::to_string(facts.sshSessionCount) + " session";
    if (facts.sshSessionCount != 1) {
        accessValue += "s";
    }
    const model::CheckStatus accessStatus =
        facts.sshServiceState == "ACTIVE" ? model::CheckStatus::Ok : model::CheckStatus::Warn;
    results.push_back(makeResult("access", "ACCESS", accessValue, accessStatus, elapsed));

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
