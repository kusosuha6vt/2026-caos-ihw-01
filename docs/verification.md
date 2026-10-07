# Verification and AI critical assessment

## Published API reference — 2026-10-08

Moved Doxygen output from the build directory to docs/generated. The repository
includes docs/generated/html and all of its styles, scripts and images; generated
XML remains local and ignored. CMake docs/doxygen targets regenerate the same
published location, and README/developer instructions point to the new paths.

Configured and generated with Doxygen 1.18.0 successfully, without warnings or
retries. Checked local HTML links and asset references for missing files: none.
XML confirms descriptions for 44 distinct functions and five structs; public
functions also have header entries. HTML contains 146 files, approximately 1.4 MB.
No simulation code changed, and runtime tests were not repeated for this change.

## Docker build correction and Linux verification — 2026-10-08

Added libc6-dev explicitly to the Dockerfile dependency list. With recommended
packages disabled, gcc alone omitted the C headers and startup objects, causing
the compiler-link check failure recorded below. Compose needed no change.

Executed with Docker Engine 29.8.2 and Compose v5.5.1, Debian bookworm on
Linux/ARM64, GCC 12.2.0 and CMake 3.25.1:

- Built runtime, test and experiment images successfully through Compose.
- Runtime suite: all 47 groups passed, including FIFO, invariants, output errors,
  deterministic replay and SIGINT/SIGTERM in finite/unlimited modes.
- CMake sanitize target in the test container: all 47 groups passed under
  AddressSanitizer/UndefinedBehaviorSanitizer.
- Runtime service completed a 100-visitor run using the packaged demo config.
  Validated its persisted JSONL and single CSV row on the host.
- Experiment service completed 192 runs. Aggregate JSON matches the saved
  experiment data exactly after excluding the output-directory path.

Verification outputs are isolated in output/runs/docker-check.Fxe03h; previous
experiment data and report files were preserved. This establishes Linux/ARM64
execution; Linux/AMD64 was not tested. Earlier unavailable-daemon/build-failure
entries below describe their original checks, not the current working setup.

## Docker configuration check — 2026-10-08

Docker Desktop is now reachable: Engine 29.8.2, Linux/ARM64; Compose v5.5.1.
Both default and test/experiments-profile Compose configurations validate.
Attempted `docker compose --profile test --profile experiments build`.
The Debian dependency layer completed after transient download retries, but
all service builds share a failing CMake compiler check: the linker cannot find
Scrt1.o and crti.o. The Dockerfile installs gcc with --no-install-recommends
without explicitly installing libc6-dev, so the C development files are absent.

Required correction: add libc6-dev to the apt-get install list in Dockerfile.
This check records the finding; the Dockerfile was not changed. No simulation,
runtime test suite, sanitizer suite or experiments ran on Linux because the
image build failed before compilation. Earlier macOS evidence remains valid.

## Compact report — 2026-10-07

Formatting follow-up: converted the module descriptions and recorded fixes into
lists, highlighted table headers and command blocks, and kept the report at three
pages. Compared extracted PDF text before/after, ignoring whitespace, soft
hyphenation and bullet markers: wording and punctuation were identical. Visually
checked all final pages and regenerated both PDF copies with identical text.

Rewrote report/main.typ as a self-contained three-page report, replacing the
11-page PDF. It covers the assignment, event loop, strategies, interface,
termination, a hand-calculated example and the saved 192-run experiment.
Kept a brief AI disclosure and concrete fixes. Removed obsolete supporting
inputs and their references from project guidance and documentation.

The experiment tables and comparison remain generated from the saved summary
JSON. Updated the runtime test count to the previously verified 47 groups.
CMake's report target compiled successfully; Poppler rendering and visual
inspection cover all three pages. Checked that tables, commands and page numbers
fit, with no clipped text. Refreshed the PDF next to the Typst source as well.
No simulation code or experiment data changed; runtime tests were not repeated
for this report-only update.

## Python tooling cleanup — 2026-10-07

The problem was mixed concerns:
one special-case function combined timing, input, replay and POSIX I/O, while one
validator function mixed transitions, snapshots and statistical reconciliation.
Split cases into named functions in cases.py/io_cases.py, share only subprocess
setup in helpers.py, and leave run.py as a short explicit test list/launcher.
The validator now has explicit stages and small helpers for longer transitions;
it remains independent of the C scheduler. Experiments separate batch execution,
aggregation and saving, with no output schema change or new dependencies.

