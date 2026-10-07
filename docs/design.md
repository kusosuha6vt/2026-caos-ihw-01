# How the sequential simulation imitates concurrency

## Module responsibilities

| Module | Responsibility |
|---|---|
| src/main.c | Wire modules together and return exit status |
| include/config.h / src/config.c | Public configuration and checked input |
| include/internal/model.h | Type-only private visitor, painting, FIFO and simulation records |
| simulation.c | FIFO mutations, admission, visitor transitions, event selection, lifetime |
| strategy.c | Fixed RNG, keyed input samples, four painting-choice strategies |
| invariants.c | Cross-check queues, visited paintings, occupancy and conservation |
| output.c | Log lifetime, synchronous JSONL/console output, pacing, CSV and statistics |
| include/signals.h / src/signals.c | Signal setup and handler-owned stop flag |

The event loop calls supporting modules directly. Only simulation.c changes
visitor states, queues and occupancy; strategy.c changes visitor choice RNG,
invariants.c uses membership scratch storage, and output.c maintains sequence and
I/O status. Module interfaces live in include/<module>.h; functions and extern
variables declared there are defined by src/<module>.c. Interfaces forward-declare
private model records; only implementations include internal/model.h.
CMake configures target-scoped includes, C17/POSIX definitions and strict warnings
for both executables. Compiler dependency tracking rebuilds changed headers.
The default build contains gallery; gallery-sanitize is built explicitly.
build/compile_commands.json exports the normal compiler invocations for editors
and clang-tidy. CTest registers the independent Python test runner; check builds
and runs it, and sanitize builds/runs its instrumented counterpart. Docker builds
with CMake/Ninja and copies both source and include directories.

The optional docs target (also named doxygen) generates an HTML API reference
from module headers, private record documentation and docs/api.md. Output is in
the build tree; generation is explicit and does not compile/link the simulator.
Interface comments state preconditions, ownership, return values and time units.

## Design principles and patterns

Each module has one reason to change: parsing rules, scheduling/transitions,
choice strategy, validation, output schema/storage, or signal handling.
main.c wires these responsibilities together. Forward declarations keep interfaces
small and avoid include cycles. Definitions include their matching header first,
allowing the compiler to check signatures.

The model uses an explicit state machine and a deterministic event loop. Choice
strategies share a contract: choose an unfinished painting, modifying only the
visitor's private choice RNG. FIFO queues use visitor slots as intrusive nodes,
and one owning loop mutates them. These are useful C patterns for the assignment.

SOLID's single-responsibility and interface-segregation principles directly guide
this split. Open/closed has a deliberate limit: adding a choice mode extends the
strategy enum/selection code and input help. Four fixed modes make that localized
change straightforward. Liskov substitution is not a class-hierarchy requirement
here; the common strategy contract is its useful analogue. Dependencies flow
through function declarations, while helpers share private model records.
This provides explicit ownership and compiler-checked contracts; it is not a
claim of full object-oriented SOLID compliance.

config.c uses NUMBER_CASE only for identical parsed-number assignments. Its
arguments are enum/member tokens, not evaluated expressions, and it is undefined
after the setter. Boolean bounds, paths and strategy parsing remain visible.
The existing ARG macro builds getopt initializers. Macros do not encode transitions,
resource cleanup or I/O error handling.

## Three distinctions

1. Visitor = model record, not an OS process/thread.
2. Painting place = occupancy reservation, not a held mutex.
3. Simulated time = event timestamp, not elapsed wall-clock runtime.

All mutable state belongs to one event loop. There are no concurrent accesses,
so queues and counters require no synchronization. FIFO still matters for fairness.
The watchman is a separate behavioral function: it admits entrance waiters when
gallery capacity is available. Each visitor owns a seen[] bitmap and choice RNG.

## Hand-checkable day

Capacity 1, one painting with one place, two simultaneous arrivals at t=0,
viewing duration exactly 5 ms:

| Time | Transition | Gallery occupancy |
|---|---|---|
| 0 | Visitor 1 arrives, is admitted, starts viewing | 1 |
| 0 | Visitor 2 arrives and queues outside; new arrivals close | 1 |
| 5 | Visitor 1 completes viewing and exits | 0 |
| 5 | Watchman admits visitor 2; viewing starts | 1 |
| 10 | Visitor 2 finishes and exits | 0 |

Mean entrance wait = (0 + 5) / 2 = 2.5 ms. Painting wait = 0. Gallery and
painting utilization = 100% for this day. This case is asserted by the tester.
With gallery capacity 2, both can enter at t=0, but visitor 2 waits for the
painting instead. With painting capacity 2 as well, both view simultaneously
and finish at t=5; the single loop processes their two completions successively.

## Reading the loop

Find the earliest viewing finish by scanning active records. Compare it with
the next arrival. Advance now and integrate occupancy × elapsed logical time.
Process the event, service the resulting queues, and check invariants. Completion
events win ties with arrivals; tied completions use ascending visitor ID.

No polling ticks are needed, and waiting visitors don't schedule repeated retries.
They resume only when the watchman or a painting releases a place. This avoids
spin loops and makes simulated waiting durations exact.

The event-selection scan and full invariant checks favor clarity over asymptotic
performance. For 100–300 visitors this is sufficient; a heap and incremental
checks could be considered for much larger workloads, after measuring performance.

## Why the model completes

For a finite admitted population, each visitor needs finitely many paintings.
All viewing durations are positive and finite. A painting waiter holds no other
painting place, so no circular resource wait can arise. Every active viewing
finishes and releases a place; FIFO admits waiting visitors. Departures release
gallery places. Therefore remaining required viewings eventually reach zero.
Resource exhaustion is an explicit failure rather than false normal completion.

## Statistics

Entrance wait is measured at admission and averaged over entered visitors.
Painting wait is measured at viewing start and averaged over started viewings.
They include zero waits. Interrupted pending waits are censored and reported as
separate sums. views_finished counts only completed durations. Utilization equals
the integral of occupancy divided by capacity × final logical time. Zero-duration
empty days use utilization 0 by convention.

Logs are synchronously written in the same order as state transitions. Parallel
test execution has no effect on a simulation's seed or logical timestamps.
CSV record locking coordinates only independent processes appending results,
not participants inside a simulation.

## Random inputs

Fixed SplitMix64 mixing and rejection sampling replace platform-dependent rand().
An arrival interval is keyed by seed and visitor ID; a viewing duration by seed,
visitor ID and painting ID. Choice RNG is separate. Therefore changing strategy
changes queueing and order, without changing the visitors' underlying workload.
The RNG is for reproducible simulation, not cryptography.

Least-crowded is an exact observation at choice time, not a promise that the
chosen painting stays least crowded. Cyclic traversal uses a coprime step,
which visits every element of Z/MZ once. A prime step alone does not guarantee it.

## Interruption

Signal handlers only assign a volatile sig_atomic_t flag. The loop stops, cancels
remaining visitors, removes queue links, releases reservations, writes partial
statistics, and frees memory. Display nanosleep is interrupted immediately.
No model threads need to be woken/joined and no child processes need to be reaped.
The tester alone owns its subprocesses and enforces wall-clock timeouts.
