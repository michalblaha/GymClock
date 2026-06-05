// Host-side unit tests for the pure workout logic (no Pebble SDK needed).
// Build & run:  ./tests/run_tests.sh   (or see the cc command inside it)

#include <stdio.h>
#include "../src/c/workout_logic.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
  checks++; \
  if (!(cond)) { \
    failures++; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
  } \
} while (0)

static void test_next_index(void) {
  CHECK(wl_next_index(0, 4) == 1);
  CHECK(wl_next_index(2, 4) == 3);
  CHECK(wl_next_index(3, 4) == 0); // wraps
  CHECK(wl_next_index(0, 1) == 0); // single item wraps to itself
  CHECK(wl_next_index(5, 0) == 0); // empty list guarded
}

static void test_advance(void) {
  int current = 0, lap = 1;
  CHECK(wl_advance(&current, &lap, 4) == false);
  CHECK(current == 1 && lap == 1);

  current = 3; lap = 1;
  CHECK(wl_advance(&current, &lap, 4) == true); // wraps
  CHECK(current == 0 && lap == 2);

  current = 0; lap = 1;
  CHECK(wl_advance(&current, &lap, 1) == true); // single-exercise round
  CHECK(current == 0 && lap == 2);
}

static void test_retreat(void) {
  int current = 2, lap = 1;
  CHECK(wl_retreat(&current, &lap, 4) == false);
  CHECK(current == 1 && lap == 1);

  current = 0; lap = 2;
  CHECK(wl_retreat(&current, &lap, 4) == true); // wraps back
  CHECK(current == 3 && lap == 1);
}

static void test_is_last(void) {
  // 4 exercises, 4 rounds: finish on exercise index 3 of lap 4.
  CHECK(wl_is_last(3, 4, 4, 4) == true);
  CHECK(wl_is_last(3, 4, 3, 4) == false); // not the last lap yet
  CHECK(wl_is_last(2, 4, 4, 4) == false); // not the last exercise yet
  CHECK(wl_is_last(0, 1, 4, 4) == true);  // single exercise, last lap
  CHECK(wl_is_last(3, 4, 4, 0) == false); // repeat<=0 => infinite
  CHECK(wl_is_last(0, 0, 4, 4) == false); // no exercises
}

static void test_resume(void) {
  // Helper expectations: work=90, rest=30, 4 exercises, 4 rounds unless noted.

  // Still inside the current interval => unchanged, transitions = 0.
  WlResumeResult r = wl_resume(WL_WORK, 0, 1, 30, 90, 30, 4, 4);
  CHECK(r.finished == false && r.phase == WL_WORK && r.current == 0 && r.lap == 1 && r.seconds == 30);

  // Work just ended 5s ago => into rest (same exercise/lap), 25s left.
  r = wl_resume(WL_WORK, 0, 1, -5, 90, 30, 4, 4);
  CHECK(!r.finished && r.phase == WL_REST && r.current == 0 && r.lap == 1 && r.seconds == 25);

  // Past work + rest (35s over the work end) => advance to next work, exercise 1.
  r = wl_resume(WL_WORK, 0, 1, -35, 90, 30, 4, 4);
  CHECK(!r.finished && r.phase == WL_WORK && r.current == 1 && r.lap == 1 && r.seconds == 85);

  // Exactly at the work boundary => rest just starting, full 30s.
  r = wl_resume(WL_WORK, 0, 1, 0, 90, 30, 4, 4);
  CHECK(!r.finished && r.phase == WL_REST && r.current == 0 && r.lap == 1 && r.seconds == 30);

  // Rest ended at the end of a lap => wrap to next lap's first work.
  r = wl_resume(WL_REST, 3, 1, -5, 90, 30, 4, 4);
  CHECK(!r.finished && r.phase == WL_WORK && r.current == 0 && r.lap == 2 && r.seconds == 85);

  // No-rest mode (rest <= 0): two work intervals elapse, land in the third.
  r = wl_resume(WL_WORK, 0, 1, -70, 60, 0, 4, 4);
  CHECK(!r.finished && r.phase == WL_WORK && r.current == 2 && r.lap == 1 && r.seconds == 50);

  // Finite rounds: last exercise of last lap finishing => workout complete.
  r = wl_resume(WL_WORK, 3, 4, -1, 10, 0, 4, 4);
  CHECK(r.finished && r.seconds == 0);

  // Same position but infinite (repeat 0) => never finishes, wraps to lap 5.
  r = wl_resume(WL_WORK, 3, 4, -5, 10, 0, 4, 0);
  CHECK(!r.finished && r.phase == WL_WORK && r.current == 0 && r.lap == 5 && r.seconds == 5);

  // Single-exercise, no-rest, finite: completes when its last lap's work ends.
  r = wl_resume(WL_WORK, 0, 4, -3, 20, 0, 1, 4);
  CHECK(r.finished && r.seconds == 0);

  // Long absence over many intervals stays consistent (no-rest, 4 ex, infinite).
  // From the end of (ex0,lap1), 205s = 10 full 20s intervals + 5s; the 11th
  // advance lands on ex3 of lap3 with 15s left.
  r = wl_resume(WL_WORK, 0, 1, -205, 20, 0, 4, 0);
  CHECK(!r.finished && r.phase == WL_WORK && r.current == 3 && r.lap == 3 && r.seconds == 15);

  // Guards: misconfigured input echoes back unchanged.
  r = wl_resume(WL_WORK, 1, 2, -10, 0, 30, 4, 4); // work <= 0
  CHECK(!r.finished && r.current == 1 && r.lap == 2 && r.seconds == -10);
  r = wl_resume(WL_WORK, 1, 2, -10, 90, 30, 0, 4); // count <= 0
  CHECK(!r.finished && r.current == 1 && r.lap == 2 && r.seconds == -10);
}

int main(void) {
  test_next_index();
  test_advance();
  test_retreat();
  test_is_last();
  test_resume();

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
