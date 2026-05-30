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
