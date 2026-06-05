#pragma once
#include <stdbool.h>

// Pure workout state arithmetic, free of any Pebble SDK dependency so it can be
// unit-tested on the host (see tests/). All functions operate on plain ints.

// Index of the exercise that follows `current` in a list of `count` items,
// wrapping back to 0. Returns 0 if count <= 0.
int wl_next_index(int current, int count);

// Advance to the next exercise. Increments *current; on wrap-around resets it to
// 0 and increments *lap. Returns true if it wrapped (i.e. a lap completed).
bool wl_advance(int *current, int *lap, int count);

// Step back to the previous exercise. Decrements *current; on underflow wraps to
// count-1 and decrements *lap. Returns true if it wrapped.
bool wl_retreat(int *current, int *lap, int count);

// True when `current` is the final exercise of the final lap, i.e. the workout
// should finish after this exercise completes. `repeat` <= 0 means "loop forever"
// (the workout never finishes on its own).
bool wl_is_last(int current, int count, int lap, int repeat);

// The two timed phases of a running workout.
typedef enum {
  WL_REST = 0,
  WL_WORK = 1,
} WlPhase;

// Result of fast-forwarding a running workout (see wl_resume).
typedef struct {
  WlPhase phase;  // phase of the interval in progress now
  int current;    // exercise index of that interval
  int lap;        // lap of that interval
  int seconds;    // seconds left in that interval (>= 1; 0 when finished)
  bool finished;  // true if the workout completed during the elapsed time
} WlResumeResult;

// Fast-forward a running workout so it can be resumed after the app was closed.
//
// At the reference moment the workout is in `phase` for exercise `current` /
// `lap`, with `seconds_until_end` seconds left until that interval ends (may be
// <= 0 if it already ended while the app was gone). `work`/`rest` are the
// interval durations (rest <= 0 => no-rest mode: work flows straight into the
// next exercise); `count` exercises per lap, `repeat` rounds (<= 0 = forever).
//
// Returns the interval in progress now and how many seconds remain in it, or
// finished=true if the workout would already have completed. `work` must be > 0
// and `count` > 0 (the caller guarantees an active list); otherwise the result
// echoes the inputs unchanged.
WlResumeResult wl_resume(WlPhase phase, int current, int lap, int seconds_until_end,
                         int work, int rest, int count, int repeat);
