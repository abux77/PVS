#!/usr/bin/env python3
"""Increment the build-directory-local firmware number on each build."""
from pathlib import Path
from datetime import datetime, timezone
import re
import sys
output = Path(sys.argv[1])
previous = re.search(r'PVS_BUILD_NUMBER (\d+)', output.read_text()) if output.exists() else None
number = int(previous[1]) + 1 if previous else 1
stamp = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')
output.parent.mkdir(parents=True, exist_ok=True)
temporary = output.with_suffix('.tmp')
temporary.write_text(f'#pragma once\n#define PVS_BUILD_NUMBER {number}\n#define PVS_BUILD_TIME "{stamp}"\n')
temporary.replace(output)