Deleted tests/test_doxygen_runner.py and its CTest registration as unrelated
support-tool complexity. This intentionally removes automated retry-regression
coverage, not simulation coverage. Keep cmake/RunDoxygen.cmake and the docs target;
the Doxygen checks recorded below remain historical evidence.

Actual verification on macOS:

- Strict CMake build, check and ASan/UBSan sanitize: all 47 groups passed.
  The previous 42 groups became 47 by splitting one mixed group into six;
  none of its runtime assertions was dropped. A one-worker run also passed.
- CTest: gallery-suite passed; no stale doxygen-runner registration remains.
- Ran both original and refactored experiments for all 192 days in separate
  temporary directories. All per-run summaries and aggregate JSON (excluding
  the batch path) and CSV matched exactly. Existing report results were untouched.
- Original and refactored validators agreed on all eight saved experiment
  traces. Both rejected nine corruptions covering sequence, clock, occupancy,
  FIFO, invalid painting choice, exit release and summary counts. The new
  validator also rejects repeated configuration and events after the summary.

AI-generated extraction defects caught and fixed before delivery: an incorrect
split point at a nested capacity assertion caused an indentation error, and the
extracted choice helper initially lacked its visitor argument. Import/runtime
checks exposed both; the final native/sanitizer suites passed after correction.
No C implementation changed. No new Linux execution is claimed.

## Doxygen SIGBUS recovery — 2026-10-07

