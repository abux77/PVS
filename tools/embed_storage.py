#!/usr/bin/env python3
"""Embed editable factory files for first-boot provisioning and fallback only."""
from pathlib import Path
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("data", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
files = sorted(args.data.glob("*.txt"))
lines = ["/* Generated from data text files by tools/embed_storage.py. */",
         "#pragma once", "#include <stddef.h>",
         "typedef struct { const char *name; const char *text; size_t size; } storage_default_t;"]
for index, path in enumerate(files):
    data = path.read_bytes()
    # Hex escapes avoid C string escaping, source encoding and trigraph surprises.
    lines.append(f"static const char storage_default_{index}[] =")
    for offset in range(0, len(data), 32):
        lines.append('"' + ''.join(f"\\x{byte:02x}" for byte in data[offset:offset + 32]) + '"')
    lines.append(';' if data else '"";')
lines.append("static const storage_default_t storage_defaults[] = {")
for index, path in enumerate(files):
    lines.append(f'  {{"{path.name}", storage_default_{index}, sizeof(storage_default_{index}) - 1}},')
lines.append("};")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n'.join(lines) + '\n')
