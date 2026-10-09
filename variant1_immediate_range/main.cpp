/**
 * Variant 1 — Print immediately + straight division of the search range.
 *
 * The range [1, y] is cut into x contiguous chunks (e.g. 1-250, 251-500, ...
 * for y = 1000, x = 4). Each thread tests every number in its own chunk and
 * prints each prime the moment it is found, tagged with its thread id and a
 * timestamp. Output from different threads interleaves.
 */
#include "utils.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>

namespace {

using namespace prime_search;

// Each line is written under this lock so lines from different threads
// interleave with each other but are never torn mid-line.
std::mutex g_outputMutex;

void printLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::cout << line << '\n';
}

void searchRange(unsigned threadId, Range range, std::atomic<std::size_t>& primeCount) {
    std::size_t localCount = 0;
    for (Number n = range.begin; n < range.end; ++n) {
        if (!isPrime(n)) {
            continue;
        }
        ++localCount;
        printLine("[" + timestamp() + "] Thread " + std::to_string(threadId) + " found prime " +
                  std::to_string(n));
    }
    primeCount += localCount;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Config config = loadConfig(argc > 1 ? argv[1] : "config.txt");
        printRunStart("Variant 1: Print immediately | Straight range division", config);
        const Stopwatch stopwatch;

        const Range whole{1, config.limit + 1};
        std::atomic<std::size_t> primeCount{0};

        ThreadGroup workers;
        workers.reserve(config.threadCount);
        for (unsigned i = 0; i < config.threadCount; ++i) {
            const Range chunk = splitRange(whole, config.threadCount, i);
            const unsigned threadId = i + 1;
            if (chunk.empty()) {
                printLine("Thread " + std::to_string(threadId) + " assigned: (nothing)");
            } else {
                printLine("Thread " + std::to_string(threadId) + " assigned: " +
                          std::to_string(chunk.begin) + " - " + std::to_string(chunk.end - 1));
            }
            workers.spawn(searchRange, threadId, chunk, std::ref(primeCount));
        }
        workers.joinAll();

        printRunEnd(primeCount.load(), stopwatch.elapsedMs());
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
