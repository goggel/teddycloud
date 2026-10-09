#!/usr/bin/env python3
"""Test-process PATH wrapper; production never installs this executable."""
import json
import os
from pathlib import Path
import signal
import sys
import time

control = Path(os.environ['TC_PODCAST_ENCODER_CONTROL'])
if not control.exists():
    os.execv(os.environ['TC_PODCAST_REAL_FFMPEG'], ['ffmpeg', *sys.argv[1:]])
mode = control.read_text().strip()
counter = control.with_suffix('.count')
count = int(counter.read_text()) + 1 if counter.exists() else 1
counter.write_text(str(count))
control.with_suffix('.pid').write_text(str(os.getpid()))
if mode == 'hang':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    while True:
        time.sleep(.05)
if mode == 'fail_second' and count == 2:
    sys.exit(1)
frames = 57600 + (1 if mode == 'inconsistent' and count == 2 else 0)
# More than a pipe's stderr capacity exercises concurrent bounded draining.
os.write(2, b'x' * 262144)
# Deliberately split stereo PCM frames across read boundaries.
remaining = frames * 4
for size in [1, 2, 3] * 100:
    os.write(1, b'\0' * size)
    remaining -= size
    time.sleep(.0001)
while remaining:
    size = min(4093, remaining)
    os.write(1, b'\0' * size)
    remaining -= size
if mode == 'partial_frame':
    os.write(1, b'\0')
