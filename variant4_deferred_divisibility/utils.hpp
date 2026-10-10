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
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
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

/// Largest accepted `threads` value.
inline constexpr Number MAX_THREADS = 100000;
/// Largest accepted `limit`. The search ranges are half-open [1, limit + 1),
/// so limit + 1 must not wrap around.
inline constexpr Number MAX_LIMIT = std::numeric_limits<Number>::max() - 1;
/// Config files larger than this are rejected before being read.
inline constexpr std::uintmax_t MAX_CONFIG_BYTES = 64 * 1024;

namespace detail {

inline std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view WHITESPACE = " \t\r\n\v\f";
    const auto first = text.find_first_not_of(WHITESPACE);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(WHITESPACE);
    return text.substr(first, last - first + 1);
}

/// `text` in quotes for an error message: control characters are escaped and
/// long text is cut short, so a bad file cannot flood or garble the terminal.
inline std::string quote(std::string_view text) {
    constexpr std::size_t MAX_SHOWN = 40;
    std::string out = "'";
    for (std::size_t i = 0; i < text.size() && i < MAX_SHOWN; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7F) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\x%02X", c);
            out += escaped;
        } else {
            out += static_cast<char>(c);
        }
    }
    if (text.size() > MAX_SHOWN) {
        out += "...";
    }
    return out + "'";
}

/// Thrown by parseNumber(); loadConfig() adds the file name and line number.
struct ValueError {
    std::string message;
};

/// Parses a whole number written with digits only (no sign, spaces, commas,
/// decimals or exponent) and checks it lies in [min, max].
inline Number parseNumber(std::string_view key, std::string_view value, Number min, Number max) {
    const std::string range =
        "must be a whole number from " + std::to_string(min) + " to " + std::to_string(max);
    Number number = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') {
            throw ValueError{std::string(key) + " " + range + ", not " + quote(value)};
        }
        const auto digit = static_cast<Number>(c - '0');
        if (number > (max - digit) / 10) {  // number * 10 + digit would exceed max
            throw ValueError{std::string(key) + " = " + quote(value) + " is too large (" + range + ")"};
        }
        number = number * 10 + digit;
    }
    if (number < min) {
        throw ValueError{std::string(key) + " " + range + ", not " + quote(value)};
    }
    return number;
}

/// Reads the whole file after checking it is a regular file of sensible size.
inline std::string readConfigFile(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (!fs::exists(status)) {
        throw ConfigError("cannot find config file " + quote(path));
    }
    if (fs::is_directory(status)) {
        throw ConfigError(quote(path) + " is a directory, not a config file");
    }
    if (!fs::is_regular_file(status)) {
        throw ConfigError(quote(path) + " is not a regular file");
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        throw ConfigError("cannot read config file " + quote(path) + ": " + error.message());
    }
    if (size > MAX_CONFIG_BYTES) {
        throw ConfigError("config file " + quote(path) + " is too large (" + std::to_string(size) +
                          " bytes, the maximum is " + std::to_string(MAX_CONFIG_BYTES) + ")");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ConfigError("cannot open config file " + quote(path));
    }
    // Read one byte past the limit in case the file grew after the size check.
    std::string contents(MAX_CONFIG_BYTES + 1, '\0');
    in.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (in.bad()) {
        throw ConfigError("error while reading config file " + quote(path));
    }
    contents.resize(static_cast<std::size_t>(in.gcount()));
    if (contents.size() > MAX_CONFIG_BYTES) {
        throw ConfigError("config file " + quote(path) + " is too large");
    }
    return contents;
}

}  // namespace detail

/**
 * @brief Returns the config file path from the command line: the first
 *        argument if given, otherwise "config.txt".
 * @throws ConfigError if there is more than one argument or it is empty.
 */
inline std::string configPathFromArgs(int argc, char* argv[]) {
    if (argc > 2) {
        throw ConfigError("too many arguments (usage: " + std::string(argv[0]) + " [config file])");
    }
    if (argc == 2) {
        if (argv[1][0] == '\0') {
            throw ConfigError("the config file path is empty");
        }
        return argv[1];
    }
    return "config.txt";
}

/**
 * @brief Loads `threads=<x>` and `limit=<y>` from a key=value file.
 *
 * Rules: each key must appear exactly once, in any order; keys are
 * case-sensitive; '#' starts a comment anywhere on a line; blank lines are
 * ignored; a UTF-8 byte order mark is allowed. Values are whole numbers
 * written with digits only: 1 <= threads <= MAX_THREADS and
 * 1 <= limit <= MAX_LIMIT.
 * @throws ConfigError naming the file and line on any problem.
 */
