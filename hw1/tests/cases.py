"""Deterministic examples, replay, and input validation."""
from helpers import clean_env, execute


TINY = ["--capacity", "1", "--paintings", "1", "--painting-capacity", "1",
        "--visitors", "2", "--arrival-min", "0", "--arrival-max", "0",
        "--view-min", "5", "--view-max", "5"]


def timing_case(binary, root):
    _, summary, seen = execute(binary, TINY, root)
    assert summary["time_ms"] == 10
    assert summary["mean_entrance_wait_ms"] == 2.5
    assert summary["mean_painting_wait_ms"] == 0
    assert summary["gallery_utilization"] == 1
    # Both finish at the same instant; completion must precede a tied arrival.
    tied = ["--capacity", "3", "--paintings", "1", "--painting-capacity", "3",
            "--visitors", "3", "--arrival-min", "5", "--arrival-max", "5",
            "--view-min", "5", "--view-max", "5"]
    events, _, _ = execute(binary, tied, root, suffix="ties")
    at_ten = [e["event"] for e in events if e["time_ms"] == 10]
    assert at_ten.index("view_end") < at_ten.index("arrival")
    simultaneous = ["--visitors", "20", "--capacity", "20", "--paintings", "2",
                    "--painting-capacity", "20", "--arrival-min", "0", "--arrival-max", "0",
                    "--view-min", "5", "--view-max", "5"]
    execute(binary, simultaneous, root, suffix="simultaneous")
    execute(binary, ["--visitors", "0"], root, suffix="empty")
    execute(binary, ["--visitors", "300", "--strategy", "least-crowded"], root, suffix="300")
    execute(binary, ["--visitors", "1000", "--capacity", "1", "--paintings", "1",
                     "--painting-capacity", "1", "--max-active", "1",
                     "--arrival-min", "1", "--arrival-max", "1",
                     "--view-min", "1", "--view-max", "1"], root, suffix="slot-reuse")
    return seen


def replay_case(binary, root):
    base = ["--visitors", "40", "--seed", "18446744073709551615", "--strategy", "cyclic"]
    first, _, _ = execute(binary, base, root, suffix="replay1")
    second, _, _ = execute(binary, base, root, suffix="replay2")
    assert first == second, "non-deterministic seeded trace"
    delayed, _, _ = execute(binary, [*TINY, "--delay-ms", "1"], root, suffix="paced")
    unpaced, _, _ = execute(binary, TINY, root, suffix="unpaced")
    assert delayed[1:] == unpaced[1:], "pacing changed model"
    inputs = []
    for strategy in ("ordered", "random", "least-crowded", "cyclic"):
        records, _, _ = execute(binary, ["--strategy", strategy, "--visitors", "30"],
                                root, suffix=strategy)
        inputs.append(([(e["visitor"], e["time_ms"]) for e in records if e["event"] == "arrival"],
                       {(e["visitor"], e["painting"]): e["duration_ms"]
                        for e in records if e["event"] == "view_start"}))
    assert all(value == inputs[0] for value in inputs), "strategy changed exogenous inputs"


def input_case(binary, root):
    # Main asks for every primary field in the documented order.
    execute(binary, ["--interactive"], root,
            input_text="2\n3\n1\n5\n0\n1\n2\n3\nordered\n", suffix="interactive")
    execute(binary, ["--interactive"], root, input_text="", expected=2, suffix="eof")
    execute(binary, ["--interactive"], root, input_text="2\x00garbage\n",
            expected=2, suffix="interactive-nul")
    execute(binary, ["--interactive"], root, input_text=" " * 4096 + "2\n",
            expected=2, suffix="interactive-long")
    config = root / "config.env"
    config.write_text("# parsed as data\n GALLERY_VISITORS = 3\nGALLERY_STRATEGY=ordered\n")
    env = clean_env() | {"GALLERY_VISITORS": "4"}
    records, _, _ = execute(binary, ["--config", str(config), "--visitors", "5"],
                            root, env=env, suffix="precedence")
    assert records[0]["visitors"] == 5 and records[0]["strategy"] == "ordered"
    records, _, _ = execute(binary, ["--config", str(config)], root, env=env, suffix="env")
    assert records[0]["visitors"] == 4
    invalid = [["--capacity", "0"], ["--paintings", "0"], ["--visitors", "-1"],
               ["--visitors", "18446744073709551616"], ["--view-min", "0"],
               ["--arrival-min", "5", "--arrival-max", "2"], ["--strategy", "prime"],
               ["--paintings", "257"], ["--delay-ms", "60001"], ["--unknown"],
               ["unexpected"], ["--unlimited", "--arrival-min", "0", "--arrival-max", "0"]]
    for index, args in enumerate(invalid):
        execute(binary, args, root, expected=2, suffix=f"invalid{index}")
    for index, data in enumerate(["UNKNOWN=1\n", "GALLERY_SEED=1\nGALLERY_SEED=2\n",
                                  "GALLERY_VISITORS=$(touch sentinel)\n", "GALLERY_SEED=2\x00\n"]):
        config.write_text(data)
        execute(binary, ["--config", str(config)], root, expected=2, suffix=f"badfile{index}")


def limit_case(binary, root):
    _, summary, _ = execute(binary, [*TINY, "--visitors", "10", "--max-active", "1"],
                            root, expected=1, suffix="limit")
    assert summary["status"] == "failed" and summary["cancelled"] == 1
