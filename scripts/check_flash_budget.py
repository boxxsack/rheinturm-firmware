#!/usr/bin/env python3
"""Check a firmware binary against the app OTA partition capacity."""

import argparse
import csv
import os
import sys
from pathlib import Path


def partition_capacity(path: Path) -> int:
    with path.open(newline="") as stream:
        for row in csv.reader(stream, skipinitialspace=True):
            if not row or row[0].startswith("#") or row[0] != "ota_0":
                continue
            if len(row) < 5 or row[1] != "app":
                raise ValueError(f"invalid ota_0 row in {path}")
            return int(row[4], 0)
    raise ValueError(f"ota_0 app partition not found in {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default=".pio/build/esp32dev/firmware.bin")
    parser.add_argument("--partitions", default="partitions_ota.csv")
    parser.add_argument("--warning-percent", type=float,
                        default=float(os.getenv("FLASH_BUDGET_WARNING_PERCENT", "96")))
    parser.add_argument("--fail-percent", type=float,
                        default=float(os.getenv("FLASH_BUDGET_FAIL_PERCENT", "98")))
    args = parser.parse_args()
    binary = Path(args.binary)
    capacity = partition_capacity(Path(args.partitions))
    used = binary.stat().st_size
    percent = used * 100 / capacity
    remaining = capacity - used
    message = (f"Flash budget: {used:,} bytes used / {capacity:,} bytes capacity "
               f"({percent:.1f}%), {remaining:,} bytes remaining")
    print(message)

    summary = os.getenv("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as stream:
            stream.write(f"{message}\n")

    if percent >= args.fail_percent:
        level = "error" if os.getenv("GITHUB_ACTIONS") == "true" else "failure"
        if os.getenv("GITHUB_ACTIONS") == "true":
            print(f"::{level} title=Flash budget exceeded::{message}")
        if summary:
            with open(summary, "a") as stream:
                stream.write("Error: flash budget hard limit exceeded.\n")
        return 1
    if percent >= args.warning_percent:
        if os.getenv("GITHUB_ACTIONS") == "true":
            print(f"::warning title=Flash budget warning::{message}")
        if summary:
            with open(summary, "a") as stream:
                stream.write("Warning: flash budget warning threshold reached.\n")
        return 0
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"Flash budget check failed: {error}", file=sys.stderr)
        raise SystemExit(2)