inline Config loadConfig(const std::string& path) {
    std::string contents = detail::readConfigFile(path);

    if (contents.find('\0') != std::string::npos) {
        throw ConfigError(detail::quote(path) +
                          " contains NUL bytes; it may be saved as UTF-16 (Windows "
                          "PowerShell's '>' does this). Save it as UTF-8 or ANSI instead.");
    }
    constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";
    if (std::string_view(contents).substr(0, UTF8_BOM.size()) == UTF8_BOM) {
        contents.erase(0, UTF8_BOM.size());
    }

    Config config;
    int threadsLine = 0;  // line each key was set on (0 = not set yet)
    int limitLine = 0;

    std::string_view rest = contents;
    for (int lineNumber = 1; !rest.empty(); ++lineNumber) {
        const auto newline = rest.find('\n');
        std::string_view line = rest.substr(0, newline);
        rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);

        const auto lineError = [&](const std::string& message) {
            return ConfigError(path + ", line " + std::to_string(lineNumber) + ": " + message);
        };

        line = detail::trim(line.substr(0, line.find('#')));
        if (line.empty()) {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            throw lineError("expected key=value, got " + detail::quote(line));
        }
        const auto key = detail::trim(line.substr(0, equals));
        const auto value = detail::trim(line.substr(equals + 1));

        int* setOn = nullptr;
        if (key == "threads") {
            setOn = &threadsLine;
        } else if (key == "limit") {
            setOn = &limitLine;
        } else if (key.empty()) {
            throw lineError("missing the key before '='");
        } else {
            throw lineError("unknown key " + detail::quote(key) + " (expected threads or limit)");
        }
        if (*setOn != 0) {
            throw lineError(std::string(key) + " is already set on line " + std::to_string(*setOn));
        }
        if (value.empty()) {
            throw lineError(std::string(key) + " has no value");
        }

        try {
            if (key == "threads") {
                config.threadCount =
                    static_cast<unsigned>(detail::parseNumber(key, value, 1, MAX_THREADS));
            } else {
                config.limit = detail::parseNumber(key, value, 1, MAX_LIMIT);
            }
        } catch (const detail::ValueError& error) {
            throw lineError(error.message);
        }
        *setOn = lineNumber;
    }

    std::string missing;
    if (threadsLine == 0) {
        missing += ", threads (number of worker threads)";
    }
    if (limitLine == 0) {
        missing += ", limit (search for primes from 1 to this number)";
    }
    if (!missing.empty()) {
        throw ConfigError(path + ": missing " + missing.substr(2));
    }
    return config;
}

/// `when` formatted as "YYYY-MM-DD HH:MM:SS.microseconds" in local time.
inline std::string formatTimestamp(std::chrono::system_clock::time_point when) {
    using namespace std::chrono;
    const auto micros = duration_cast<microseconds>(when.time_since_epoch()) % 1'000'000;
    const std::time_t seconds = system_clock::to_time_t(when);

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

/// Current wall-clock time, formatted by formatTimestamp().
inline std::string timestamp() {
    return formatTimestamp(std::chrono::system_clock::now());
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

/// Trial division over every divisor in [2, sqrt(n)]. The bound is computed
/// once, so each step costs one division, the same as in the divisibility variants.
inline bool isPrime(Number n) noexcept {
    if (n < 2) {
        return false;
    }
    const Number root = integerSqrt(n);
    for (Number divisor = 2; divisor <= root; ++divisor) {
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
 *
 * Spawned threads wait at a start gate until start() (or joinAll()) opens it,
 * so all of them begin together. If the group is destroyed before that, for
 * example because the system could not create every thread, the waiting
 * threads exit without running their work.
 */
class ThreadGroup {
public:
    ThreadGroup() = default;
    ThreadGroup(const ThreadGroup&) = delete;
    ThreadGroup& operator=(const ThreadGroup&) = delete;

    ~ThreadGroup() {
        try {
            setGate(Gate::Cancelled);
            joinThreads();
        } catch (...) {
            // Destructors must not throw; nothing sensible to do here.
        }
    }

    void reserve(std::size_t count) { m_threads.reserve(count); }

    template <typename Function, typename... Args>
    void spawn(Function&& function, Args&&... args) {
        m_threads.emplace_back(
            &ThreadGroup::runWhenStarted<std::decay_t<Function>, std::decay_t<Args>...>, this,
            std::forward<Function>(function), std::forward<Args>(args)...);
    }

    /// Releases every spawned thread at once.
    void start() { setGate(Gate::Open); }

    /// Starts the threads if start() has not been called, then joins them.
    void joinAll() {
        start();
        joinThreads();
    }

private:
    enum class Gate { Closed, Open, Cancelled };

    /// Moves the gate out of Closed; later calls have no effect.
    void setGate(Gate state) {
        {
            std::lock_guard<std::mutex> lock(m_gateMutex);
            if (m_gate != Gate::Closed) {
                return;
            }
            m_gate = state;
        }
        m_gateChanged.notify_all();
    }

    template <typename Function, typename... Args>
    void runWhenStarted(Function function, Args... args) {
        bool shouldRun = false;
        {
            std::unique_lock<std::mutex> lock(m_gateMutex);
            m_gateChanged.wait(lock, [this] { return m_gate != Gate::Closed; });
            shouldRun = m_gate == Gate::Open;
        }
        if (shouldRun) {
            std::invoke(std::move(function), std::move(args)...);
        }
    }

    void joinThreads() {
        for (auto& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    std::mutex m_gateMutex;
    std::condition_variable m_gateChanged;
    Gate m_gate = Gate::Closed;
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
