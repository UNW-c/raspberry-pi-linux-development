#pragma once

#include <cstdint>
#include <string>

#include "driver/i_clock.h"
#include "driver/i_system_probe.h"
#include "driver/i_terminal.h"
#include "model/system_facts.h"

namespace prts::test {

// Captures everything the animation writes so that tests can assert on the
// rendered text without a real terminal.
class FakeTerminal : public driver::ITerminal {
public:
    void write(std::string_view text) override { output.append(text); }
    void writeStyled(std::string_view text, const model::Color&, bool) override {
        output.append(text);
    }
    void clearScreen() override { output.clear(); }
    void moveUp(int) override {}
    void clearLine() override {}
    void setCursorVisible(bool) override {}
    void flush() override {}

    driver::TerminalSize size() const override { return size_; }
    bool isInteractive() const override { return interactive_; }
    bool supportsColor() const override { return color_; }

    int lineCount() const {
        int lines = 0;
        for (char character : output) {
            if (character == '\n') {
                ++lines;
            }
        }
        return lines;
    }

    bool contains(const std::string& needle) const {
        return output.find(needle) != std::string::npos;
    }

    std::string output;
    driver::TerminalSize size_{100, 40};
    bool interactive_{true};
    bool color_{true};
};

// Deterministic clock: sleeping advances virtual time instead of blocking.
class FakeClock : public driver::IClock {
public:
    std::int64_t steadyMillis() const override { return current_; }
    std::string wallClock(const std::string&) const override { return "2026-01-01 08:00:00"; }
    std::string timestampWithMillis() const override { return "08:00:00.000"; }
    void sleepMillis(int milliseconds) override { current_ += milliseconds; }

    std::int64_t current_{0};
};

// Probe with fixed, plausible values so tests never touch the host system.
class FakeProbe : public driver::ISystemProbe {
public:
    void collect(model::ProbeDomain domain, model::SystemFacts& facts) const override {
        switch (domain) {
            case model::ProbeDomain::Kernel:
                facts.kernelRelease = "6.18.50+rpt-rpi-v8";
                facts.kernelVersion = "#1 SMP PREEMPT";
                facts.machine = "aarch64";
                break;
            case model::ProbeDomain::Host:
                facts.hostname = "PRTS";
                facts.boardModel = "Raspberry Pi 3 Model B Rev 1.2";
                break;
            case model::ProbeDomain::Cpu:
                facts.cpuModel = "Cortex-A53";
                facts.cpuCores = 4;
                facts.cpuMhz = 1200.0;
                facts.cpuTemperatureC = 38.6;
                facts.loadAverage1 = 0.14;
                break;
            case model::ProbeDomain::Memory:
                facts.memoryTotalKb = 927232;
                facts.memoryAvailableKb = 766000;
                break;
            case model::ProbeDomain::Storage:
                facts.diskTotalBytes = 14ULL * 1024ULL * 1024ULL * 1024ULL;
                facts.diskFreeBytes = 11ULL * 1024ULL * 1024ULL * 1024ULL;
                break;
            case model::ProbeDomain::Network:
                facts.networkInterface = "eth0";
                facts.ipv4Address = "192.168.210.239";
                facts.gatewayAddress = "192.168.210.1";
                break;
            case model::ProbeDomain::Session:
                facts.sshServiceState = "ACTIVE";
                facts.sshSessionCount = 1;
                facts.uptimeSeconds = 93784.0;
                break;
        }
    }
};

std::string temporaryDirectory();
void clearTemporaryDirectory();

}  // namespace prts::test
