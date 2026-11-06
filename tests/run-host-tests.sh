#!/bin/sh
# Build and run the host-side tests (native gcc, with sanitizers).
# Needs the FFmpeg headers from scripts/build-deps.sh (headers only, no linking).
set -e
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/gmplay-scale-test"
gcc -O2 -fsanitize=address,undefined -Isrc -Ideps/kindlehf/include tests/scale_test.c src/scale.c -lm -o "$out"
"$out"