The reported docs command failed with exit 138. Read the matching
macOS diagnostic report for PID 31335: SIGBUS/KERN_PROTECTION_FAILURE in
commentscanYYlex + 1928, called by CommentScanner and MarkdownOutlineParser.
The installed tool is Homebrew Doxygen 1.18.0 on ARM64. The same scanner frame
and offset are reported in [upstream issue 12326](https://github.com/doxygen/doxygen/issues/12326).
This is a match to an external tool-failure report, not a claim to have identified
or repaired the binary's internal memory error. Earlier successful single runs
did not establish reliable repeated generation.

Implemented cmake/RunDoxygen.cmake. It executes the selected Doxygen directly,
limits an invocation to 30 seconds, and propagates ordinary diagnostic errors
immediately. CMake enables up to three total attempts only for 1.18.0 on macOS
ARM64; retries cover SIGBUS, SIGSEGV and timeout. Exhaustion still fails the target,
with visible diagnostics. No retries silently turn a documentation error into
success, and no system package was replaced. The runner itself requires no Python.

Executed checks:

- Original direct invocation loop: ten runs passed, run 11 exceeded its 3-second
  diagnostic timeout. This reproduces intermittency without changing inputs.
- New docs target: 15 consecutive builds passed. Run 5 hit the 30-second timeout,
  printed a retry warning, and then succeeded. Remaining runs succeeded directly.
  The sandbox also printed a ps permission denial during timeout termination;
  the target completed successfully on retry. No approval escalation was needed.
- Four runner regression groups (six scenarios): transient SIGBUS, transient
  SIGSEGV and timeout recover; normal exit 1 fails after one attempt; persistent
  SIGBUS fails after three; a one-attempt configuration does not retry.
- CTest now registers gallery-suite and doxygen-runner. Both passed: the existing
  42 runtime groups and all runner regressions. Python is used only by tests.
- Final docs generation after documentation edits also recovered from one timeout
  and succeeded. Checked HTML overview and XML descriptions for all 44 functions
  and five structs; coverage is preserved after recovery.

README, developer overview and PLAN explain the bounded workaround and its limits.
No simulation source changed and no sanitizer rerun was needed. Persistent parser
failures still require a working Doxygen installation; the retry is a mitigation.

## Complete function/type documentation — 2026-10-07

Added Doxygen comments above every
implementation function, including main and all static helpers. Exported functions
retain their header contracts and add implementation notes in source. Enabled
EXTRACT_STATIC and XML generation; existing comments cover all project structs.

- `cmake --build build --target docs`: generated HTML/XML without warnings.
- Compared function definitions in all seven C files with documented XML
  members: all 44 functions had a nonempty description. Confirmed documented
  Config, Queue, Visitor, Painting and Simulation compounds: all five structs.
- `cmake --build build --target check --parallel`: strict native build and all
  42 runtime test groups passed. Algorithms/outputs are unchanged; only comments
  and exported-function parameter identifiers changed.
- `clang-format --dry-run --Werror` passed for every source/header.

Actual documentation correction: adding implementation notes made Doxygen merge
header/definition documentation and expose mismatched parameter identifiers
(config vs c, simulation vs s, visitor vs v). Renamed implementation parameters
to match owning headers, preserving strings and behavior. An intermediate Doxygen
invocation also stalled at HTML finalization and was interrupted; direct verbose
generation and the subsequent docs target both passed. As recorded previously,
the local tool's intermittent finalization stall has no identified cause.
Sanitizer tests were not repeated for comments/identifier-only changes.

## Doxygen documentation — 2026-10-07

Added module/file and API-contract
comments to all seven headers, a developer overview in docs/api.md, and the
docs/Doxyfile.in template. Optional docs/doxygen targets generate HTML under
build/docs/html. Documentation generation runs explicitly, requires neither
Graphviz nor LaTeX, and fails on documentation errors.

Executed on macOS with Doxygen 1.18.0 and CMake 4.4.3:

- Configure and `cmake --build build --target docs`: passed without warnings.
- `cmake --build build --target doxygen`: alias regenerated the same HTML.
  One intermediate regeneration stalled and was interrupted. Archived the
  derived HTML directory under /private/tmp/gallery-review.CO1rEF, regenerated
  from a fresh output directory, and repeated the alias successfully (under a
  15-second timeout). The cause of that isolated Doxygen stall was not determined.
- Checked generated pages for all 14 declared module functions and confirmed
  index.html contains the developer overview. Private model records also have
  generated type pages. Parameters, ownership and failure conventions are in
  the owning header's API comments.
- Strict native rebuild passed after header comment changes.
- Fresh configure/build with CMAKE_DISABLE_FIND_PACKAGE_Doxygen=TRUE,
  BUILD_TESTING=OFF and Python discovery disabled: passed, demonstrating runtime
  builds do not acquire a documentation-tool dependency.
- Header formatting checked with clang-format --dry-run --Werror.

No executable code changed; runtime/sanitizer suites were not repeated for this
documentation-only change. Generated HTML stays in the ignored build directory.
No Linux/container execution or report PDF regeneration was claimed.

## CMake migration — 2026-10-07

CMakeLists.txt replaces the
handwritten Makefile; all previous workflows are represented by CMake/CTest.
No C simulation code changed. Runtime builds need only CMake and a C compiler;
Python, clang-tidy and Typst enable their respective optional targets.

Executed on macOS with CMake 4.4.3, Ninja and AppleClang 21:

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` and normal build: passed.
- `ctest --test-dir build --output-on-failure`: registered gallery-suite passed,
  running all 42 existing test groups.
- Final `cmake --build build --target check sanitize --parallel`: both 42-group
  suites passed, including ASan/UBSan after the SDK configuration fix.
- `cmake --build build --target clang-tidy`: all seven sources passed using
  the exported database, with no project diagnostics.
- Parsed build/compile_commands.json: exactly seven normal-build commands,
  matching every src/*.c file, including C17, POSIX definition, project include
  directory and explicit Apple SDK. Sanitizer commands are deliberately omitted
  so editor/lint flags are unambiguous.
- Separate Unix Makefiles-generator configuration and runtime build passed with
  BUILD_TESTING=OFF and CMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE. Thus runtime
  configuration does not require the test interpreter. This is still macOS
  verification, not Linux testing.

Actual build-configuration corrections during implementation:

1. An empty custom target with USES_TERMINAL was rejected by CMake. Collected
   lint commands first and attached them directly to the custom target.
2. Initial CACHE-only initialization retained CMake's existing empty export
   setting, so no database was generated. Explicitly enabled the normal variable
   and excluded the sanitizer target with its per-target export property.
3. CMake 4 defaults to an implicit Apple SDK for /usr/bin/cc; Homebrew clang-tidy
   could not replay that command and reported missing signal.h. Default an empty
   macOS sysroot to macosx before project initialization, preserving an explicit
   user SDK choice. The regenerated database includes the resolved -isysroot.

Dockerfile now installs CMake/Ninja and builds the same target. README, PLAN,
design and report source use CMake commands. Container execution, experiments
and report PDF regeneration were not performed during this build migration.
Historical Makefile checks below remain records of what was actually executed.

## Header layout/design follow-up — 2026-10-07

Replaced the two umbrella headers
with six module headers and one private type-only header under include/. Their
contents were migrated; no configuration or model fields were removed. Function
declarations/extern data now belong to the matching implementation file.
Design principles, patterns and macro tradeoffs are recorded in docs/design.md
and docs/code-review.md.

- Strict native C17 rebuild passed with -Wmissing-prototypes/-Wstrict-prototypes
  added; no compiler warnings. Header include paths are independent of CPPFLAGS.
- `make test`: 42 groups passed with the new layout and numeric setter macro.
- `make sanitize`: the same 42 groups passed under ASan/UBSan.
- `make clang-tidy`: all seven C files passed; no project diagnostics.
- Compiled each of seven headers independently, included twice, using C17,
  -Wall/-Wextra/-Wpedantic/-Werror: all passed. Checked that every function
  declared by include/<module>.h has its definition in src/<module>.c.
  Confirmed no .h files remain under src/. signals.h's extern stop_signal is
  defined by signals.c, which also owns the signal handler.
- Compared the saved executable from immediately before this layout change
  against the final native build in 108 cases: four strategies, three painting
  counts (1/6/7), three seeds (0/42/UINT64_MAX), and human/quiet/JSON output.
  All succeeded; stdout, stderr, JSONL and CSV remained byte-identical.
- Dockerfile copies include/ alongside src/; no container/Linux execution was
  performed, and the generated report PDF was not rebuilt in this follow-up.

## Review/refactoring — 2026-10-07

Findings and retained limits are recorded in docs/code-review.md. Existing user formatting and lint fixes
were retained; no AI subagents were used.

- Baseline `make test`: 42 test groups passed before refactoring. Saved that
  executable in a temporary directory for before/after comparison.
- Reproduced embedded-NUL interactive acceptance: a complete input session with
  capacity `2\x00garbage` returned 0. Added regression cases for NUL and an
  oversized interactive line; both now require exit 2.
- Final `make test`: 42 groups passed, including both new regression cases,
  FIFO reconstruction, all five invariants, signals, output errors, replay,
  slot reuse, and independent concurrent journal appenders.
- Final `make sanitize`: the same 42 groups passed with AddressSanitizer and
  UndefinedBehaviorSanitizer after all source changes.
- Final `make clang-tidy`: all six C translation units passed, no diagnostics
  from project sources; system-header findings were suppressed by the tool.
- Before/after comparison: 108 cases (4 strategies × 3 painting counts: 1/6/7
  × 3 seeds: 0/42/UINT64_MAX × 3 output modes: human/quiet/JSON), each with
  100 visitors, capacity 9, painting capacity 2 and arrivals 0..3 ms.
  Exit codes were 0 and stdout, stderr, complete JSONL and CSV were byte-identical.
- Strict C17 builds passed. Makefile now tracks all six sources and both headers
  for normal and sanitizer builds. Docker copies the entire src directory and
  builds through this same Makefile; Linux execution was not performed this turn.

README, design, plan and report source describe the new modules. The generated
report PDF was not rebuilt/rendered during this code-focused task; rebuild and
visually verify it before submitting the report.

## Developer tooling — 2026-10-07

Added `.clang-format` using LLVM style, two-space indentation and 80 columns,
and `.clang-tidy` with C-relevant analyzer, bugprone, performance and portability
checks. Formatting is explicit; existing C sources were not rewritten.
`make clang-tidy` checks each translation unit with the build's CPPFLAGS/CFLAGS,
stops on tool failure, and accepts a CLANG_TIDY executable override.
Analyzer/compiler diagnostics are fatal; heuristic suggestions are advisory.
Excluded Annex K recommendations (optional *_s APIs do not fit portable POSIX),
signed-bitwise diagnostics for valid signed shift counts, and noisy
easily-swappable-parameter heuristics.

Verified on macOS with Homebrew clang-format/clang-tidy 23.1.1:

- `clang-format --style=file --dump-config`: configuration parsed successfully.
- `make clang-tidy`: all three C files analyzed, exit 0; four advisory findings
  remain: macro argument parentheses in config.c and explicit strcmp result
  comparisons in config.c/simulation.c. No C fixes were made in this tooling task.
- `make clang-tidy CLANG_TIDY=false`: deliberately failed with make exit 2,
  confirming tool failures propagate.
- `make`: strict-warning C17 build passed. Runtime tests were not rerun because
  this change only adds developer configuration and a lint target.

## Executed on 2026-10-06

Host: macOS (Darwin), native C compiler. The submitted C simulation is single
process/single thread; Python tester workers launch independent subprocesses.

- `make`: passed with C17, -O2, -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror.
- `make test`: 42 test groups, four parallel tester workers, all mandatory events.
- `make sanitize`: same groups passed under AddressSanitizer + UndefinedBehaviorSanitizer.
- A hand-checkable two-visitor day returned 10 ms, mean entrance wait 2.5 ms,
  no painting wait, and 100% gallery/painting utilization.
- `make experiments`: 192 successful runs, 24 paired seeds × 4 strategies × 2
  scenarios, 200 visitors and 7 paintings each. All journal rows preserved.
- `docker compose config --quiet`: passed; Compose syntax and configuration valid.
- `docker info`: daemon unavailable at the configured socket. Container build,
  Linux execution, and Linux sanitizer behavior have NOT been verified here.
  Docker Desktop startup was attempted; macOS reported kLSNoExecutableErr
  (the app bundle's executable is missing). No installation or repair was made.
- `make report`: generated the Russian 11-page Typst PDF. Poppler rendering and
  visual review covered all pages, including tables. This PDF was later replaced
  by the compact report described above.
  Typst required formatting the literal GALLERY_* wildcard as inline code; fixed
  the markup error. Experimental narrative is derived from the same JSON as
  its table, rather than retaining hardcoded numbers after future reruns.

The tests reconstruct histories from logs, including FIFO order, all five
invariants, durations, means, peaks and utilization. They cover 100/300 visitors,
zero visitors, simultaneous completions, max uint64 seed, paced replay, strategy
input equality, interactive input/EOF, file/env/CLI priority, invalid numbers,
duplicate/unknown/NUL-containing config, active-record limit, I/O errors, log/journal
hard-link alias protection, SIGINT/SIGTERM for finite/unlimited runs, and 12
concurrent appenders sharing a journal. A further case reuses one slot for 1000
visitors; final native and sanitizer reruns include that case and passed.

## Actual corrections to AI-generated work

1. Initial compilation failed under strict warnings: implicit uint64-to-floating
   conversions could lose precision, and BSD flock declarations were hidden by
   strict POSIX feature selection on macOS. Added explicit conversions and replaced
   flock with POSIX fcntl record locking; retained all warning flags.
2. Review caught that `finish <= now` wrongly rejects another viewing finishing
   at the same timestamp before its event is processed. Changed to `finish < now`;
   tests cover simultaneous completions and completion/arrival ties.
3. Initial cancellation drained all queue links before emitting individual
   cancellation events. This made intermediate log snapshots unsuitable for
   independent reconstruction. Cancellation now removes each waiter at its own
   transition, preserving observable queue consistency.
4. The earlier plan incorrectly prioritized process/thread architecture over
   assignment 1's sequential requirement. The instructor clarification supplied
   by the user resolved it. Removed threaded/IPC design, logger queue, wall-clock
   scheduling and scheduling-dependent trace assumptions from active instructions.
5. Final review found that a blocking journal lock could wait indefinitely behind
   an external holder. Replaced it with bounded, signal-aware nonblocking retries.
   A tester holds the journal lock and interrupts the simulation, checking prompt
   failure/cleanup instead of a hang. Journal I/O failure takes precedence over
   the signal exit code when that journal cannot be saved.

These were agent review/compiler/test corrections. Do not claim that the student
has already manually inspected or independently authored the generated code.
Before submission, the student must read the implementation, reproduce tests,
and add any personal corrections and assessment to the report.

## Experimental observations

Under low load all strategies have almost equal closing times and no entrance
waiting. Under the selected crowded conditions, least-crowded averages 2612.29 ms
versus ordered 2828.29 ms (216 ms / approximately 7.64% lower). Random and cyclic
average about 3054 ms, worse than ordered. This is evidence for these parameters
and seeds, not a proof that any strategy universally dominates. Ordered can
pipeline visitors across paintings; randomization alone does not ensure efficiency.

## Limits

Runtime input ranges and MAX_ACTIVE bound memory; long unlimited logs can grow on
disk. Event scans and full invariant checks trade speed for clarity. Logical time
omits walking times and models arrivals/viewing as integer uniform samples.
FIFO fairness is explicit; parameters can still exhaust the active-record limit.
No external linked libraries are used. Journal locking assumes cooperating writers
on a filesystem supporting POSIX advisory locks. Source PDFs were not reread.
