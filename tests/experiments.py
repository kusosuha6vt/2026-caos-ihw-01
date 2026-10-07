"""Paired strategy experiments: identical exogenous inputs for every strategy."""
import argparse
import csv
import json
import statistics
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from functools import partial
from pathlib import Path

from helpers import clean_env

STRATEGIES = ("ordered", "random", "least-crowded", "cyclic")
SCENARIOS = {
    "low-load": {"capacity": 20, "painting-capacity": 3, "arrival-min": 15,
                 "arrival-max": 25, "view-min": 10, "view-max": 30},
    "crowded": {"capacity": 20, "painting-capacity": 2, "arrival-min": 0,
                "arrival-max": 2, "view-min": 15, "view-max": 35},
}


def run_one(binary, directory, task):
    scenario, strategy, seed = task
    parameters = SCENARIOS[scenario]
    log = str(directory / f"{scenario}-{strategy}.jsonl") if seed == 0 else "-"
    args = [str(binary), "--quiet", "--visitors", "200", "--paintings", "7",
            "--strategy", strategy, "--seed", str(seed), "--log", log,
            "--journal", str(directory / "runs.csv")]
    for name, value in parameters.items():
        args.extend([f"--{name}", str(value)])
    result = subprocess.run(args, capture_output=True, text=True, env=clean_env(), timeout=30)
    if result.returncode:
        raise RuntimeError(f"{scenario}/{strategy}/{seed}: {result.stderr}")
    summary = json.loads(result.stdout)
    assert summary["completed"] == 200 and summary["status"] == "completed"
    return {"scenario": scenario, "strategy": strategy, "seed": seed, **summary}


def run_batch(binary, directory, jobs, seeds):
    tasks = [(scenario, strategy, seed) for scenario in SCENARIOS
             for strategy in STRATEGIES for seed in range(seeds)]
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        return list(pool.map(partial(run_one, binary, directory), tasks))


def summarize(results):
    table = []
    for scenario in SCENARIOS:
        baseline = {r["seed"]: r["time_ms"] for r in results
                    if r["scenario"] == scenario and r["strategy"] == "ordered"}
        for strategy in STRATEGIES:
            group = [r for r in results if r["scenario"] == scenario and r["strategy"] == strategy]
            times = [r["time_ms"] for r in group]
            deltas = [r["time_ms"] - baseline[r["seed"]] for r in group]
            table.append({
                "scenario": scenario,
                "strategy": strategy,
                "runs": len(group),
                "mean_time_ms": statistics.mean(times),
                "min_time_ms": min(times),
                "max_time_ms": max(times),
                "sd_time_ms": statistics.stdev(times) if len(times) > 1 else 0,
                "mean_entrance_wait_ms": statistics.mean(
                    r["mean_entrance_wait_ms"] for r in group),
                "mean_painting_wait_ms": statistics.mean(
                    r["mean_painting_wait_ms"] for r in group),
                "mean_paired_delta_ms": statistics.mean(deltas),
                "mean_gallery_utilization": statistics.mean(
                    r["gallery_utilization"] for r in group),
            })
    return table


def save_results(output, directory, results, data):
    table = data["table"]
    (output / "summary.json").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    with (output / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(table[0]))
        writer.writeheader()
        writer.writerows(table)
    with (directory / "summaries.json").open("w") as stream:
        json.dump(results, stream, indent=2)
    with (directory / "runs.csv").open() as stream:
        assert len(list(csv.DictReader(stream))) == len(results)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build/gallery"))
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--seeds", type=int, default=24)
    parser.add_argument("--output", type=Path, default=Path("output/experiments"))
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32 or not 1 <= args.seeds <= 1000:
        parser.error("jobs must be 1..32; seeds 1..1000")
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="batch-", dir=args.output))
    results = run_batch(args.binary.resolve(), directory, args.jobs, args.seeds)
    table = summarize(results)
    data = {"seeds": args.seeds, "total_runs": len(results), "visitors": 200,
            "paintings": 7, "scenarios": SCENARIOS, "batch": str(directory), "table": table}
    save_results(args.output, directory, results, data)
    print(f"Saved {len(results)} runs ({args.seeds} paired seeds) in {directory}")
    for row in table:
        print(f"{row['scenario']:9} {row['strategy']:13} "
              f"day={row['mean_time_ms']:8.2f}ms "
              f"entrance={row['mean_entrance_wait_ms']:8.2f}ms "
              f"painting={row['mean_painting_wait_ms']:6.2f}ms "
              f"paired delta={row['mean_paired_delta_ms']:+8.2f}ms")


if __name__ == "__main__":
    main()
