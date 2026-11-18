#!/usr/bin/env python3
"""Check the native NX_PRESENTATION_PROBE readbacks (standard library only).

Run the runtime twice with fresh data roots, width=1000/1720, height=720,
internal_resolution=0, NX_HEADLESS=1, NX_HEADLESS_PRESENT=1,
NX_PRESENTATION_PROBE=1, NX_DUMP_PRESENTED=1 and NX_DUMP_FRAMES=1.
The runtime creates an original synthetic 32x18 picture; no game files are needed.
Pass the two roots here. Tests the complete blit/FSR/FXAA/swapchain readback path.
"""
import argparse
from pathlib import Path


def check(root):
    failures = []
    for case in range(1, 11):
        path = root / "frames" / f"{case:05d}.ppm"
        try:
            with path.open("rb") as file:
                assert file.readline().strip() == b"P6", "expected binary PPM"
                width, height = map(int, file.readline().split())
                assert height == 720 and width in (1000, 1720), "unsupported test extent"
                assert file.readline().strip() == b"255", "expected 8-bit pixels"
                data = file.read()
            assert len(data) == width * height * 3, "truncated readback"
            if case in (5, 6, 7, 8):
                x, y, w, h = 0, 0, width, height  # Stretch
            elif case == 9:
                x, y, w, h = (width - 640) // 2, 0, 640, 720  # 16x18 crop
            elif width == 1000:
                x, y, w, h = 0, 79, 1000, 562
            else:
                x, y, w, h = 220, 0, 1280, 720
            for row in range(height):
                pixels = data[row * width * 3:(row + 1) * width * 3]
                if row < y or row >= y + h:
                    assert pixels.count(0) == len(pixels), "letterbox not completely black"
                    continue
                bars = pixels[:x * 3] + pixels[(x + w) * 3:]
                assert bars.count(0) == len(bars), "pillarbox not completely black"
                picture = pixels[x * 3:(x + w) * 3]
                for channel, expected in enumerate((64, 128, 191)):
                    values = picture[channel::3]
                    assert min(values) >= expected - 2 and max(values) <= expected + 2, "picture lost, stretched into bars or incorrectly filtered"
            print(f"{root.name} case {case}: PASS")
        except (OSError, AssertionError, ValueError) as error:
            failures.append(f"{path}: {error}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path)
    args = parser.parse_args()
    failures = [failure for root in args.roots for failure in check(root)]
    for failure in failures: print(failure)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
