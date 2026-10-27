"""Shared subprocess setup; no simulation logic lives here."""
import csv
import json
import os
import subprocess

from validate import load, validate


def clean_env():
    return {key: value for key, value in os.environ.items() if not key.startswith("GALLERY_")}


def execute(binary, args, root, *, env=None, input_text=None, expected=0, suffix="case"):
    log = root / f"{suffix}.jsonl"
    journal = root / f"{suffix}.csv"
    command = [str(binary), "--quiet", "--log", str(log), "--journal", str(journal), *args]
    result = subprocess.run(command, input=input_text, capture_output=True, text=True,
                            env=env if env is not None else clean_env(), timeout=30)
    assert result.returncode == expected, (command, result.returncode, result.stderr)
    if expected == 0 or (expected == 1 and log.exists() and log.stat().st_size):
        records = load(log)
        summary, kinds = validate(records)
        assert json.loads(result.stdout) == summary
        if journal.exists():
            with journal.open() as stream:
                rows = list(csv.DictReader(stream))
            assert len(rows) == 1 and rows[0]["status"] == summary["status"]
        return records, summary, kinds
    return None
