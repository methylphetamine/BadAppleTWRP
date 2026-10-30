# BadApple-TWRP

Plays Bad Apple!! as ASCII art inside the TWRP flash log. Flash the zip and the recovery console turns into a 15 fps player.

Credits: cq61tech

This fork adds a ready-made profile for the Xiaomi Redmi Note 8 Pro (`begonia`)
running OrangeFox 12.1, built by
[`methylphetamine/orangefox-begonia-build`](https://github.com/methylphetamine/orangefox-begonia-build)
(run `40689383411`). See [begonia / OrangeFox](#redmi-note-8-pro-begonia--orangefox).

## How it works

- `update-binary` is a plain shell script. The ASCII frames are appended to the same file after a `__DATA__` marker.
- Every frame is padded to the height of the console and sent to the recovery as one block of `ui_print` commands, so each frame pushes the previous one off the screen.
- `set_progress` drives the install progress bar, so it works as a seek bar.
- The touchscreen is locked while the video plays, because touching the console disables its auto-scroll. It is restored when playback ends or the script is interrupted.
- The installer console can only **append** text, so frames accumulate there. The framebuffer build (`begonia-fb`) instead freezes the recovery GUI, redraws `/dev/graphics/fb0` for every frame and clears the screen first, so the picture is replaced rather than stacked.">

## Requirements

- A recovery that runs a shell `update-binary` and shows `ui_print` output (TWRP and forks).
- `/sbin/sh` in the recovery. OrangeFox keeps it as a symlink to `/system/bin/sh`; if your recovery only has `/system/bin/sh`, build the zip with `--shell /system/bin/sh`.
- Tested on: Galaxy S9+ (SM-G965F) with TWRP, and the OrangeFox builds listed below.

## Quick start

1. Download `BadApple_TWRP.zip` from the Releases page.
2. Copy it to your SD card or internal storage.
3. Boot TWRP, Install, select the zip.

The first frames may look cropped or misaligned on other screens. Flash `rows_calibration.zip` once, count the fully visible numbered lines at the end, and use that number as `ba_rows` (see below).

## Tested Devices

| Device | Android Version | Codename | Recovery | Rec Version | Status |
|---|---|---|---|---|---|
| Galaxy S9+ | Android 16 | star2lte | TWRP | v3.7.0-9.0 | Working Good✅ |
| Infinix GT20 Pro | Android 16 | X6871 | OrangeFox | - | Working • |
| TECNO POVA 5 | Android 17 | LH7n | OrangeFox | - | Working |
| Infinix NOTE 30 | Android 16 | X6833B | OrangeFox | - | Working |
| Galaxy S9+ | Android 16 | star2lte | Evo X Recovery | 11.8 | Not Working |
| Galaxy S9+ | Android 16 | star2lte | OrangeFox | R11.0 | Working |
| Redmi Note 8 Pro | - | begonia | OrangeFox | R12.1 (wifi-browser) | Built for this recovery, awaiting device test |

### Redmi Note 8 Pro (begonia) / OrangeFox

The fork targets the OrangeFox 12.1 `begonia` build from
[`orangefox-begonia-build`](https://github.com/methylphetamine/orangefox-begonia-build)
(`TW_THEME := portrait_hdpi`, `OF_SCREEN_H := 2340`, `TW_H_OFFSET := -80`).

The install page in that theme uses the big console:

- console area: 984 x 1780 px before theme scaling, about 984 x 1719 px after
  the 0.9658 vertical scale;
- fixed font: FiraCode, 29 px scaled to 28 px;
- usable text grid: about 56 columns x 44-49 rows.

A 52 x 19 frame is the widest 4:3 picture that fits without wrapping, and the
profile emits 48 console lines per frame. That is deliberately the top of the
measured row range: a couple of clipped blank padding lines at the top are
harmless, while a row count that is too small leaves bits of the previous frame
on screen.

Build the begonia zip:

```sh
python tools/convert.py badapple.mp4 --cols 52 --rows 19 --fps 15
python build.py --device begonia
# dist/BadApple_begonia_orangeFox_rows48.zip
```

A prebuilt full player (3286 frames, 3:39) and the calibration zip are on the
[releases page](https://github.com/methylphetamine/BadAppleTWRP/releases/tag/begonia-orangeFox-r12.2).

The framebuffer build (recommended - clears the screen every frame):
```sh
python tools/build-fbplayer.sh build/fbplayer
python build.py --device begonia-fb
# dist/BadApple_begonia_fb.zip
```

The console fallback:
```sh
python tools/convert.py badapple.mp4 --cols 52 --rows 19 --fps 15
python build.py --device begonia
# dist/BadApple_begonia_orangeFox_rows48.zip
```

`--device begonia` sets `/sbin/sh`, 48 console rows, 19 rows per frame,
52 columns and the `rows48` output name. The name also acts as the runtime
override, so the zip still works if `ba_rows` is absent. The profile does not
include frames: generate them yourself from a video you are allowed to use.

Without a video, the bundled test animation builds a demo zip:

```sh
python tools/make_demo_frames.py
python build.py --device begonia
```

This is an original bouncing-apple test card, not the Bad Apple!! animation.

On-device calibration: flash `python build.py --device begonia --calibration`
(`dist/rows_calibration.zip`), count the fully visible numbered lines and put
that number in `ba_rows` at the root of your storage. The zip name still wins,
so use `--rows N` plus a matching output name if you prefer a permanent value.
The OrangeFox theme, DPI or `OF_SCREEN_H` can change the number after an update.

The fork also builds in GitHub Actions: **Actions -> Build BadApple zip
(begonia / OrangeFox) -> Run workflow**. Pass an optional direct `frames_url`
to a `frames.txt`; with no URL it builds the test animation. The workflow
publishes the player zip and `rows_calibration.zip` as artifacts.

Touch lock knows the MediaTek `hbtp` touch device used by begonia and skips the
`hbtp_vm` virtual mouse. If locking leaves the panel unresponsive, reboot
recovery and use the `notouch` zip name.

## Settings

Settings are read from the root of any mounted storage. The zip name overrides files.

| Setting | File in storage root | Zip name | Default |
| --- | --- | --- | --- |
| Visible console rows | `ba_rows` (number) | `rows32` (two digits) | 28 (48 in the begonia profile) |
| Delay between frames in seconds | `ba_delay` (for example `0.063`) | - | 0.063 |
| Disable touchscreen lock | `ba_notouchlock` (empty file) | `notouch` | locked |

Example: `BadApple_rows32_notouch.zip`.

Storage is searched in this order: `/external_sd`, `/sdcard1`, `/usb_otg`, `/usb-otg`, `/mnt/media_rw/*`, `/storage/*`, `/mnt/usb*`, any mounted vfat/exfat/sdfat/texfat/fuseblk/ntfs volume from `/proc/mounts`, then `/sdcard` and `/data/media/0`.

A log is written to `bad_apple_log.txt` in the first writable storage, and to `/tmp` otherwise. Attach it when reporting a problem.

## Touchscreen lock

The script looks for touch input devices in `/sys/class/input` (names matching common touch drivers, key and button devices are skipped) and writes to their `enabled` or `inhibited` node, plus `/sys/class/sec/tsp/input/enabled` on Samsung devices. The original values are saved and written back on exit. If no suitable node exists nothing is changed.

Some touch drivers may not recover cleanly after being disabled. If your touchscreen stays dead after playback, reboot the recovery, and use the `notouch` zip name or `ba_notouchlock` from then on. Holding the power button always works.

## Building

Python 3.8 or newer, standard library only.

```
python build.py
python build.py --device begonia
python build.py --calibration
```

Options for `build.py`:

- `--device {begonia}` apply a device/recovery profile; everything below can still be overridden
- `--frames PATH` frame file, default `frames/frames.txt`
- `--output PATH` default `dist/BadApple_TWRP.zip` (the begonia profile uses `dist/BadApple_begonia_orangeFox_rows48.zip`)
- `--shell PATH` interpreter written to the shebang, default `/sbin/sh`
- `--rows N` visible console rows, default 28 (48 for begonia)
- `--content-rows N` rows per frame, default 19
- `--cols N` columns per frame, default 54 (52 for begonia); used to warn about wrapping
- `--delay SECONDS` delay between frames, default `0.063`

The build adds an empty file named `cq61tech` to the zip. The installer refuses to run without it.

## Making frames

Requires `ffmpeg`, `numpy` and `Pillow`.

```
python tools/convert.py video.mp4
```

Options: `--cols` (default 52), `--rows` (default 19), `--fps` (default 15), `--cell-w` and `--cell-h` (default 6 and 12), `--black` and `--white` levels, `--font`, `--chars`, `--letterbox`.

For each cell the converter picks the character whose shape best matches the pixels, instead of mapping brightness to a character ramp. The console is about 54 columns wide on the reference device, so 52 columns fit with a margin; begonia's OrangeFox console fits about 56. If you change `--rows`, pass the same value to `build.py --content-rows`.

For begonia, keep `--cols 52 --rows 19`: 19 rows is the widest 4:3 frame that still fits the 56-column console. The player centers those 19 lines inside the taller OrangeFox console.

## Troubleshooting

- Fails immediately with error 255: the recovery has no `/sbin/sh`. Rebuild with `--shell /system/bin/sh`.
- Lines wrap and the picture is broken: your console is narrower than 52 columns. Generate frames with a smaller `--cols`.
- Top of the picture is cut off: `ba_rows` is larger than the visible console height, or smaller than needed. Use the calibration zip.
- Previous frames stay visible on begonia/OrangeFox: the row count is off. Run the calibration zip; the profile default of 48 is the safe upper bound, not a substitute for counting on your build.
- Plays too fast or too slow: change `ba_delay`. Larger is slower.
- Nothing is printed: check `bad_apple_log.txt`.

## Legal

The code in this repository is released under the MIT license. The song Bad Apple!! and the original animation belong to their respective authors and are not part of this repository. Generate frames from a video you are allowed to use, and check the rights before redistributing a zip that contains them.

`tools/make_demo_frames.py` draws its own bouncing-apple test card and contains no frames from the animation.
