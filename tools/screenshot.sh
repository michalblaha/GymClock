#!/usr/bin/env bash
# Capture a screenshot of GymClock from a Pebble emulator, from the command line.
#
# Wraps the full workflow: (optional build) -> boot+install on an emulator ->
# (optional) press a sequence of buttons to reach a given screen -> screenshot.
# Also handles the emulator getting wedged (screenshot/emu-button timeouts) by
# retrying and, on request, wiping the emulator's persisted flash and retrying.
#
# Examples:
#   tools/screenshot.sh emery                          # idle screen on emery
#   tools/screenshot.sh emery -b select                # start the workout, then shoot the run screen
#   tools/screenshot.sh gabbro -b select -o appstore-resources/gb_run.png
#   tools/screenshot.sh emery -b "up select:600" -o /tmp/menu.png   # long-press SELECT (600 ms)
#   tools/screenshot.sh emery --build --wipe           # rebuild + wipe stale state first
#   tools/screenshot.sh emery --no-install             # shoot an already-running emulator
#   tools/screenshot.sh --kill                         # just kill any running emulators
#
# Run with -h for the full option list.
set -uo pipefail

# --- Tunables -----------------------------------------------------------------
DEFAULT_PLATFORM="emery"               # what to target when no platform is given
SCREENSHOT_RETRIES=6                   # screenshot attempts before giving up / auto-wiping
RETRY_DELAY=3                          # seconds between screenshot attempts
INSTALL_SETTLE=4                       # seconds to let the app settle after install before interacting
BUTTON_DELAY=1                         # seconds between button presses
PRE_SHOT_DELAY=2                       # seconds to wait after the last button before shooting
LONG_PRESS_MS=600                      # default hold for "name:long" button specs
# ------------------------------------------------------------------------------

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

err()  { printf 'error: %s\n' "$*" >&2; }
info() { printf '==> %s\n' "$*"; }

usage() {
  sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//; /^set -uo/d'
  cat <<'EOF'

Usage: screenshot.sh [PLATFORM] [options]

Arguments:
  PLATFORM            emery (default), gabbro, basalt, chalk, diorite, aplite, flint

Options:
  -o, --out PATH      Output PNG path (default: ./gymclock-<platform>.png)
  -b, --buttons SEQ   Space-separated buttons to click before the screenshot.
                      Each token is a button (back|up|select|down), optionally
                      "name:MS" to hold for MS milliseconds, or "name:long" for
                      a default long press. e.g. "up select" or "select:long".
  -d, --delay SEC     Extra wait before the screenshot (default: 2).
      --build         Run `pebble build` first.
      --wipe          Wipe the emulator's persisted data before installing
                      (fixes a wedged emulator that times out on screenshots).
      --no-install    Skip build+install; shoot an already-running emulator.
      --kill          Kill any running emulators and exit.
  -h, --help          Show this help.
EOF
}

# --- Argument parsing ---------------------------------------------------------
PLATFORM=""
OUT=""
BUTTONS=""
DO_BUILD=0
DO_WIPE=0
DO_INSTALL=1
KILL_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)    usage; exit 0 ;;
    -o|--out)     OUT="${2:?--out needs a path}"; shift 2 ;;
    -b|--buttons) BUTTONS="${2:?--buttons needs a sequence}"; shift 2 ;;
    -d|--delay)   PRE_SHOT_DELAY="${2:?--delay needs seconds}"; shift 2 ;;
    --build)      DO_BUILD=1; shift ;;
    --wipe)       DO_WIPE=1; shift ;;
    --no-install) DO_INSTALL=0; shift ;;
    --kill)       KILL_ONLY=1; shift ;;
    -*)           err "unknown option: $1"; usage; exit 2 ;;
    *)
      if [[ -z "$PLATFORM" ]]; then PLATFORM="$1"; shift
      else err "unexpected argument: $1"; exit 2; fi ;;
  esac
done

command -v pebble >/dev/null 2>&1 || { err "'pebble' tool not found in PATH"; exit 1; }

# --- Helpers ------------------------------------------------------------------
kill_emulators() {
  pebble kill >/dev/null 2>&1 || true
  pkill -9 -f qemu-pebble >/dev/null 2>&1 || true
  pkill -9 -f pypkjs >/dev/null 2>&1 || true
}

if [[ "$KILL_ONLY" -eq 1 ]]; then
  info "Killing emulators"
  kill_emulators
  info "Done."
  exit 0
fi

PLATFORM="${PLATFORM:-$DEFAULT_PLATFORM}"
OUT="${OUT:-$ROOT/gymclock-$PLATFORM.png}"

# Press one button token: "select", "select:600" (ms hold) or "select:long".
press_button() {
  local token="$1" name dur
  name="${token%%:*}"
  if [[ "$token" == *:* ]]; then
    dur="${token##*:}"
    [[ "$dur" == "long" ]] && dur="$LONG_PRESS_MS"
    info "Press $name (hold ${dur}ms)"
    pebble emu-button click "$name" --duration "$dur" --emulator "$PLATFORM"
  else
    info "Press $name"
    pebble emu-button click "$name" --emulator "$PLATFORM"
  fi
}

# Take the screenshot, retrying on the libpebble2 TimeoutError that a wedged
# emulator throws. Returns non-zero if every attempt failed.
take_screenshot() {
  local attempt
  for ((attempt = 1; attempt <= SCREENSHOT_RETRIES; attempt++)); do
    if pebble screenshot --no-open --emulator "$PLATFORM" "$OUT" 2>/dev/null; then
      return 0
    fi
    info "screenshot attempt $attempt/$SCREENSHOT_RETRIES failed; retrying in ${RETRY_DELAY}s"
    sleep "$RETRY_DELAY"
  done
  return 1
}

# --- Run ----------------------------------------------------------------------
if [[ "$DO_BUILD" -eq 1 ]]; then
  info "Building"
  ( cd "$ROOT" && pebble build ) || { err "build failed"; exit 1; }
fi

if [[ "$DO_INSTALL" -eq 1 ]]; then
  if [[ "$DO_WIPE" -eq 1 ]]; then
    info "Wiping emulator state for $PLATFORM"
    kill_emulators
    sleep 2
    pebble wipe >/dev/null 2>&1 || true
  fi
  info "Installing on $PLATFORM (boots the emulator if needed)"
  if ! ( cd "$ROOT" && pebble install --emulator "$PLATFORM" ); then
    err "install failed; try again with --wipe to reset a wedged emulator"
    exit 1
  fi
  info "Letting the app settle (${INSTALL_SETTLE}s)"
  sleep "$INSTALL_SETTLE"
fi

if [[ -n "$BUTTONS" ]]; then
  for token in $BUTTONS; do
    press_button "$token" || { err "button press failed: $token"; exit 1; }
    sleep "$BUTTON_DELAY"
  done
fi

[[ "$PRE_SHOT_DELAY" != "0" ]] && sleep "$PRE_SHOT_DELAY"

info "Capturing screenshot -> $OUT"
if ! take_screenshot; then
  err "screenshot timed out after $SCREENSHOT_RETRIES attempts."
  if [[ "$DO_WIPE" -eq 0 && "$DO_INSTALL" -eq 1 ]]; then
    err "the emulator looks wedged; re-run with --wipe to reset it."
  fi
  exit 1
fi

info "Saved $OUT"
