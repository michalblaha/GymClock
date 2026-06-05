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
tools/screenshot.sh emery -b select             # helper: install + drive buttons + screenshot (see -h); auto-retries a wedged emulator (--wipe)
pebble logs --emulator emery                    # view APP_LOG output (the code logs every state change)
pebble install --phone <ip>                     # install on a real watch via the Pebble app
pebble clean                                    # wipe build/; required after changing messageKeys in package.json
```

Active toolchain is **SDK 4.9.169** (Pebble Tool v5). The bundle is `build/GymClock.pbw` (name derives from the project dir, not the `name`/`displayName` fields). A clean build emits no warnings from our code; the only remaining one is an SDK-level `LOAD segment with RWX permissions` linker note, inherent to every Pebble app.

**Tests** live in `tests/`. `src/c/workout_logic.{h,c}` is a deliberately `pebble.h`-free module holding the index/lap arithmetic and the workout-finished decision; `tests/test_workout_logic.c` exercises it via a tiny assert harness compiled with the system `cc -Werror`. Put any new pure logic there so it stays unit-testable — UI/vibration/timer code that needs `pebble.h` can only be checked in QEMU. No linter is wired up yet; `clang-format`/`clang-tidy` over `src/c/**` and ESLint over `src/pkjs/**` are welcome (keep configs at repo root).

## Architecture

Two-process design typical of Pebble apps:

- **`src/c/gymclock.c`** — the watch-side C app. Single window, seven `TextLayer`s: HR / lap / big timer / current exercise / next-or-status stacked top-down in `window_load` from `LAYOUT_*` constants, plus two overlays — a transient `notice_layer` ("Resumed") at the very top and a small `version_layer` pinned to the very bottom. `next_layer` has two positions (`next_frame_normal` under the timer during a workout, `next_frame_idle` centred between the timer and the version label on the idle screen — measured once with `graphics_text_layout_get_content_size` and swapped by `place_next()`). The layout is **adaptive**: a `tall` flag (`bounds.size.h >= LAYOUT_TALL_THRESHOLD`, true for emery) picks larger fonts for HR (28-bold) and the lap line on the big screen and steps them down on the 168/180px B&W displays so nothing overflows. On the big screen the lap line uses a **custom font** — `RESOURCE_ID_ROBOTO_REGULAR_32` (Roboto Regular 32px, bundled from `resources/fonts/Roboto-Regular.ttf`, Apache-2.0, glyph set trimmed via `characterRegex` to the chars the lap line needs); it is `fonts_load_custom_font`-ed in `window_load` and unloaded in `window_unload`. Smaller screens fall back to system `GOTHIC_28`. The lap line uses `GTextOverflowModeFill` and a compact `"L %d | %d/%d"` format. No custom drawing or GPaths.
- **`src/c/workout_logic.{h,c}`** — pure, `pebble.h`-free state arithmetic (`wl_advance`, `wl_retreat`, `wl_next_index`, `wl_is_last`). Compiled into the app (wscript globs `src/c/**/*.c`) *and* into the host tests. Keep it dependency-free.
- **`src/pkjs/config.js`** — the **Clay** form definition. Exports `{ items, NUM_EXERCISE_SLOTS, exerciseSlotKey }`. Tunables (slot count, slider ranges, defaults, input length limit) are constants at the top.
- **`src/pkjs/index.js`** — phone-side glue. Initializes Clay with `autoHandleEvents:false`, opens `clay.generateUrl()` on `showConfiguration`, and on `webviewclosed` reads raw values via `clay.getSettings(response, false)` and builds the AppMessage by hand.
- **`wscript`** — standard Pebble waf build; globs `src/c/**/*.c` and bundles `src/pkjs/**` (`.js` + `.json`). Also injects the app version from `package.json` into the C build as `-DAPP_VERSION` (`_app_version()` → `ctx.env.append_value('DEFINES', ...)`), so the version is never duplicated in source; `gymclock.c` stringifies it into `VERSION_LABEL` and `#error`s if it is missing. Otherwise rarely needs editing.

### State machine (the core of the C code)

Global flags `working` / `resting` / `paused` plus a `seconds` countdown drive everything. `timer_callback` fires every second, decrements `seconds`, and on reaching 0 flips work↔rest: switching to rest previews the next exercise (`wl_next_index`); switching back to work advances via `wl_advance(&current_exercise, &lap, exercises)` (wrap increments `lap`) and recolors via `set_colors()`. `reset()` is the single source of truth for the idle/zeroed state (config receipt, stop, a paused DOWN-reset, and workout completion) and also `clear_session()`s the persisted resume snapshot.

**No-rest mode:** when `default_rest <= 0` the rest phase is skipped entirely — `begin_next_work()` (shared by the rest→work transition) runs straight after a work interval, so work flows directly into the next exercise. The Clay `REST` slider allows 0.

**Finite rounds:** `REPEAT` is enforced. When a work interval ends, `wl_is_last(current, count, lap, repeat)` decides whether this was the last exercise of the last lap; if so, `workout_complete()` stops the timer, long-pulses, resets, and shows "Done!" / "SEL to restart". `repeat <= 0` means loop forever. (This is the one intentional behavior change from the original, which always looped.)

Button mapping (`click_config_provider`): SELECT = start/pause, long SELECT = stop+reset, long UP = skip back one exercise, long DOWN = skip forward, **single UP (from the idle/armed screen) = open in-watch settings**, **single DOWN = reset, but only while paused** (`down_reset`; ignored otherwise so it never disturbs a running countdown — long DOWN likewise resets instead of skipping when paused). The long-click handlers show a hint label first, then act on release. `skip`/`skip_back`/`reset` all null out `timer` after cancelling it (avoids cancelling a freed timer later). The idle screen shows the `IDLE_HINT` (`SEL: Start / UP: Settings`) vertically centred between the big timer and the bottom version label (`place_next(true)`); the paused screen shows `PAUSED_HINT` (`"Paused - SEL: go / DOWN: reset"`) in the normal under-timer position.

**Resume after relaunch.** Pebble kills the foreground app when another app opens, so the workout state is persisted and rebuilt on launch instead of resetting. A compact session snapshot (`PERSIST_SESSION_*`: state, working/resting, exercise, lap, value) is written **only while the timer is running or paused** — `save_running()` at each interval boundary/start (VALUE = the interval's absolute end-time) and `save_frozen()` on pause (VALUE = frozen seconds). Idle, done, and armed-after-skip states `clear_session()`, so a skip is intentionally not resumable. On launch `restore_or_reset()` (called from `window_load` in place of `reset()`) reads the snapshot and validates it against the current active list: a running clock is fast-forwarded by the real wall-clock time the app was gone via the pure, host-tested `wl_resume()` (it replays work↔rest transitions and lands on "Done!" if the workout would already have finished); a paused session is restored verbatim showing `PAUSED_HINT`; `render_time()` paints without the vibration side-effects of `show_time()` so resuming never fires a stray pulse. A running resume also flashes `notice_layer` — a bold top banner (`NOTICE_TEXT` "Resumed") overlaid above the HR/lap line and auto-hidden after `NOTICE_TIMEOUT_MS` (10 s) by `notice_timer`/`notice_timeout`; starting/skipping and `window_unload` hide it early. The same `notice_layer` doubles as the app title bar: on the idle screen `reset()` calls `show_idle_title()` to pin `TITLE_TEXT` ("GymClock") there persistently, in the identical font/size/style (the two states never coincide). This only catches the clock up *on return* — nothing runs or vibrates while the app is closed (a background worker can't vibrate on this SDK, so it would buy nothing here).

### In-watch settings

A separate `settings_window` (pushed onto the stack, opened only when idle) hosts a `MenuLayer` with four rows — Work, Rest, Rounds, Exercises. The first three push a built-in `NumberWindow` (ranges in `SETTING_*`, mirroring the Clay sliders); values are written **live** in the `incremented`/`decremented` callbacks into `default_work`/`default_rest`/`default_repeat`, so Select or Back both keep the change. On the settings window's `unload` it calls `save_config()` + `sync_to_phone()` + `reset()`.

The **Exercises** row opens `exercises_window`, a second `MenuLayer` over the stored list; Select toggles `stored_enabled[i]` (✓ "On" / "Skipped") and calls `rebuild_active()`. Exercise **names** are never editable on-watch (no keyboard) — only their enabled flag — so naming stays in Clay. Windows are created lazily and reused (destroyed in `deinit`); their menus/NumberWindows are created in `load` and freed in `unload`.

### Stored vs active exercise list

There are two lists. The **stored** list (`stored_name[]`, `stored_enabled[]`, `stored_count`) is the editable source of truth (settings, Clay, persistence). The **active** list (`exercise[]`, `exercises`) is what the timer cycles, rebuilt by `rebuild_active()` = the entries that are `enabled && name != ""`. So a disabled exercise behaves exactly like an unnamed one: kept in storage, filtered out of the workout. The timer code only ever touches the active list, so it stayed unchanged when skip/disable was added. `start_or_pause` guards against an empty active list ("No exercises").

### Config message protocol (Clay + named keys)

Config travels as one AppMessage with **named** `messageKeys`, declared as a list in `package.json`:

| Key | Meaning |
|-----|---------|
| `WORK` | work seconds |
| `REST` | rest seconds |
| `REPEAT` | number of rounds (laps) |
| `EXERCISE_COUNT` | number of exercises sent |
| `EXERCISE_ENABLED` | bitmask: bit i = exercise i enabled (skip/disable state) |
| `EXERCISES[50]` | array key: exercise i at `MESSAGE_KEY_EXERCISES + i` |

`EXERCISES[50]` reserves 50 consecutive slots; C reads exercise i as `dict_find(received, MESSAGE_KEY_EXERCISES + i)`, JS writes it as `dict[keys.EXERCISES + i]` via `require('message_keys')`. **Changing the key list requires `pebble clean`** (the `MESSAGE_KEY_*` macros are generated from `package.json`).

Clay has no dynamic-list component, so `config.js` renders `NUM_EXERCISE_SLOTS` (12) fixed input fields plus an `Enabled` toggle each, with Clay-only keys (`EX0`.. / `EXEN0`..) that are **not** in `package.json`. `index.js` reads them with `getSettings(response, false)` and sends every **named** slot (enabled or not) compacted into `EXERCISES[i]` + `EXERCISE_COUNT`, with `EXERCISE_ENABLED` a bitmask whose bit = the slot's position in that compacted list. Empty slots are dropped. The watch caps the count at `MAX_EXERCISES` (50) and each name at `EXERCISE_LENGTH-1` (19) chars — the Clay input `limit` must match `EXERCISE_LENGTH-1`.

**Bidirectional sync.** On settings close the watch calls `sync_to_phone()`, sending `WORK`/`REST`/`REPEAT` + the `EXERCISE_ENABLED` bitmask back via the outbox (no names — they never change on-watch). `index.js`'s `appmessage` handler maps the bitmask back onto the Clay slots (re-running the same name-order compaction read from `localStorage['clay-settings']`) and writes them with `clay.setSettings(...)`, so the config page reflects watch-side skip toggles. The enabled bitmask is capped at 32 entries (matches the int32 AppMessage value); persisted on the watch as one `persist_write_int` under `PERSIST_ENABLED_KEY`. Missing key (config from before Phase 2) ⇒ all enabled.

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
