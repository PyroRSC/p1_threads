/**
 * Variant 3 — Print immediately + threads split the divisibility test.
 *
 * Candidates 2..y are checked one at a time (linear search). For each
 * candidate n, the divisors [2, sqrt(n)] are divided among x threads; each
 * thread tests its share and stops early if any thread finds a divisor.
 * The last thread to finish checking a prime prints it immediately with its
 * thread id and a timestamp. All x threads are joined before moving on to
 * the next candidate, so threads are created and joined once per number.
 */
#include "utils.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>

namespace {

using namespace prime_search;

std::mutex g_outputMutex;

void printLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::cout << line << '\n';
}

/// Shared state for the x threads cooperating on one candidate.
struct CandidateCheck {
    CandidateCheck(Number value, unsigned threadCount) : candidate(value), remaining(threadCount) {}

    const Number candidate;
    std::atomic<bool> divisorFound{false};
    std::atomic<unsigned> remaining;  ///< threads that have not finished yet
};

void testDivisors(unsigned threadId, Range divisors, CandidateCheck& check) {
    for (Number d = divisors.begin; d < divisors.end; ++d) {
        if (check.divisorFound.load(std::memory_order_relaxed)) {
            break;  // another thread already proved n composite
        }
        if (check.candidate % d == 0) {
            check.divisorFound.store(true);
            break;
        }
    }
    // The thread that finishes last knows the final verdict and reports it.
    const bool isLastToFinish = check.remaining.fetch_sub(1) == 1;
    if (isLastToFinish && !check.divisorFound.load()) {
        printLine("[" + timestamp() + "] Thread " + std::to_string(threadId) + " found prime " +
                  std::to_string(check.candidate));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Config config = loadConfig(argc > 1 ? argv[1] : "config.txt");
        printRunStart("Variant 3: Print immediately | Threads split divisibility test", config);
        const Stopwatch stopwatch;

        std::size_t primeCount = 0;
        // 1 is not prime, so the linear search starts at 2.
        for (Number n = 2; n <= config.limit; ++n) {
            const Range divisors{2, integerSqrt(n) + 1};
            CandidateCheck check(n, config.threadCount);

            ThreadGroup workers;
            workers.reserve(config.threadCount);
            for (unsigned i = 0; i < config.threadCount; ++i) {
                workers.spawn(testDivisors, i + 1, splitRange(divisors, config.threadCount, i),
                              std::ref(check));
            }
            workers.joinAll();

            if (!check.divisorFound.load()) {
                ++primeCount;
            }
        }

        printRunEnd(primeCount, stopwatch.elapsedMs());
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
