/**
 * Variant 3 — Print immediately + threads split the divisibility test.
 *
 * Candidates 2..y are checked one at a time (linear search). For each
 * candidate n, the divisors [2, sqrt(n)] are divided among x threads; each
 * thread tests its share and stops early if any thread finds a divisor.
 * The x threads are created once and reused for every candidate: a barrier
 * makes all of them finish n before any starts n + 1. The barrier's
 * completion step prints n immediately if it is prime, tagged with the id
 * of the last thread to finish and a timestamp, then moves on to n + 1.
 */
#include "utils.hpp"

#include <atomic>
#include <barrier>
#include <iostream>
#include <string>

namespace {

using namespace prime_search;

/// State shared by the x threads. `candidate`, `done` and `primeCount` are
/// changed only in the barrier's completion step, while every thread waits.
struct SharedSearch {
    SharedSearch(Number searchLimit, unsigned threads)
        : limit(searchLimit), threadCount(threads), remaining(threads) {}

    const Number limit;
    const unsigned threadCount;
    // 1 is not prime, so the linear search starts at 2.
    Number candidate = 2;
    bool done = limit < candidate;
    std::size_t primeCount = 0;
    std::atomic<bool> divisorFound{false};
    std::atomic<unsigned> remaining;  ///< threads that have not finished this candidate
    unsigned lastToFinish = 0;        ///< written only by the last thread to finish
};

/// Barrier completion step: runs once per candidate, after every thread arrives.
struct FinishCandidate {
    SharedSearch* search;

    void operator()() noexcept {
        SharedSearch& s = *search;
        if (!s.divisorFound.load()) {
            ++s.primeCount;
            // Only this step prints, and the other threads are blocked, so no lock is needed.
            std::cout << "[" << timestamp() << "] Thread " << s.lastToFinish << " found prime "
                      << s.candidate << '\n';
        }
        s.divisorFound.store(false);
        s.remaining.store(s.threadCount);
        if (s.candidate == s.limit) {
            s.done = true;
        } else {
            ++s.candidate;
        }
    }
};

using CandidateBarrier = std::barrier<FinishCandidate>;

void testDivisors(unsigned threadId, SharedSearch& search, CandidateBarrier& barrier) {
    while (!search.done) {
        const Number n = search.candidate;
        const Range divisors =
            splitRange({2, integerSqrt(n) + 1}, search.threadCount, threadId - 1);
        for (Number d = divisors.begin; d < divisors.end; ++d) {
            if (search.divisorFound.load(std::memory_order_relaxed)) {
                break;  // another thread already proved n composite
            }
            if (n % d == 0) {
                search.divisorFound.store(true);
                break;
            }
        }
        // The thread that finishes last is credited with the verdict.
        if (search.remaining.fetch_sub(1) == 1) {
            search.lastToFinish = threadId;
        }
        barrier.arrive_and_wait();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Config config = loadConfig(configPathFromArgs(argc, argv));
        printRunStart("Variant 3: Print immediately | Threads split divisibility test", config);
        const Stopwatch stopwatch;

        SharedSearch search(config.limit, config.threadCount);
        CandidateBarrier barrier(config.threadCount, FinishCandidate{&search});

        ThreadGroup workers;
        workers.reserve(config.threadCount);
        for (unsigned i = 0; i < config.threadCount; ++i) {
            workers.spawn(testDivisors, i + 1, std::ref(search), std::ref(barrier));
        }
        workers.joinAll();

        printRunEnd(search.primeCount, stopwatch.elapsedMs());
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
