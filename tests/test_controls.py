#!/usr/bin/env python3
"""Compile and exercise the real PVS control state machine with hardware fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = (
    "bootsel", "bootsel_eject", "startup", "modes_pickup", "pickup_threshold", "live_modulation",
    "word_modulation", "chromatic_last", "presets", "locks", "locked_sample",
    "edit", "format", "bank_priority", "retrigger", "edit_chord", "exclusive_modes", "idle_display", "display_notices",
)


def main():
    with tempfile.TemporaryDirectory(prefix="pvs-controls-") as temporary:
        executable = Path(temporary) / "test_controls"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT / "include"),
            str(ROOT / "src/pvs.c"), str(ROOT / "tests/test_controls.c"),
            "-o", str(executable),
        ]
        subprocess.run(command, check=True)
        for scenario in SCENARIOS:
            result = subprocess.run([str(executable), scenario], text=True, capture_output=True)
            if result.returncode:
                print(result.stdout, end="")
                print(result.stderr, end="")
                raise SystemExit(f"FAIL {scenario}: return code {result.returncode}")
            print(f"PASS {scenario}")
        print(f"Controls: {len(SCENARIOS)} scenarios passed with AddressSanitizer/UBSan")


if __name__ == "__main__":
    main()
