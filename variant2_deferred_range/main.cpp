/**
 * Variant 2 — Wait for all threads, then print + straight division of the
 * search range.
 *
 * The range [1, y] is cut into x contiguous chunks. Each thread writes the
 * primes it finds, and when it found each one, into its own private result
 * list (no locking needed).
 * Nothing is printed until every thread has been joined; the main thread
 * then prints all results in order.
 */
#include "utils.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace prime_search;

struct FoundPrime {
    Number value;
    std::chrono::system_clock::time_point foundAt;
};

/// Primes found by one thread, plus when it started and finished.
struct ThreadResult {
    Range assigned;
    std::vector<FoundPrime> primes;
    std::string startTime;
    std::string endTime;
};

void searchRange(Range range, ThreadResult& result) {
    result.startTime = timestamp();
    for (Number n = range.begin; n < range.end; ++n) {
        if (isPrime(n)) {
            result.primes.push_back({n, std::chrono::system_clock::now()});
        }
    }
    result.endTime = timestamp();
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Config config = loadConfig(configPathFromArgs(argc, argv));
        printRunStart("Variant 2: Print after all threads finish | Straight range division",
                      config);
        const Stopwatch stopwatch;

        const Range whole{1, config.limit + 1};
        // Sized up front so no reallocation happens while threads hold references.
        std::vector<ThreadResult> results(config.threadCount);

        ThreadGroup workers;
        workers.reserve(config.threadCount);
        for (unsigned i = 0; i < config.threadCount; ++i) {
            results[i].assigned = splitRange(whole, config.threadCount, i);
            workers.spawn(searchRange, results[i].assigned, std::ref(results[i]));
        }
        workers.joinAll();
        const double searchMs = stopwatch.elapsedMs();

        std::size_t primeCount = 0;
        for (std::size_t i = 0; i < results.size(); ++i) {
            const ThreadResult& result = results[i];
            const std::string threadTag = "Thread " + std::to_string(i + 1);
            if (result.assigned.empty()) {
                std::cout << threadTag << " assigned: (nothing)\n";
                continue;
            }
            std::cout << threadTag << " assigned: " << result.assigned.begin << " - "
                      << result.assigned.end - 1 << " | started [" << result.startTime
                      << "] finished [" << result.endTime << "]\n";
            for (const auto& [value, foundAt] : result.primes) {
                std::cout << "  [" << formatTimestamp(foundAt) << "] " << threadTag
                          << " found prime " << value << '\n';
            }
            primeCount += result.primes.size();
        }

        std::cout << " Search time (until all threads joined): " << std::fixed
                  << std::setprecision(3) << searchMs << " ms\n";
        printRunEnd(primeCount, stopwatch.elapsedMs());
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
