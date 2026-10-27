# Test code map

Start with `run.py`: it lists the checks and runs them in isolated directories.
No test framework, dynamic discovery, or simulation implementation is hidden here.

- `cases.py`: hand-calculated examples, event ties, replay, input and limits.
- `io_cases.py`: output failures, locked/shared journals and SIGINT/SIGTERM.
- `helpers.py`: clean environment and one checked subprocess invocation.
- `validate.py`: independent JSONL reconstruction. `accept` advances time;
  `transition` updates visitor history; `check_snapshot` checks occupancy/FIFO
  snapshots; `check_result` reconciles totals, waits and utilization. The three
  longer transitions have their own small methods.
- `experiments.py`: run a paired batch, summarize it, save results. Output fields
  remain compatible with the report; old batches are never deleted.

Run from the project root:

```sh
cmake --build build --target check sanitize
python3 tests/run.py --binary build/gallery --jobs 1
python3 tests/experiments.py --binary build/gallery --seeds 2 --output /tmp/gallery-experiment
```

There are 47 runtime groups: 36 strategy/painting/seed combinations, six named
edge-case groups, shared-journal writing, and four signal/mode combinations.
The simulation remains single-threaded; only the tester runs child processes
in parallel. Tests use ordinary assertions: do not run Python with `-O`.

The fake-Doxygen regression harness was removed as unrelated homework tooling.
The real `docs` target still reports errors and uses its existing bounded retry
workaround. That workaround no longer has an automated regression suite here.
