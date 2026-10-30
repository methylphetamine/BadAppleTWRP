#!/bin/sh
# Build fbplayer.  The recovery is aarch64, so the release binary is a static
# aarch64 ELF; a static binary needs no interpreter and runs fine under the
# Android kernel even though it is linked against glibc.
#
#   tools/build-fbplayer.sh                 -> build/fbplayer (aarch64, static)
#   CC=gcc tools/build-fbplayer.sh out/x86  -> native build for local testing
set -e
cd "$(dirname "$0")/.."

CC=${CC:-aarch64-linux-gnu-gcc}
OUT=${1:-build/fbplayer}
STRIP=${STRIP:-aarch64-linux-gnu-strip}

mkdir -p "$(dirname "$OUT")"
$CC -O2 -Wall -Wextra -static -Isrc -o "$OUT" tools/fbplayer.c
if [ "$(basename "$CC")" != "gcc" ] && [ -n "$STRIP" ] && command -v "$STRIP" >/dev/null 2>&1; then
  $STRIP --strip-all "$OUT"
fi
ls -l "$OUT"
