# Speaker notes: Threaded Prime Search

One section per slide, in deck order. Each section is roughly 30–60 seconds spoken. **Key point** is the one sentence the audience should remember if they remember nothing else.

---

## 1. Threaded Prime Search (cover)

This project finds every prime from 1 to y using x threads, where both numbers come from a config file. We built it four ways, and the question we answer is simple: when do more threads actually make the program faster?

Short answer before we start: sometimes a lot faster, sometimes hundreds of times slower, and the difference comes down to how the work is split.

---

## 2. Prime search with x threads, four ways

The four variants come from two independent choices.

- **Rows: how the work is divided.** Either each thread gets its own slice of the range 1 to y (straight division), or all threads work on one number together, splitting that number's divisors (divisibility testing).
- **Columns: when output is printed.** Either each prime is printed the moment it's found, with a thread id and timestamp, or everything is printed after all threads are joined.

Two choices times two options gives V1 through V4. The colors on this slide (blue, orange, green, yellow) stay the same for each variant on every chart that follows.

**Key point:** all four use the identical primality test, trial division up to √n. So any timing difference comes from the threading design, not from the math.

---

## 3. Straight division: independent blocks, one join

With y = 1,000 and 4 threads, thread 1 gets 1–250, thread 2 gets 251–500, and so on. When y doesn't split evenly, the first blocks get one extra number.

What makes this design fast is that **the threads share nothing while they search**. Each thread has its own block, and in V2, its own result list, so the search needs no locks at all.

- V1 needs exactly one lock, for printing. Each line is fully built first, then written while holding a mutex. That's why lines from different threads can mix, but a single line is never cut in half.
- All threads wait at a start gate until every thread exists, so they begin together. At the end, the main thread joins all x threads. The run is only as fast as the slowest block, which comes back on slide 9.

---

## 4. Divisibility testing: threads share each number

Here the search goes through numbers one at a time. For each number n, its candidate divisors from 2 to √n are split across the x threads. When every thread has finished its share, they all meet at a barrier, and only then does anyone move to n + 1.

Three implementation details matter:

1. **Threads are created once and reused.** Our first version created and joined x brand-new threads for every single number. At 16 threads and y = 200,000, that took 22 seconds. Reusing the threads with a barrier took 0.45 seconds. Creating a thread is far more expensive than syncing one.
2. **Early stop.** A shared atomic flag lets every thread stop as soon as any thread finds a divisor. No point testing the rest once we know n is composite.
3. **The last thread to finish reports.** The barrier runs a small completion step once per number, on the last thread to arrive. That step decides prime or not, prints it (V3) or records it (V4), and advances to the next number.

---

## 5. How the runs were measured

Quick context so the numbers are trustworthy.

- The machine has 8 cores with 2 hardware threads each, so 16 logical CPUs. Remember **16**: it's where every curve bends.
- Thread counts double from 1 to 128, so we deliberately go past the hardware limit to see what happens.
- y goes from 10 thousand to 10 million. The divisibility variants stop at 1 million, because one run at 10 million with 128 threads would take minutes.
- Every config ran 3 to 5 times and we report the median. Repeats agreed within about 7%.

One important choice: **output was redirected to a file.** Printing to a terminal is so slow that it would hide the threading effects we're trying to measure. That matters for the live demo, which prints to a terminal.

---

## 6. The result, at 128 threads vs. 1

This is the whole talk in one slide.

Straight division with 128 threads runs **8.2 times faster** than with one thread. Divisibility testing with 128 threads runs **265 times slower** than with one thread.

Same machine, same primality test, same primes found. Only the division of work changed. The rest of the talk explains why.

---

## 7. More threads help one scheme and hurt the other

How to read this chart: thread count runs along the bottom, doubling each step. Run time is on the left, on a **log scale**, so each gridline is a multiplication, not an addition. This is y = 1 million.

- The blue and orange lines (straight division) go **down** as threads are added, from about 125 ms at one thread to 24 ms for V1 at 16 threads. Then they go flat.
- The green and yellow lines (divisibility testing) go **up** from the very first extra thread: 141 ms on one thread, about 2.3 seconds at 16, and 37 seconds at 128.

Watch what happens at 16 threads, this machine's logical CPU count. Both groups change behavior there. That's the next slide.

**Key point:** at one thread, all four variants take about the same time. The gap is created entirely by adding threads.

---

## 8. Gains stop at the CPU's 16 hardware threads

This is speedup, meaning time on one thread divided by time on x threads, for y = 10 million. The gray diagonal is perfect scaling: twice the threads, twice the speed.

