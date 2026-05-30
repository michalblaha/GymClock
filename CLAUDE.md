# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Pebble **watchapp** (not a watchface) — a customizable HIIT/interval workout timer. It counts down work/rest intervals, cycles through a list of exercises across multiple laps, vibrates on transitions, and switches the whole screen between a "work" and "rest" color scheme. Primary target hardware is the Pebble Time 2 (emery, 200×228 color), but it also builds for aplite, basalt, chalk, and diorite. Fork of `samuelmr/pebble-workout`.

Because this is an app driving a per-second countdown, it uses `app_timer_register()` on a 1000 ms loop — this is intentional and correct here. Do **not** apply the watchface "never use SECOND_UNIT / minimize timers" battery rule to this project. (One caveat: a timer re-registered after each callback can drift slightly over a long session. If exact timing ever matters, derive remaining time from a stored end timestamp instead of counting ticks.)

### SDK version (do not "fix" this)

This app is built with the **SDK 4** toolchain. It targets the SDK-4-era platforms emery + diorite and uses SDK-4-era APIs: `AppGlance` (introduced in SDK 4.0) and the Health **heart-rate metric** (`HealthMetricHeartRateBPM`, an SDK-4.x-line addition — note the `HealthService` API itself predates SDK 4). Per the current docs at https://developer.repebble.com/docs/, `package.json` `pebble.sdkVersion` stays **`"3"`** — there is no `sdkVersion` `"4"`; the toolchain rejects `"4"`. API availability follows from the installed SDK/toolchain, the build targets, and compile-time macros (`PBL_*`) — not from this field. `targetPlatforms` selects which platforms compile; `capabilities` declares things like `health`/`configurable`. Leave `sdkVersion: "3"` as is; bumping it to `"4"` breaks the build.

## Build, run, debug

```bash
pebble build                                    # compiles all targetPlatforms -> build/GymClock.pbw
./tests/run_tests.sh                            # host unit tests for the pure logic (uses system cc, no SDK)
pebble install --emulator emery                 # boot QEMU + install (also: basalt, chalk, diorite, aplite)
pebble screenshot --no-open --emulator emery    # capture screenshot for visual verification
pebble logs --emulator emery                    # view APP_LOG output (the code logs every state change)
pebble install --phone <ip>                     # install on a real watch via the Pebble app
pebble clean                                    # wipe build/; required after changing messageKeys in package.json
```

Active toolchain is **SDK 4.9.169** (Pebble Tool v5). The bundle is `build/GymClock.pbw` (name derives from the project dir, not the `name`/`displayName` fields). A clean build emits no warnings from our code; the only remaining one is an SDK-level `LOAD segment with RWX permissions` linker note, inherent to every Pebble app.

**Tests** live in `tests/`. `src/c/workout_logic.{h,c}` is a deliberately `pebble.h`-free module holding the index/lap arithmetic and the workout-finished decision; `tests/test_workout_logic.c` exercises it via a tiny assert harness compiled with the system `cc -Werror`. Put any new pure logic there so it stays unit-testable — UI/vibration/timer code that needs `pebble.h` can only be checked in QEMU. No linter is wired up yet; `clang-format`/`clang-tidy` over `src/c/**` and ESLint over `src/pkjs/**` are welcome (keep configs at repo root).

## Architecture

Two-process design typical of Pebble apps:

- **`src/c/gymclock.c`** — the watch-side C app. Single window, five `TextLayer`s (HR / lap / big timer / current exercise / next-or-status) stacked top-down in `window_load` from `LAYOUT_*` constants. The layout is **adaptive**: a `tall` flag (`bounds.size.h >= LAYOUT_TALL_THRESHOLD`, true for emery) picks larger fonts for HR (28-bold) and the lap line on the big screen and steps them down on the 168/180px B&W displays so nothing overflows. On the big screen the lap line uses a **custom font** — `RESOURCE_ID_ROBOTO_REGULAR_42` (Roboto Regular 42px, bundled from `resources/fonts/Roboto-Regular.ttf`, Apache-2.0, glyph set trimmed via `characterRegex` to the chars the lap line needs); it is `fonts_load_custom_font`-ed in `window_load` and unloaded in `window_unload`. Smaller screens fall back to system `GOTHIC_28`. The lap line uses `GTextOverflowModeFill` and a compact `"%d/%d  Lap %d"` format. No custom drawing or GPaths.
- **`src/c/workout_logic.{h,c}`** — pure, `pebble.h`-free state arithmetic (`wl_advance`, `wl_retreat`, `wl_next_index`, `wl_is_last`). Compiled into the app (wscript globs `src/c/**/*.c`) *and* into the host tests. Keep it dependency-free.
- **`src/pkjs/config.js`** — the **Clay** form definition. Exports `{ items, NUM_EXERCISE_SLOTS, exerciseSlotKey }`. Tunables (slot count, slider ranges, defaults, input length limit) are constants at the top.
- **`src/pkjs/index.js`** — phone-side glue. Initializes Clay with `autoHandleEvents:false`, opens `clay.generateUrl()` on `showConfiguration`, and on `webviewclosed` reads raw values via `clay.getSettings(response, false)` and builds the AppMessage by hand.
- **`wscript`** — standard Pebble waf build; globs `src/c/**/*.c` and bundles `src/pkjs/**` (`.js` + `.json`). Rarely needs editing.

### State machine (the core of the C code)

