"""POSIX output failures, interruption, and shared-journal checks."""
import csv
import fcntl
import json
import os
import signal
import subprocess
import tempfile
import time
from pathlib import Path

from helpers import clean_env
from validate import load, validate


def output_case(binary, root):
    result = subprocess.run([str(binary), "--log", str(root), "--quiet"],
                            capture_output=True, env=clean_env(), timeout=10)
    assert result.returncode == 1
    result = subprocess.run([str(binary), "--quiet", "--log", "-", "--journal", str(root)],
                            capture_output=True, env=clean_env(), timeout=10)
    assert result.returncode == 1 and json.loads(result.stdout)["status"] == "failed"
    config = root / "existing.csv"
    config.write_text("keep this journal\n")
    alias = root / "alias"
    os.link(config, alias)
    result = subprocess.run([str(binary), "--log", str(alias), "--journal", str(config)],
                            capture_output=True, env=clean_env(), timeout=10)
    assert result.returncode == 1 and config.read_text() == "keep this journal\n"
    # A closed pipe must yield an ordinary I/O error, not SIGPIPE termination.
    read_fd, write_fd = os.pipe()
    os.close(read_fd)
    try:
        result = subprocess.run([str(binary), "--visitors", "1", "--log", "-", "--journal", "-"],
                                stdout=write_fd, stderr=subprocess.PIPE, env=clean_env(), timeout=10)
        assert result.returncode == 1
    finally:
        os.close(write_fd)


def locked_journal_case(binary, root):
    # An external holder must not trap the simulator forever in journal locking.
    locked_log = root / "locked.jsonl"
    with (root / "locked.csv").open("w") as holder:
        fcntl.lockf(holder, fcntl.LOCK_EX)
        process = subprocess.Popen([str(binary), "--quiet", "--visitors", "0",
                                   "--log", str(locked_log), "--journal", str(root / "locked.csv")],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True, env=clean_env())
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if locked_log.exists() and '"close"' in locked_log.read_text():
                    break
                assert process.poll() is None
                time.sleep(0.001)
            else:
                raise AssertionError("no close event before journal wait")
            process.send_signal(signal.SIGINT)
            stdout, stderr = process.communicate(timeout=5)
            assert process.returncode == 1, stderr  # Journal I/O failure takes precedence.
            assert json.loads(stdout)["status"] == "failed"
            validate(load(locked_log))
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()


def interruption_case(binary, sig, unlimited):
    with tempfile.TemporaryDirectory(prefix="gallery-signal-") as directory:
        root = Path(directory)
        log = root / "events.jsonl"
        args = [str(binary), "--quiet", "--delay-ms", "2", "--capacity", "4",
                "--paintings", "3", "--painting-capacity", "1", "--visitors", "300",
                "--arrival-min", "1", "--arrival-max", "1", "--view-min", "30", "--view-max", "30",
                "--log", str(log), "--journal", str(root / "results.csv")]
        if unlimited:
            args.append("--unlimited")
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, env=clean_env())
        try:
            deadline = time.monotonic() + 10
            # Wait for evidence, not a guessed startup sleep: cover both queue types.
            while time.monotonic() < deadline:
                if log.exists() and b'"entrance_wait"' in log.read_bytes() and b'"painting_wait"' in log.read_bytes():
                    break
                assert process.poll() is None, "simulation ended before signal"
                time.sleep(0.005)
            else:
                raise AssertionError("no queue-wait events before signal")
            process.send_signal(sig)
            stdout, stderr = process.communicate(timeout=10)
            assert process.returncode == 128 + sig, (process.returncode, stderr)
            summary, kinds = validate(load(log))
            assert summary["status"] == "interrupted" and summary["signal"] == sig
            assert summary["cancelled"] > 0 and json.loads(stdout) == summary
            return kinds
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()


def shared_journal_case(binary):
    with tempfile.TemporaryDirectory(prefix="gallery-journal-") as directory:
        root = Path(directory)
        processes = []
        for index in range(12):
            processes.append(subprocess.Popen([str(binary), "--quiet", "--visitors", "2",
                             "--seed", str(index), "--log", str(root / f"{index}.jsonl"),
                             "--journal", str(root / "shared.csv")], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, env=clean_env()))
        try:
            for process in processes:
                _, stderr = process.communicate(timeout=20)
                assert process.returncode == 0, stderr
            with (root / "shared.csv").open() as stream:
                rows = list(csv.DictReader(stream))
            assert len(rows) == 12 and {int(row["seed"]) for row in rows} == set(range(12))
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
    return set()
