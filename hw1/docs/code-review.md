# Code review and refactoring — 2026-10-07

## Current build system

CMakeLists.txt replaces the handwritten Makefile. Target-scoped
C17/POSIX settings and warning flags serve normal and sanitizer builds; generated
compiler dependencies track headers. CTest/check use the existing test runner.
clang-tidy reads exported build/compile_commands.json. Docker uses CMake/Ninja.
Earlier Makefile references below are historical review evidence.

## Follow-up: header ownership and design principles

The module layout supersedes the earlier choice of two shared headers. All headers
now live in include/, and each functional module has a matching .h/.c pair:
config, simulation, strategy, invariants, output and signals. main.c is the
entry point and has no separate exported API. include/internal/model.h contains
only shared type definitions, so it needs no implementation file.

Moved strategy_name from config.c into strategy.c and stop_signal/handler setup
from main.c into signals.c. Each implementation includes its own header first;
headers forward-declare private records and compile independently. choose_painting
takes a const Simulation pointer, making read-only model access explicit while
preserving the mutable visitor RNG. Build flags now reject missing function
prototypes and old-style signatures. Normal/sanitizer/lint builds all include
the project header path, and Docker copies include/.

The design assessment in docs/design.md explains the state machine, strategy
contract, intrusive FIFO and ownership, and distinguishes applicable SOLID
principles from object-oriented requirements. Interfaces are narrow; private
model records remain shared by synchronous helpers. The fixed four-mode enum
is an intentional extension point, not a dynamic plugin system.

NUMBER_CASE removes eleven identical numeric setter cases. It is scoped to
set_field, has token-only arguments, and evaluates no caller expressions.
Special validation and state transitions remain explicit for reviewability.

The findings below describe the initial refactor; its header paths and line counts
are historical. Current verification is recorded in docs/verification.md.

Reviewed the C sources, Makefile/container integration, independent log validator,
test runner and experiment runner against the agreed assignment in PLAN.md. Source PDFs
were not reread. This is an agent review, not a claimed student manual review.

## Findings and changes

1. **Medium — interactive input accepted a prefix before embedded NUL.**
   config.c checked config-file line length against strlen, but interactive mode
   passed getline results straight to trim/set_field. An input beginning
   `2\x00garbage\n` was accepted as capacity 2; the pre-refactor executable
   returned 0 for a complete interactive session using that input. Interactive
   mode now rejects embedded NUL and lines longer than 4096 bytes, matching file
   input. Regression cases require exit 2 for both malformed inputs.
2. **Maintainability — simulation.c mixed unrelated responsibilities.**
   Its 756 lines included state records, FIFO, RNG, transitions, invariant checks,
   output formatting and journal locking. Extracted strategy.c, invariants.c and
   output.c; simulation.c is now 331 lines and owns model transitions and queues.
   Extracted next_completion so the visitor-ID tie rule is visible separately
   from arrival/completion dispatch. File opening and closing belong to output.c.
   Kept one private shared header instead of publishing model internals or adding
   one header per small supporting module.
3. **Low — duplicated CSV header could drift from its write length.**
   journal used two copies of the same schema string. It now has one static array
   with sizeof(header) - 1 as its write length; output bytes are preserved.
4. **Low — painting bound duplicated in input validation and invariant storage.**
   Added GALLERY_MAX_PAINTINGS, used by both. GALLERY_MAX_INPUT_LINE likewise
   names the shared file/interactive line bound. Makefile lists all six C files
   and depends on both headers, including the sanitizer executable.

Existing user changes to formatting and explicit comparisons/macro parentheses
were retained. Sources were formatted with the current four-space configuration.

## Reviewed behavior retained

The simulation remains one process and one thread. Completion precedes arrival
at equal time; completion ties use visitor IDs, not reused slot indices. Existing
painting waiters are serviced before the completing visitor chooses again.
All four strategies keep the same keyed inputs and private choice RNG sequence.
Signals only set the flag; cancellation releases individual queue entries and
reservations before each event snapshot. Output remains synchronous and journal
header/row writes stay under the same bounded POSIX advisory lock.

Configuration remains a cohesive module: the same setter applies CLI, environment,
file and interactive input. FIFO operations remain private to the transition
module because they use visitor slots and are not a general-purpose queue API.
The existing independent validator and subprocess test runner were kept rather
than adding a second scheduler implementation.

## Remaining limits

The alias check assumes output paths are stable while opening files; stat followed
by open is not protection against another process deliberately replacing paths.
The CSV row is written before final statistics/output and log close, so a failure
at those later stages can produce a nonzero exit after a completed CSV row. Treat
exit status and stderr as authoritative for delivery errors. Atomically committing
multiple output files is outside this refactor.

getline enforces the input length limit after reading a line; it is not a hard
allocation limit for hostile input. The fixed-size formatting buffers currently
contain only bounded numbers and internal strings; future user-supplied JSON text
would require escaping and explicit truncation checks. The RNG is not cryptographic.
Event scans/full invariant checks prioritize explainability over large-run speed.

Actual native, sanitizer, lint and before/after checks are in docs/verification.md.
