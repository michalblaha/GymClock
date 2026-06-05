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

WlResumeResult wl_resume(WlPhase phase, int current, int lap, int seconds_until_end,
                         int work, int rest, int count, int repeat) {
  WlResumeResult r = { phase, current, lap, seconds_until_end, false };
  if (work <= 0 || count <= 0) {
    return r; // misconfigured; nothing safe to fast-forward
  }
  if (seconds_until_end > 0) {
    return r; // still inside the current interval, no transitions missed
  }

  // The current interval has already ended; `overshoot` is how long ago.
  int overshoot = -seconds_until_end;
  for (;;) {
    // Step from the just-ended interval into the next one.
    if (r.phase == WL_WORK) {
      if (wl_is_last(r.current, count, r.lap, repeat)) {
        r.finished = true;
        r.seconds = 0;
        return r; // workout ends exactly at this boundary
      }
      if (rest <= 0) {
        wl_advance(&r.current, &r.lap, count); // no-rest: straight to next work
        r.phase = WL_WORK;
        r.seconds = work;
      } else {
        r.phase = WL_REST; // rest keeps the same exercise/lap (rest before next)
        r.seconds = rest;
      }
    } else {
      wl_advance(&r.current, &r.lap, count); // rest done: advance into next work
      r.phase = WL_WORK;
      r.seconds = work;
    }

    if (overshoot < r.seconds) {
      r.seconds -= overshoot; // we land inside this interval
      return r;
    }
    overshoot -= r.seconds; // this interval also fully elapsed; keep walking
  }
}