Global flags `working` / `resting` / `paused` plus a `seconds` countdown drive everything. `timer_callback` fires every second, decrements `seconds`, and on reaching 0 flips work↔rest: switching to rest previews the next exercise (`wl_next_index`); switching back to work advances via `wl_advance(&current_exercise, &lap, exercises)` (wrap increments `lap`) and recolors via `set_colors()`. `reset()` is the single source of truth for the idle/zeroed state (called on config receipt and on stop).

**No-rest mode:** when `default_rest <= 0` the rest phase is skipped entirely — `begin_next_work()` (shared by the rest→work transition) runs straight after a work interval, so work flows directly into the next exercise. The Clay `REST` slider allows 0.

**Finite rounds:** `REPEAT` is enforced. When a work interval ends, `wl_is_last(current, count, lap, repeat)` decides whether this was the last exercise of the last lap; if so, `workout_complete()` stops the timer, long-pulses, resets, and shows "Done!" / "SEL to restart". `repeat <= 0` means loop forever. (This is the one intentional behavior change from the original, which always looped.)

Button mapping (`click_config_provider`): SELECT = start/pause, long SELECT = stop+reset, long UP = skip back one exercise, long DOWN = skip forward, **single UP (from the idle/armed screen) = open in-watch settings**. The long-click handlers show a hint label first, then act on release. `skip`/`skip_back`/`reset` all null out `timer` after cancelling it (avoids cancelling a freed timer later). The idle screen shows a `SEL: Start / UP: Settings` hint.

### In-watch settings (Phase 1: Work / Rest / Rounds)

A separate `settings_window` (pushed onto the stack, opened only when idle) hosts a `MenuLayer` with three rows; selecting a row pushes a built-in `NumberWindow` for that parameter (ranges in the `SETTING_*` constants, mirroring the Clay sliders). Values are written **live** in the `incremented`/`decremented` callbacks straight into `default_work`/`default_rest`/`default_repeat`, so leaving via Select or Back both keep the change. On the settings window's `unload` it calls `save_config()` + `reset()` to persist and apply. The window is created lazily and reused (destroyed only in `deinit`); the menu + NumberWindows are created in `load` and freed in `unload`. Exercise names are deliberately **not** editable on-watch (no keyboard) — that stays in Clay. (Phase 2 — on-watch exercise list with skip/disable synced to Clay — is designed but not yet implemented.)

### Config message protocol (Clay + named keys)

Config travels as one AppMessage with **named** `messageKeys`, declared as a list in `package.json`:

| Key | Meaning |
|-----|---------|
| `WORK` | work seconds |
| `REST` | rest seconds |
| `REPEAT` | number of rounds (laps) |
| `EXERCISE_COUNT` | number of exercises sent |
| `EXERCISES[50]` | array key: exercise i at `MESSAGE_KEY_EXERCISES + i` |

`EXERCISES[50]` reserves 50 consecutive slots; C reads exercise i as `dict_find(received, MESSAGE_KEY_EXERCISES + i)`, JS writes it as `dict[keys.EXERCISES + i]` via `require('message_keys')`. **Changing the key list requires `pebble clean`** (the `MESSAGE_KEY_*` macros are generated from `package.json`).

Clay has no dynamic-list component, so `config.js` renders `NUM_EXERCISE_SLOTS` (12) fixed input fields with Clay-only keys (`EX0`..) that are **not** in `package.json`. `index.js` reads them with `getSettings(response, false)`, drops blanks, and maps the survivors onto `EXERCISES[i]` + `EXERCISE_COUNT`. The watch caps the count at `MAX_EXERCISES` (50) and each name at `EXERCISE_LENGTH-1` (19) chars — the Clay input `limit` must match `EXERCISE_LENGTH-1`.

The watch persists every received config to its own storage (`PERSIST_*` keys; `save_config`/`load_config`), so it restores settings on launch without the phone. Clay edits only push on `webviewclosed`; there is no re-send on `ready`, which is why watch-side persistence matters.

### Conditional compilation

- `#ifdef PBL_COLOR` — color scheme: work = orange/black, rest = duke-blue/white; B&W platforms fall back to black/white inversion.
- `#ifdef PBL_HEALTH` — adds the heart-rate `TextLayer` and a `health_service_events_subscribe` handler. `window_load` reserves vertical space for it, so layout math in that function assumes HR may or may not be present.
- `#ifndef PBL_PLATFORM_APLITE` — guards the AppGlance code. AppGlance is a no-op macro on aplite (original B&W Pebble), so leaving it unguarded produces "defined but not used" warnings there. Don't switch this to `#if PBL_API_EXISTS(...)` — that macro expands to `defined()` and trips `-Wexpansion-to-defined` on the GCC 14 toolchain.

`deinit()` writes an AppGlance ("Last used …") so the launcher shows recency.

## Conventions

- Defaults (work 90s, rest 30s, 4 rounds, 4 named exercises) live in **three** places that must stay in sync: `set_default_config()` in the C file (used on first launch before any config), the `DEFAULT_*`/`DEFAULT_EXERCISES` constants in `config.js` (the Clay form defaults), and the `*_DEFAULT` slider values in `config.js`.
- No magic numbers: C constants are `#define`/`static const` at the top of the file (timing, buffer sizes, `LAYOUT_*`, `PERSIST_*`); JS tunables are named constants at the top of `config.js`.
- The code logs liberally with `APP_LOG(APP_LOG_LEVEL_DEBUG, ...)` on every transition — keep this; it's the main debugging affordance.
