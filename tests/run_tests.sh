#!/usr/bin/env bash
# Compile and run the host-side unit tests for the pure workout logic.
# These deliberately do NOT depend on pebble.h, so the system compiler is enough.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)/wltest"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT/src/c/workout_logic.c" \
  "$ROOT/tests/test_workout_logic.c" \
  -o "$OUT"

"$OUT"
