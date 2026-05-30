#include "workout_logic.h"

int wl_next_index(int current, int count) {
  if (count <= 0) {
    return 0;
  }
  int next = current + 1;
  if (next >= count) {
    next = 0;
  }
  return next;
}

bool wl_advance(int *current, int *lap, int count) {
  (*current)++;
  if (*current >= count) {
    *current = 0;
    (*lap)++;
    return true;
  }
  return false;
}

bool wl_retreat(int *current, int *lap, int count) {
  (*current)--;
  if (*current < 0) {
    *current = count - 1;
    (*lap)--;
    return true;
  }
  return false;
}

bool wl_is_last(int current, int count, int lap, int repeat) {
  if (repeat <= 0 || count <= 0) {
    return false; // loop forever / nothing configured
  }
  return (lap >= repeat) && (current >= count - 1);
}
