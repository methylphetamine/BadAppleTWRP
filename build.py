import argparse
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
MARKER = "cq61tech"
SCRIPT_PATH = "META-INF/com/google/android/update-binary"
UPDATER_PATH = "META-INF/com/google/android/updater-script"
STAMP = (2024, 1, 1, 0, 0, 0)

# Device profiles.  A profile only changes defaults; every value can still be
# overridden on the command line.
#
# begonia (Redmi Note 8 Pro) runs OrangeFox 12.1 with TW_THEME := portrait_hdpi
# and OF_SCREEN_H := 2340.  The install page uses the "big" console:
#   console_action_height_big = (2340 - (80 - 72)) - 552 = 1780 px
# which is scaled by (2340 + TW_H_OFFSET) / 2340 = 0.9658, so about 1719 px.
# The fixed console font is FiraCode at 29 px, scaled to 28 px, and its line
# box is ~35-39 px, which gives 44-49 visible lines depending on the exact
# FreeType metrics.  48 is deliberately the upper end of that range: a few
# clipped blank padding lines at the top are harmless, while too few rows
# would leave stale lines from the previous frame on screen.
PROFILES = {
    "begonia": {
        "label": "Xiaomi Redmi Note 8 Pro (begonia), OrangeFox 12.1",
        "shell": "/sbin/sh",
        "rows": 48,
        "content_rows": 19,
        "cols": 52,
        "delay": "0.063",
        "output": os.path.join("dist", "BadApple_begonia_orangeFox_rows48.zip"),
    },
}

DEFAULTS = {
    "shell": "/sbin/sh",
    "rows": 28,
    "content_rows": 19,
    "cols": 54,
    "delay": "0.063",
    "output": os.path.join("dist", "BadApple_TWRP.zip"),
}


def add(archive, name, data, mode):
    info = zipfile.ZipInfo(name, date_time=STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (0o100000 | mode) << 16
    archive.writestr(info, data)


def read(path, mode="rb"):
    with open(os.path.join(ROOT, path), mode) as handle:
        return handle.read()


def set_shell(script, shell):
    lines = script.split(b"\n", 1)
    lines[0] = b"#!" + shell.encode()
    return b"\n".join(lines)


def set_assignment(script, name, value):
    """Replace one shell assignment such as ROWS=28 or CONTENT=19."""
    pattern = rb"(?m)^" + re.escape(name) + rb"=.*$"
    replacement = name + b"=" + str(value).encode()
    patched, count = re.subn(pattern, replacement, script)
    if count != 1:
        sys.exit(f"{name.decode()} assignment not found in script")
    return patched


def check_frames(data, rows, cols):
    lines = data.split(b"\n")
    if lines and lines[-1] == b"":
        lines.pop()
    if not lines:
        sys.exit("frame file is empty")
    if len(lines) % rows:
        sys.exit(f"frame file has {len(lines)} lines, not a multiple of {rows}")
    widths = {len(line) for line in lines}
    if 0 in widths:
        sys.exit("frame file contains empty lines")
    if b"\r" in data:
        sys.exit("frame file contains carriage returns")
    widest = max(widths)
    if widest > cols:
        print(
            f"warning: widest row is {widest} columns, more than the {cols} "
            "columns this profile expects; the console will wrap",
            file=sys.stderr,
        )
    return len(lines) // rows


def build_player(args):
    frames = read(args.frames)
    total = check_frames(frames, args.content_rows, args.cols)
    script = read("src/update-binary.sh")
    script = set_shell(script, args.shell)
    script = set_assignment(script, b"CONTENT", args.content_rows)
    script = set_assignment(script, b"ROWS", args.rows)
    script = set_assignment(script, b"DELAY", args.delay)
    if not script.endswith(b"\n"):
        script += b"\n"
    payload = script + frames
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with zipfile.ZipFile(args.output, "w") as archive:
        add(archive, SCRIPT_PATH, payload, 0o755)
        add(archive, UPDATER_PATH, read("src/updater-script"), 0o644)
        add(archive, MARKER, b"", 0o644)
    print(
        f"{args.output}: {total} frames, {args.cols}x{args.content_rows} cells, "
        f"{args.rows} console rows, {os.path.getsize(args.output)} bytes"
    )


def build_calibration(args):
    script = set_shell(read("src/rows-calibration.sh"), args.shell)
    os.makedirs(os.path.dirname(os.path.abspath(args.calibration_output)), exist_ok=True)
    with zipfile.ZipFile(args.calibration_output, "w") as archive:
        add(archive, SCRIPT_PATH, script, 0o755)
        add(archive, UPDATER_PATH, read("src/updater-script"), 0o644)
    print(f"{args.calibration_output}: {os.path.getsize(args.calibration_output)} bytes")


def main():
    ap = argparse.ArgumentParser(description="Build the flashable zip")
    ap.add_argument(
        "--device",
        choices=sorted(PROFILES),
        help="apply a device/recovery profile (currently: begonia / OrangeFox)",
    )
    ap.add_argument("--frames", default=os.path.join(ROOT, "frames", "frames.txt"))
    ap.add_argument("--output")
    ap.add_argument("--shell", help="interpreter for the update-binary shebang")
    ap.add_argument("--rows", type=int, help="visible console rows")
    ap.add_argument("--content-rows", type=int, help="rows per frame")
    ap.add_argument("--cols", type=int, help="columns per frame (used for validation)")
    ap.add_argument("--delay", help="delay between frames in seconds")
    ap.add_argument("--calibration", action="store_true")
    ap.add_argument("--calibration-output", default=os.path.join(ROOT, "dist", "rows_calibration.zip"))
    args = ap.parse_args()

    profile = dict(DEFAULTS)
    if args.device:
        profile.update(PROFILES[args.device])
        print(f"profile: {args.device} - {profile['label']}", file=sys.stderr)

    args.shell = args.shell or profile["shell"]
    args.rows = args.rows or profile["rows"]
    args.content_rows = args.content_rows or profile["content_rows"]
    args.cols = args.cols or profile["cols"]
    args.delay = args.delay or profile["delay"]
    args.output = args.output or os.path.join(ROOT, profile["output"])

    if args.rows < args.content_rows:
        sys.exit(f"--rows ({args.rows}) must be at least --content-rows ({args.content_rows})")
    if args.rows > 200:
        sys.exit("--rows must be 200 or less")
    try:
        if float(args.delay) <= 0:
            raise ValueError
    except ValueError:
        sys.exit("--delay must be a positive number")

    if args.calibration:
        build_calibration(args)
    else:
        build_player(args)


if __name__ == "__main__":
    main()