- V1 tracks the perfect line early, reaches **7.5× at 16 threads**, and then stops. The remaining 112 threads add only 0.7× more. Once all 16 hardware threads are busy, extra threads just take turns on the same CPUs.
- Why not a full 16×? Two reasons. The 16 logical CPUs are only 8 physical cores, and pairs share execution units. And the blocks aren't equally hard, which is the next slide.
- V2 levels off lower, at 4.8×. Its search scales like V1's, but its printing happens on one thread after the join, and no number of threads can speed that part up.

Small problems are the opposite case. At y = 10,000 the whole job takes under a millisecond, so creating 128 threads costs more than the work itself: 1.6 ms versus 0.5 ms at 4 threads.

---

## 9. The join waits for the slowest block

Each bar is one thread in V2, with 8 threads and y = 10 million. Orange is time spent searching; gray is time spent finished and doing nothing while waiting for the join.

Every block has the same *count* of numbers, but not the same *work*. Testing a number takes up to √n divisions, so numbers near 10 million cost more than numbers near 1. Thread 1 is done at 163 ms; thread 8 needs 398 ms; and join() can't return until thread 8 is done.

Add up the gray: **25% of all thread time is wasted waiting.** At 128 threads it's 51%, partly because 128 threads take turns on 16 CPUs and some start late.

The fix would be to hand out small chunks on demand, or to interleave the numbers across threads. But the assignment specifies straight division, so this imbalance is a built-in cost of that scheme.

---

## 10. A barrier on every number dominates the run

This is the joining bottleneck in its most extreme form.

At one thread, testing a number (including the bookkeeping) averages **0.11 microseconds**. At 16 threads it's 2.2 microseconds, and at 128 threads it's **37.5 microseconds**, about 340 times the cost per number.

Three reasons:

1. **The meetings.** At y = 1 million, the threads meet at the barrier 999,999 times. Each meeting needs every thread to arrive and then be woken up again, which costs more than the divisions being shared.
2. **The work is tiny.** A number near one million has only 999 candidate divisors. Split 16 ways, that's about 62 divisions per thread, a few nanoseconds of work, followed by a synchronization.
3. **The jump past 16.** From 16 to 32 threads the cost per number quadruples. With more threads than CPUs, some threads aren't running when a round starts, so every round waits for the operating system to schedule them.

**Key point:** this scheme parallelizes something too small to be worth parallelizing.

---

## 11. When output is printed changes the cost

Printing has opposite effects in the two schemes.

**Straight division, left:** printing immediately wins. V1 finishes in 312 ms; V2 in 538 ms. V1's threads write their lines while the other threads keep searching, so printing overlaps with useful work. V2 has to print all 664,579 primes from a single thread *after* the join, about 245 ms that no number of threads can shrink. That's 46% of V2's run.

**Divisibility testing, right:** printing immediately loses at low thread counts. At 4 threads, V3 takes 733 ms and V4 takes 363 ms. V3 prints inside the barrier's completion step, while every other thread is blocked waiting, so each print adds straight onto the critical path. From 16 threads up the barrier itself is so expensive that this difference disappears.

These were measured writing to a file. In the live demo, printing to a terminal, the printing costs are much larger, so these gaps get bigger.

---

## 12. Immediate printing interleaves the output

Each dot is one output line from V1 with 4 threads; its height shows which thread printed it. The first 17 lines come only from thread 2, because it happened to start first. After that, the four threads take turns in no fixed pattern.

Over the full run of 9,592 primes:

- **96%** of lines come from a different thread than the line before.
- **48%** show a smaller prime than the line before. The output is not sorted.
- **816** lines have an *earlier* timestamp than the line above them. Each thread stamps its line *before* waiting for the output lock, so a thread that waited longer prints a line that is older.
- **0** lines are broken. Each line is built completely and then written under the mutex. Without that lock, characters from different threads could mix inside a single line.

---

## 13. What the four variants show

One lesson per learning objective:

- **(a) Number of threads:** threads help only when each one gets substantial, independent work. Big blocks gave 8.2×; tiny shared tasks gave a 265× slowdown. Nothing improves past the 16 logical CPUs.
- **(b) Interleaving:** threads printing at the same time scramble the output. A mutex keeps each line whole but does nothing for the order, to the point that timestamps can run backwards.
- **(c) Joining bottlenecks:** every join or barrier waits for the slowest thread. Uneven blocks wasted 25–51% of capacity, and a barrier per number cost more than the actual testing.
- **(d) Division schemes:** divide the data, not tiny tasks. Large independent chunks scale; splitting small jobs buys coordination, not speed.

Close with: the fastest design is the one where threads talk to each other the least.
