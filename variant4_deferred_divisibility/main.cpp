/**
 * Variant 4 — Wait for all threads, then print + threads split the
 * divisibility test.
 *
 * Candidates 2..y are checked one at a time (linear search). For each
 * candidate n, the divisors [2, sqrt(n)] are divided among x threads; each
 * thread tests its share and stops early if any thread finds a divisor.
 * The main thread joins all x threads, records the verdict, and moves on.
 * Nothing is printed until the whole search is complete.
 */
#include "utils.hpp"

#include <atomic>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace prime_search;

/// Shared state for the x threads cooperating on one candidate.
struct CandidateCheck {
    CandidateCheck(Number value, unsigned threadCount) : candidate(value), remaining(threadCount) {}

    const Number candidate;
    std::atomic<bool> divisorFound{false};
    std::atomic<unsigned> remaining;  ///< threads that have not finished yet
    unsigned confirmedBy = 0;         ///< written only by the last thread to finish
};

struct FoundPrime {
    Number value;
    unsigned confirmedBy;
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
    // Record which thread delivered the final verdict (read after join).
    if (check.remaining.fetch_sub(1) == 1) {
        check.confirmedBy = threadId;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Config config = loadConfig(argc > 1 ? argv[1] : "config.txt");
        printRunStart("Variant 4: Print after all threads finish | Threads split divisibility test",
                      config);
        const Stopwatch stopwatch;

        std::vector<FoundPrime> primes;
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
                primes.push_back({n, check.confirmedBy});
            }
        }
        const double searchMs = stopwatch.elapsedMs();

        for (const auto& [value, confirmedBy] : primes) {
            std::cout << "Thread " << confirmedBy << " found prime " << value << '\n';
        }

        std::cout << " Search time (until all threads joined): " << std::fixed
                  << std::setprecision(3) << searchMs << " ms\n";
        printRunEnd(primes.size(), stopwatch.elapsedMs());
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
