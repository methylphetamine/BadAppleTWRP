#!/usr/bin/env python3
"""Generate an original, dependency-free ASCII test animation.

This is not the Bad Apple!! animation: it is a small bouncing-apple test card
used to build and smoke-test the player zip without shipping copyrighted
frames.  It writes exactly `--rows` lines per frame so the output can be fed
straight to build.py.

    python tools/make_demo_frames.py --output frames/frames.txt
    python build.py --device begonia
"""

import argparse
import math
import os

DENSITY = " .:-=+*#%@"


def put(canvas, x, y, ch):
    if 0 <= y < len(canvas) and 0 <= x < len(canvas[0]):
        canvas[y][x] = ch


def centered(canvas, y, text):
    text = text[: len(canvas[0])]
    x = (len(canvas[0]) - len(text)) // 2
    for i, ch in enumerate(text):
        put(canvas, x + i, y, ch)


def draw_apple(canvas, cx, cy, radius):
    height, width = len(canvas), len(canvas[0])
    # Terminal cells are roughly twice as tall as they are wide.
    for y in range(height):
        for x in range(width):
            dx = x - cx
            dy = (y - cy) * 1.05
            distance = math.hypot(dx, dy)
            if distance > radius:
                continue
            # The bite is a second circle subtracted from the apple.
            bx = x - (cx + radius * 0.62)
            by = (y - (cy - radius * 0.42)) * 1.05
            if math.hypot(bx, by) < radius * 0.55:
                continue
            shade = int((1.0 - distance / radius) * (len(DENSITY) - 1))
            canvas[y][x] = DENSITY[max(1, min(shade, len(DENSITY) - 1))]

    # Stem and leaf.
    top = int(round(cy - radius))
    put(canvas, int(round(cx)), top - 1, "|")
    put(canvas, int(round(cx)) + 1, top - 1, "\\")
    put(canvas, int(round(cx)) + 2, top - 2, "_")


def frame(index, cols, rows):
    canvas = [[" "] * cols for _ in range(rows)]
    middle = (cols - 1) / 2.0

    # Scrolling title on the second line.
    message = "  *** BAD APPLE!! ***  ORANGEFOX 12.1  BEGONIA  "
    repeated = message * (cols // len(message) + 4)
    offset = int(index * 0.7) % len(message)
    for x in range(cols):
        canvas[1][x] = repeated[offset + x]

    # The apple bounces and drifts from side to side.
    cy = (rows * 0.55) + 1.2 * math.sin(index * 0.18)
    cx = middle + (cols * 0.28) * math.sin(index * 0.055)
    draw_apple(canvas, cx, cy, cols * 0.105)

    # Four sparkles orbit the apple.
    for i in range(4):
        angle = index * 0.075 + i * math.pi / 2.0
        put(
            canvas,
            int(round(middle + cols * 0.36 * math.cos(angle))),
            int(round(rows * 0.52 + rows * 0.30 * math.sin(angle))),
            "*",
        )

    centered(canvas, rows - 2, "ORANGEFOX 12.1 // BEGONIA")
    for x in range(cols):
        canvas[rows - 1][x] = "~=+"[(x + index) % 3]

    return ["".join(row) for row in canvas]


def main():
    ap = argparse.ArgumentParser(description="Write an ASCII test animation")
    ap.add_argument("-o", "--output", default=os.path.join("frames", "frames.txt"))
    ap.add_argument("--cols", type=int, default=52)
    ap.add_argument("--rows", type=int, default=19)
    ap.add_argument("--frames", type=int, default=150)
    args = ap.parse_args()

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", newline="\n") as handle:
        for index in range(args.frames):
            for line in frame(index, args.cols, args.rows):
                handle.write(line + "\n")
    print(f"{args.frames} frames written to {args.output}")


if __name__ == "__main__":
    main()
