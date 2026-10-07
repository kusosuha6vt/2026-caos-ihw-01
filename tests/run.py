"""Run named checks in isolated directories with bounded subprocess parallelism."""
import argparse
import signal
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from cases import timing_case, replay_case, input_case, limit_case
from helpers import execute
from io_cases import output_case, locked_journal_case, interruption_case, shared_journal_case


def isolated_case(case, binary):
    with tempfile.TemporaryDirectory(prefix=f"gallery-{case.__name__}-") as directory:
        return case(binary, Path(directory))


def finite_case(binary, strategy, paintings, seed):
    with tempfile.TemporaryDirectory(prefix="gallery-test-") as directory:
        return execute(binary, ["--strategy", strategy, "--paintings", str(paintings),
                               "--capacity", "9", "--painting-capacity", "2",
                               "--visitors", "100", "--seed", str(seed),
                               "--arrival-min", "0", "--arrival-max", "3"], Path(directory))[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build/gallery"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("jobs must be 1..32")
    binary = args.binary.resolve()
    tasks = [(f"{strategy}/M={paintings}/seed={seed}", finite_case, (binary, strategy, paintings, seed))
             for strategy in ("ordered", "random", "least-crowded", "cyclic")
             for paintings in (1, 6, 7) for seed in (0, 42, 91)]
    for case in (timing_case, replay_case, input_case, limit_case,
                 output_case, locked_journal_case):
        tasks.append((case.__name__, isolated_case, (case, binary)))
    tasks.append(("shared journal", shared_journal_case, (binary,)))
    tasks += [(f"signal={sig}/unlimited={unlimited}", interruption_case, (binary, sig, unlimited))
              for sig in (signal.SIGINT, signal.SIGTERM) for unlimited in (False, True)]
    kinds = set()
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(function, *values): name for name, function, values in tasks}
        for future in as_completed(futures):
            name = futures[future]
            try:
                kinds |= future.result() or set()
            except Exception as error:
                raise RuntimeError(f"FAILED {name}: {error}") from error
    mandatory = {"arrival", "entrance_wait", "permission", "enter", "choose", "painting_wait",
                 "view_start", "view_end", "exit", "close", "cancel", "summary"}
    assert mandatory <= kinds, f"missing events: {mandatory - kinds}"
    print(f"PASS: {len(tasks)} test groups; {args.jobs} parallel tester workers; "
          f"all mandatory events; {time.monotonic() - started:.2f}s")


if __name__ == "__main__":
    main()
