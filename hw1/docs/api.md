# Gallery developer documentation

The program models overlapping visits with one process, one thread and a logical
clock. Model participants are records, not operating-system workers. Start with
`src/main.c`, then the module interfaces and `src/simulation.c`.

## Interfaces and ownership

| Interface | Implementation | Responsibility |
|---|---|---|
| config.h | config.c | Checked CLI, environment, file and interactive input |
| simulation.h | simulation.c | Event loop, FIFO transitions and model lifetime |
| strategy.h | strategy.c | Reproducible input samples and painting choices |
| invariants.h | invariants.c | Queue, history and occupancy consistency |
| output.h | output.c | Synchronous events, log lifetime, CSV and statistics |
| signals.h | signals.c | Signal setup and interruption flag |

`include/internal/model.h` contains shared private types, not an additional API.
simulate() owns visitor slots, painting records, queue links and allocations.
Helpers run synchronously; choice functions may update a visitor's private RNG,
validation uses membership scratch storage, and output maintains sequence/I/O
state. Callers must pass configurations validated by config_read().

## Contracts that matter

- Times and durations are logical integer milliseconds. Display pacing never
  changes model timestamps.
- Completion precedes arrival at equal time. Visitor IDs break completion ties,
  including after slot reuse. A place stays reserved until viewing ends.
- RNG inputs are keyed independently of strategy choices; seeded runs reproduce
  event traces. Every strategy chooses an unfinished painting.
- Signals only set stop_signal. The event loop cancels incomplete visitors,
  removes queue links, releases reservations, writes partial results and frees RAM.
- Output functions report failures through return values or model I/O state;
  journal writes use a bounded POSIX advisory lock. The process exit status is
  authoritative for delivery errors, including late output/close failures.

## Build and verify

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --build build --target check
cmake --build build --target sanitize
cmake --build build --target clang-tidy
cmake --build build --target docs
```

Install Doxygen before configuring to enable the `docs` target. Graphviz and LaTeX
are not needed. Open `docs/generated/html/index.html` for the generated API reference;
compiler commands are exported separately as `build/compile_commands.json`.
Header comments describe preconditions, outputs and failure conventions. Keep
them in the owning module header when changing an interface. Every implementation
function also has a Doxygen comment; private helpers document parameters, effects
and return values, while exported implementations add algorithm notes to their
header contracts. All project-defined structs are documented in their headers.
Static functions are included in the reference, and XML is generated alongside
HTML under `docs/generated/xml` for local coverage inspection. The HTML and its
assets are committed for offline viewing; XML is excluded from Git. Regenerate
the published files with the docs target after changing API comments.

On macOS ARM64, the installed Doxygen 1.18.0 may hit an intermittent comment
scanner crash reported in [upstream issue 12326](https://github.com/doxygen/doxygen/issues/12326).
The CMake runner retries signal crashes/timeouts up to three total attempts for
that version/platform, with a 30-second per-attempt limit. Documentation warnings
and normal error exits are not retried. This mitigates a faulty tool; it does not
repair its parser, and persistent failures still fail the target.

The README covers end-user commands. `docs/design.md` explains the state machine
and design choices; `docs/code-review.md` and `docs/verification.md` record findings
and actual checks. No documentation claim implies a completed student code review.
