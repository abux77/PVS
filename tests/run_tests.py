#!/usr/bin/env python3
"""Run host verification with a C compiler and Python's standard library."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command, label):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=120)
    if result.returncode:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise SystemExit(f"FAIL: {label}")
    print(f"PASS: {label}", flush=True)


def main():
    run([sys.executable, "tests/test_oled_i2c.py"], "PIO OLED transport (ASan/UBSan)")
    run([sys.executable, "tests/test_oled.py"], "SSD1306 display driver (ASan/UBSan)")
    run([sys.executable, "tests/test_keypad.py"], "printed keypad and file migration")
    run([sys.executable, "tests/test_dma_handoff.py"], "DMA completion race regression")
    run([sys.executable, "tests/test_audio_startup.py"], "PIO startup register model")
    run([sys.executable, "tests/test_controls.py"], "control scenarios (ASan/UBSan)")
    run([sys.executable, "tests/test_sam_reference.py"],
        "captured reciter/parser fixtures, original C renderer PCM fixtures, quantization and bounds")
    with tempfile.TemporaryDirectory(prefix="pvs-host-tests-") as directory:
        build = Path(directory)
        run([sys.executable, "tools/embed_storage.py", "data", str(build / "storage_defaults.h")],
            "factory file generation")
        compiler = shlex.split(os.environ.get("CC", "cc"))
        options = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        audio_rate = build / "test_audio_rate"
        run(compiler + options + ["-Isrc", "tests/test_audio_rate.c", "-o", str(audio_rate)],
            "DMA sample-rate test compilation")
        run([str(audio_rate)], "DMA sample-rate accuracy across 8000–88200 Hz at three clocks")
        storage = build / "test_storage"
        run(compiler + options + ["-Iinclude", "-Ithird_party/fatfs", "-I" + str(build),
            "tests/test_storage.c", "src/storage.c", "src/storage_format.c",
            "third_party/fatfs/ff.c", "third_party/fatfs/ffunicode.c", "-o", str(storage)],
            "storage test compilation")
        run([str(storage)], "real FAT filesystem, persistence, malformed files and USB ownership (ASan/UBSan)")
        speech = build / "test_speech"
        run(compiler + options + ["-Itests/stubs", "-Iinclude", "-Isrc/sam", "-Ithird_party/sam",
            "tests/test_speech.c", "src/sam_pvs.c", "src/storage_format.c", "src/sam/sam_reciter.c",
            "src/sam/sam_parser.c", "src/sam/sam_renderer.c", "third_party/sam/render.c",
            "-o", str(speech)], "speech test compilation")
        run([str(speech)], "all factory phrases, speech callbacks/cancellation and 600 renderer cases (ASan/UBSan)")


if __name__ == "__main__":
    main()
