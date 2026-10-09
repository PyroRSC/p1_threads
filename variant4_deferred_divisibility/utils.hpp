/**
 * @file utils.hpp
 * @brief Shared helpers for the prime-search threading variants:
 *        config loading, timestamps, range splitting, primality, and an
 *        RAII thread group. Identical copy lives in every variant folder so
 *        each folder builds on its own.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace prime_search {

using Number = std::uint64_t;

/// Thrown when the config file is missing or malformed.
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Run parameters read from the config file.
struct Config {
    unsigned threadCount = 0;  ///< x: number of worker threads
    Number limit = 0;          ///< y: search for primes in [1, y]
};

/// Half-open range [begin, end).
struct Range {
    Number begin = 0;
    Number end = 0;

    [[nodiscard]] bool empty() const noexcept { return begin >= end; }
    [[nodiscard]] Number size() const noexcept { return empty() ? 0 : end - begin; }
};

namespace detail {

inline std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view WHITESPACE = " \t\r\n";
    const auto first = text.find_first_not_of(WHITESPACE);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(WHITESPACE);
    return text.substr(first, last - first + 1);
}

inline Number parsePositive(std::string_view key, std::string_view value) {
    const bool allDigits = !value.empty() &&
        std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (!allDigits) {
        throw ConfigError("'" + std::string(key) + "' must be a positive integer, got '" +
                          std::string(value) + "'");
    }
    try {
        return std::stoull(std::string(value));
    } catch (const std::out_of_range&) {
        throw ConfigError("'" + std::string(key) + "' is too large");
    }
}

}  // namespace detail

/**
 * @brief Loads `threads=<x>` and `limit=<y>` from a key=value file.
 *        Blank lines and lines starting with '#' are ignored.
 * @throws ConfigError on I/O failure, unknown keys, or invalid values.
 */
inline Config loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigError("cannot open config file '" + path + "'");
    }

    Config config;
    bool hasThreads = false;
    bool hasLimit = false;
    std::string line;
    int lineNumber = 0;

    while (std::getline(in, line)) {
        ++lineNumber;
        const auto content = detail::trim(line);
        if (content.empty() || content.front() == '#') {
            continue;
        }
        const auto equals = content.find('=');
        if (equals == std::string_view::npos) {
            throw ConfigError("line " + std::to_string(lineNumber) + ": expected key=value");
        }
        const auto key = detail::trim(content.substr(0, equals));
        const auto value = detail::trim(content.substr(equals + 1));

        if (key == "threads") {
            const Number threads = detail::parsePositive(key, value);
            if (threads == 0 || threads > 100000) {
                throw ConfigError("'threads' must be between 1 and 100000");
            }
            config.threadCount = static_cast<unsigned>(threads);
            hasThreads = true;
        } else if (key == "limit") {
            config.limit = detail::parsePositive(key, value);
            if (config.limit < 1) {
                throw ConfigError("'limit' must be at least 1");
            }
            hasLimit = true;
        } else {
            throw ConfigError("line " + std::to_string(lineNumber) + ": unknown key '" +
                              std::string(key) + "'");
        }
    }

    if (!hasThreads || !hasLimit) {
        throw ConfigError("config must define both 'threads' and 'limit'");
    }
    return config;
}

/// Wall-clock time formatted as "YYYY-MM-DD HH:MM:SS.microseconds".
inline std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto micros = duration_cast<microseconds>(now.time_since_epoch()) % 1'000'000;
    const std::time_t seconds = system_clock::to_time_t(now);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
        << std::setw(6) << micros.count();
    return out.str();
}

/**
 * @brief Splits `whole` into `parts` contiguous chunks whose sizes differ by
 *        at most one, and returns chunk `index`. Chunks may be empty when
 *        there are more parts than elements.
 */
inline Range splitRange(Range whole, unsigned parts, unsigned index) noexcept {
    const Number total = whole.size();
    const Number base = total / parts;
    const Number remainder = total % parts;
    const Number begin = whole.begin + index * base + std::min<Number>(index, remainder);
    const Number length = base + (index < remainder ? 1 : 0);
    return {begin, begin + length};
}

/// Largest r such that r * r <= n.
inline Number integerSqrt(Number n) noexcept {
    Number root = 0;
    Number bit = Number{1} << 62;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/// Trial division over every divisor in [2, sqrt(n)].
inline bool isPrime(Number n) noexcept {
    if (n < 2) {
        return false;
    }
    for (Number divisor = 2; divisor <= n / divisor; ++divisor) {
        if (n % divisor == 0) {
            return false;
        }
    }
    return true;
}

/// Measures elapsed time from construction.
class Stopwatch {
public:
    [[nodiscard]] double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - m_start).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_start = Clock::now();
};

/**
 * @brief Owns a set of std::threads and guarantees they are joined, even if
 *        an exception unwinds the stack before joinAll() is called.
 */
class ThreadGroup {
public:
    ThreadGroup() = default;
    ThreadGroup(const ThreadGroup&) = delete;
    ThreadGroup& operator=(const ThreadGroup&) = delete;

    ~ThreadGroup() {
        try {
            joinAll();
        } catch (...) {
            // Destructors must not throw; nothing sensible to do here.
        }
    }

    void reserve(std::size_t count) { m_threads.reserve(count); }

    template <typename Function, typename... Args>
    void spawn(Function&& function, Args&&... args) {
        m_threads.emplace_back(std::forward<Function>(function), std::forward<Args>(args)...);
    }

    void joinAll() {
        for (auto& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

private:
    std::vector<std::thread> m_threads;
};

inline void printRunStart(std::string_view variantName, const Config& config) {
    std::cout << "==============================================================\n"
              << " " << variantName << '\n'
              << " Threads: " << config.threadCount << " | Search range: 1 - " << config.limit
              << '\n'
              << " Run START: " << timestamp() << '\n'
              << "==============================================================\n";
}

inline void printRunEnd(std::size_t primeCount, double elapsedMs) {
    // Capture the end time before formatting anything else.
    const std::string endTime = timestamp();
    std::cout << "==============================================================\n"
              << " Run END  : " << endTime << '\n'
              << " Primes found: " << primeCount << '\n'
              << " Total elapsed: " << std::fixed << std::setprecision(3) << elapsedMs << " ms\n"
              << "==============================================================\n";
}

}  // namespace prime_search
