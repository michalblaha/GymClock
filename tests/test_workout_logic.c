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

int main(void) {
  test_next_index();
  test_advance();
  test_retreat();
  test_is_last();

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
