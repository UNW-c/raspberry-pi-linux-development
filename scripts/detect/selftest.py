"""Offline self-check for the detector plumbing.

Runs without a camera, a stream or a display, so the parts that a GUI run would
only exercise late - the frame broker, the detection thread and the overlay -
can be checked in a second:

    python scripts/detect/selftest.py

It exists because a shadowed threading.Thread attribute (self._started) crashed
only on the window path, which the headless tests never reached.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import detect_nailong as detector_module  # noqa: E402


class FakeSource:
    def __init__(self) -> None:
        self.count = 0

    def read(self):
        time.sleep(0.005)
        self.count += 1
        return np.full((480, 640, 3), self.count % 200, dtype=np.uint8)

    def close(self) -> None:
        return None


class FakeDetector:
    def detect(self, frame):
        return [{"label": "nailong", "score": 0.91, "box": [10.0, 20.0, 100.0, 80.0]}]


def main() -> int:
    frames = detector_module.LatestFrame(FakeSource())
    worker = detector_module.DetectionWorker(FakeDetector(), frames)
    worker.start()
    time.sleep(0.4)

    results, shape, processed, hits, fps = worker.snapshot()
    worker.running = False
    worker.join(timeout=2.0)

    checks = []
    checks.append(("frames arrive", frames.seen > 0))
    checks.append(("worker starts and processes", processed > 0))
    checks.append(("worker stops", not worker.is_alive()))
    checks.append(("boxes reported", len(results) == 1 and results[0]["label"] == "nailong"))
    checks.append(("frame shape recorded", shape == (480, 640, 3)))
    checks.append(("fps is a number", isinstance(fps, float) and fps > 0))

    frame = frames.get()
    overlay = detector_module.draw(frame, results, fps, "selfcheck", 960)
    checks.append(("overlay sized to 960 wide", overlay.shape == (720, 960, 3)))

    native = detector_module.draw(frame, results, fps, "selfcheck", 0)
    checks.append(("native overlay untouched", native.shape == (480, 640, 3)))

    frames.close()

    failed = 0
    for name, ok in checks:
        print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
        if not ok:
            failed += 1
    print(f"{len(checks) - failed}/{len(checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
