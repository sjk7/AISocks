// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once
#include <chrono>
#include <cstdio>
#include <string>

namespace aiSocks {

// Simple elapsed-time utility.
// Starts automatically on construction; call reset() to restart.
// If constructed with a message, prints "<message>: X.X ms" on destruction.
class Stopwatch {
    public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}
    explicit Stopwatch(std::string message)
        : start_(std::chrono::steady_clock::now())
        , message_(std::move(message)) {}

    ~Stopwatch() {
        if (!message_.empty())
            printf("%s: %.1f ms\n", message_.c_str(), elapsedMs());
    }

    void reset() { start_ = std::chrono::steady_clock::now(); }

    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_)
            .count();
    }

    double elapsedSec() const { return elapsedMs() / 1000.0; }

    private:
    std::chrono::steady_clock::time_point start_;
    std::string message_;
};

} // namespace aiSocks
