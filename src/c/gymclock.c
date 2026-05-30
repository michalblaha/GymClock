// GymClock - customizable interval timer for Pebble.
// Author: Michal Bláha. Based on the work-out watchapp by Samuel Rinnetmäki.

#include <pebble.h>
#include "workout_logic.h"

// ---- Configuration constants ----
#define MAX_EXERCISES 50
#define EXERCISE_LENGTH 20          // includes the NUL; Clay limits input to 19 chars

#define DEFAULT_WORK 90
#define DEFAULT_REST 30
#define DEFAULT_REPEAT 4
#define DEFAULT_EXERCISE_COUNT 4

#define TIMER_INTERVAL_MS 1000
#define VIBE_INTERVAL_SECONDS 30    // double pulse every N seconds while counting down
#define VIBE_COUNTDOWN_SECONDS 5    // short pulse on each of the final seconds

#define APP_MESSAGE_INBOX_SIZE 1024
#define APP_MESSAGE_OUTBOX_SIZE 64

// Text buffer sizes
#define TIME_TEXT_LEN 6
#define LAP_TEXT_LEN 48
#define NEXT_TEXT_LEN 32
#define HR_TEXT_LEN 12

// Layout (pixels). Elements are stacked top-down from a small top margin. HR sits
// at the very top (enlarged) and the lap line is enlarged too; on tall displays
// (emery) they use the biggest practical fonts, on shorter B&W displays they fall
// back a size so nothing overflows vertically.
#define LAYOUT_TALL_THRESHOLD 200   // emery is 228 tall; the rest are 168/180
#define LAYOUT_TOP_MARGIN 4
#define LAYOUT_HR_HEIGHT_BIG 34
#define LAYOUT_HR_HEIGHT_SMALL 28
#define LAYOUT_LAP_HEIGHT_BIG 48
#define LAYOUT_LAP_HEIGHT_SMALL 30
#define LAYOUT_TIME_HEIGHT 52
#define LAYOUT_EXERCISE_HEIGHT 30

// Persistent storage keys (watch-side; survives relaunch without the phone)
#define PERSIST_WORK_KEY 1
#define PERSIST_REST_KEY 2
#define PERSIST_REPEAT_KEY 3
#define PERSIST_COUNT_KEY 4
#define PERSIST_EXERCISE_BASE 100   // exercise i stored at PERSIST_EXERCISE_BASE + i

static Window *window;
static TextLayer *exercise_layer;
static TextLayer *time_layer;
static TextLayer *lap_layer;
static TextLayer *next_layer;
static GFont lap_font;       // custom Roboto Regular 42 for the lap line (tall displays only)
static AppTimer *timer;
#ifdef PBL_HEALTH
static TextLayer *hr_layer;
#endif

static int seconds;
static int default_work;
static int default_rest;
static int default_repeat;
static const char work[5] = "work";
static const char rest[5] = "rest";
static int working;
static int resting;
static int paused;
static GColor work_bg;
static GColor work_text;
static GColor rest_bg;
static GColor rest_text;

static char exercise[MAX_EXERCISES][EXERCISE_LENGTH];
static int exercises;
static int current_exercise;
static int lap;
static const char empty[2] = "";

// Forward declarations for mutually-referencing handlers.
static void reset(void);
static void workout_complete(void);

static void show_time(void) {
  static char time_text[TIME_TEXT_LEN];
  snprintf(time_text, sizeof(time_text), "%02d", seconds);
  text_layer_set_text(time_layer, time_text);
  if (working || resting) {
    if (seconds == 0) {
      vibes_long_pulse();
    }
    else if ((seconds % VIBE_INTERVAL_SECONDS) == 0) {
      vibes_double_pulse();
    }
    if ((seconds > 0) && (seconds <= VIBE_COUNTDOWN_SECONDS)) {
      vibes_short_pulse();
    }
  }
}

static void update_lap_text(void) {
  static char lap_text[LAP_TEXT_LEN];
  if (lap) {
    snprintf(lap_text, sizeof(lap_text), "%d/%d  Lap %d", current_exercise + 1, exercises, lap);
    text_layer_set_text(lap_layer, lap_text);
  }
  else {
    text_layer_set_text(lap_layer, empty);
  }
}

static void update_next_text(int index) {
  static char next_text[NEXT_TEXT_LEN];
  snprintf(next_text, sizeof(next_text), "Next: %s", exercise[index]);
  text_layer_set_text(next_layer, next_text);
}

static void set_colors(const char *mode) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting color scheme to %s", mode);
  if (strcmp(mode, work) == 0) {
    window_set_background_color(window, work_bg);
    text_layer_set_background_color(time_layer, work_bg);
    text_layer_set_text_color(time_layer, work_text);
    text_layer_set_background_color(exercise_layer, work_bg);
    text_layer_set_text_color(exercise_layer, work_text);
    text_layer_set_background_color(lap_layer, work_bg);
    text_layer_set_text_color(lap_layer, work_text);
    text_layer_set_background_color(next_layer, work_bg);
    text_layer_set_text_color(next_layer, work_text);
#ifdef PBL_HEALTH
    text_layer_set_background_color(hr_layer, work_bg);
    text_layer_set_text_color(hr_layer, work_text);
#endif
  }
  else {
    window_set_background_color(window, rest_bg);
    text_layer_set_background_color(time_layer, rest_bg);
    text_layer_set_text_color(time_layer, rest_text);
    text_layer_set_background_color(exercise_layer, rest_bg);
    text_layer_set_text_color(exercise_layer, rest_text);
    text_layer_set_background_color(lap_layer, rest_bg);
    text_layer_set_text_color(lap_layer, rest_text);
    text_layer_set_background_color(next_layer, rest_bg);
    text_layer_set_text_color(next_layer, rest_text);
#ifdef PBL_HEALTH
    text_layer_set_background_color(hr_layer, rest_bg);
    text_layer_set_text_color(hr_layer, rest_text);
#endif
  }
}

// Advance to the next exercise and start its work interval.
static void begin_next_work(void) {
  set_colors(work);
  working = 1;
  resting = 0;
  seconds = default_work;
  wl_advance(&current_exercise, &lap, exercises);
  update_lap_text();
  text_layer_set_text(exercise_layer, exercise[current_exercise]);
  text_layer_set_text(next_layer, empty);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Started %s (lap %d)", exercise[current_exercise], lap);
}

static void timer_callback(void *data) {
  (void)data;
  seconds--;
  if (seconds <= 0) {
    if (working) {
      // A work interval just ended. If this was the final exercise of the final
      // round, finish the workout instead of resting into a new round.
      if (wl_is_last(current_exercise, exercises, lap, default_repeat)) {
        workout_complete();
        return;
      }
      if (default_rest <= 0) {
        // Rest disabled: go straight to the next exercise, no rest interval.
        begin_next_work();
      } else {
        set_colors(rest);
        working = 0;
        resting = 1;
        seconds = default_rest;
        text_layer_set_text(exercise_layer, "Rest");
        int next_exercise = wl_next_index(current_exercise, exercises);
        update_next_text(next_exercise);
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Changed mode to Rest, next %s", exercise[next_exercise]);
      }
    }
    else {
      begin_next_work();
    }
  }
  timer = app_timer_register(TIMER_INTERVAL_MS, timer_callback, NULL);
  show_time();
}

static void reset(void) {
  if (timer) {
    app_timer_cancel(timer);
    timer = NULL;
  }
  working = 0;
  resting = 0;
  paused = 0;
  current_exercise = 0;
  lap = 0;
  seconds = default_work;
  set_colors(rest);
  text_layer_set_text(exercise_layer, empty);
  show_time();
  update_next_text(current_exercise);
  update_lap_text();
}

static void workout_complete(void) {
  if (timer) {
    app_timer_cancel(timer);
    timer = NULL;
  }
  vibes_long_pulse();
  reset(); // returns to a clean idle state so SELECT restarts cleanly
  text_layer_set_text(exercise_layer, "Done!");
  text_layer_set_text(next_layer, "SEL to restart");
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Workout complete after %d rounds", default_repeat);
}

static void start_or_pause(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  if (paused || (seconds < 0) || ((working == 0) && (resting == 0))) {
    if (lap == 0) {
      lap = 1;
      update_lap_text();
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Start: %02d", seconds);
    working = 1;
    resting = 0;
    paused = 0;
    seconds--;
    show_time();
    set_colors(work);
    text_layer_set_text(next_layer, empty);
    text_layer_set_text(exercise_layer, exercise[current_exercise]);
    if (timer) {
      app_timer_cancel(timer);
    }
    timer = app_timer_register(TIMER_INTERVAL_MS, timer_callback, NULL);
  }
  else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Pause: %02d", seconds);
    paused = 1;
    if (timer) {
      app_timer_cancel(timer);
      timer = NULL;
    }
    text_layer_set_text(next_layer, "SEL to continue");
  }
}

static void stop(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Stop: %02d", seconds);
  vibes_short_pulse();
  reset();
}

static void stop_message(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Reset... %02d", seconds);
  text_layer_set_text(next_layer, "Reset");
}

static void skip_back(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  vibes_short_pulse();
  if (timer) {
    app_timer_cancel(timer);
    timer = NULL;
  }
  if ((seconds == default_work) && ((lap > 1) || (current_exercise > 1))) {
    wl_retreat(&current_exercise, &lap, exercises);
  }
  seconds = default_work;
  working = 0;
  resting = 0;
  paused = 0;
  set_colors(rest);
  text_layer_set_text(exercise_layer, empty);
  update_next_text(current_exercise);
  show_time();
  update_lap_text();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Skip back to lap %d, exercise %d/%d", lap, current_exercise, exercises);
}

static void skip_back_message(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Skipping back... %02d", seconds);
  text_layer_set_text(next_layer, "Skip back");
}

static void skip(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  vibes_short_pulse();
  if (timer) {
    app_timer_cancel(timer);
    timer = NULL;
  }
  wl_advance(&current_exercise, &lap, exercises);
  seconds = default_work;
  working = 0;
  resting = 0;
  paused = 0;
  set_colors(rest);
  text_layer_set_text(exercise_layer, empty);
  update_next_text(current_exercise);
  show_time();
  update_lap_text();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Skip to lap %d, exercise %d/%d", lap, current_exercise, exercises);
}

static void skip_message(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Skipping...");
  text_layer_set_text(next_layer, "Skip exercise");
}

static void click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, start_or_pause);
  window_long_click_subscribe(BUTTON_ID_UP, 0, skip_back_message, skip_back);
  window_long_click_subscribe(BUTTON_ID_DOWN, 0, skip_message, skip);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, stop_message, stop);
}

static void save_config(void) {
  persist_write_int(PERSIST_WORK_KEY, default_work);
  persist_write_int(PERSIST_REST_KEY, default_rest);
  persist_write_int(PERSIST_REPEAT_KEY, default_repeat);
  persist_write_int(PERSIST_COUNT_KEY, exercises);
  for (int i = 0; i < exercises; i++) {
    persist_write_string(PERSIST_EXERCISE_BASE + i, exercise[i]);
  }
}

static void set_default_config(void) {
  default_work = DEFAULT_WORK;
  default_rest = DEFAULT_REST;
  default_repeat = DEFAULT_REPEAT;
  exercises = DEFAULT_EXERCISE_COUNT;
  strncpy(exercise[0], "Push-ups", EXERCISE_LENGTH - 1);
  strncpy(exercise[1], "Sit-ups", EXERCISE_LENGTH - 1);
  strncpy(exercise[2], "Lunges", EXERCISE_LENGTH - 1);
  strncpy(exercise[3], "Pull-ups", EXERCISE_LENGTH - 1);
  for (int i = 0; i < DEFAULT_EXERCISE_COUNT; i++) {
    exercise[i][EXERCISE_LENGTH - 1] = '\0';
  }
}

static void load_config(void) {
  if (!persist_exists(PERSIST_WORK_KEY)) {
    set_default_config();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "No stored config, using defaults");
    return;
  }
  default_work = persist_read_int(PERSIST_WORK_KEY);
  default_rest = persist_read_int(PERSIST_REST_KEY);
  default_repeat = persist_read_int(PERSIST_REPEAT_KEY);
  exercises = persist_read_int(PERSIST_COUNT_KEY);
  if (exercises > MAX_EXERCISES) {
    exercises = MAX_EXERCISES;
  }
  if (exercises < 0) {
    exercises = 0;
  }
  for (int i = 0; i < exercises; i++) {
    if (persist_exists(PERSIST_EXERCISE_BASE + i)) {
      persist_read_string(PERSIST_EXERCISE_BASE + i, exercise[i], EXERCISE_LENGTH);
    } else {
      exercise[i][0] = '\0';
    }
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded config: work %d, rest %d, repeat %d, %d exercises",
          default_work, default_rest, default_repeat, exercises);
}

static void in_received_handler(DictionaryIterator *received, void *context) {
  (void)context;
  Tuple *t;

  t = dict_find(received, MESSAGE_KEY_WORK);
  if (t) {
    default_work = t->value->int32;
  }
  t = dict_find(received, MESSAGE_KEY_REST);
  if (t) {
    default_rest = t->value->int32;
  }
  t = dict_find(received, MESSAGE_KEY_REPEAT);
  if (t) {
    default_repeat = t->value->int32;
  }
  t = dict_find(received, MESSAGE_KEY_EXERCISE_COUNT);
  if (t) {
    exercises = t->value->int32;
    if (exercises > MAX_EXERCISES) {
      exercises = MAX_EXERCISES;
    }
    if (exercises < 0) {
      exercises = 0;
    }
  }

  for (int i = 0; i < exercises; i++) {
    Tuple *ex = dict_find(received, MESSAGE_KEY_EXERCISES + i);
    if (ex) {
      strncpy(exercise[i], ex->value->cstring, EXERCISE_LENGTH - 1);
      exercise[i][EXERCISE_LENGTH - 1] = '\0';
    } else {
      exercise[i][0] = '\0';
    }
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Configured: work %d, rest %d, repeat %d, %d exercises",
          default_work, default_rest, default_repeat, exercises);
  save_config();
  reset();
}

static void in_dropped_handler(AppMessageResult reason, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message from phone dropped: %d", reason);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);
  bool tall = bounds.size.h >= LAYOUT_TALL_THRESHOLD;
  int w = bounds.size.w;
  int y = LAYOUT_TOP_MARGIN;

#ifdef PBL_HEALTH
  int hr_h = tall ? LAYOUT_HR_HEIGHT_BIG : LAYOUT_HR_HEIGHT_SMALL;
  hr_layer = text_layer_create(GRect(0, y, w, hr_h));
  text_layer_set_font(hr_layer,
                      fonts_get_system_font(tall ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(hr_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(hr_layer));
  y += hr_h;
#endif

  int lap_h = tall ? LAYOUT_LAP_HEIGHT_BIG : LAYOUT_LAP_HEIGHT_SMALL;
  lap_layer = text_layer_create(GRect(0, y, w, lap_h));
  if (tall) {
    lap_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_REGULAR_42));
    text_layer_set_font(lap_layer, lap_font);
  } else {
    text_layer_set_font(lap_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
  }
  text_layer_set_text_alignment(lap_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(lap_layer, GTextOverflowModeFill);
  layer_add_child(window_layer, text_layer_get_layer(lap_layer));
  y += lap_h;

  time_layer = text_layer_create(GRect(0, y, w, LAYOUT_TIME_HEIGHT));
  text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(time_layer));
  y += LAYOUT_TIME_HEIGHT;

  exercise_layer = text_layer_create(GRect(0, y, w, LAYOUT_EXERCISE_HEIGHT));
  text_layer_set_font(exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(exercise_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(exercise_layer));
  y += LAYOUT_EXERCISE_HEIGHT;

  next_layer = text_layer_create(GRect(0, y, w, bounds.size.h - y));
  text_layer_set_font(next_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(next_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(next_layer));

  reset();
}

static void window_unload(Window *window) {
  (void)window;
  text_layer_destroy(time_layer);
  text_layer_destroy(exercise_layer);
  text_layer_destroy(next_layer);
  text_layer_destroy(lap_layer);
  if (lap_font) {
    fonts_unload_custom_font(lap_font);
    lap_font = NULL;
  }
#ifdef PBL_HEALTH
  text_layer_destroy(hr_layer);
#endif
}

#ifdef PBL_HEALTH
static void prv_on_health_data(HealthEventType type, void *context) {
  (void)context;
  if (type == HealthEventHeartRateUpdate) {
    HealthValue heart_rate = health_service_peek_current_value(HealthMetricHeartRateBPM);
    static char s_hrm_buffer[HR_TEXT_LEN];
    snprintf(s_hrm_buffer, sizeof(s_hrm_buffer), "%lu BPM", (uint32_t) heart_rate);
    text_layer_set_text(hr_layer, s_hrm_buffer);
  }
}
#endif

static void init(void) {
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_open(APP_MESSAGE_INBOX_SIZE, APP_MESSAGE_OUTBOX_SIZE);

#ifdef PBL_COLOR
  rest_bg = GColorDukeBlue;
  rest_text = GColorWhite;
  work_bg = GColorOrange;
  work_text = GColorBlack;
#else
  rest_bg = GColorWhite;
  rest_text = GColorBlack;
  work_bg = GColorBlack;
  work_text = GColorWhite;
#endif

#ifdef PBL_HEALTH
  health_service_events_subscribe(prv_on_health_data, NULL);
#endif

  working = 0;
  resting = 0;
  current_exercise = 0;
  lap = 0;
  load_config();

  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(window, animated);
}

#ifndef PBL_PLATFORM_APLITE // AppGlance is unavailable on the original aplite hardware
static void prv_update_app_glance(AppGlanceReloadSession *session, size_t limit, void *context) {
  (void)limit;
  (void)context;
  static char message[80];
  time_t t = time(NULL);
  struct tm *tms;
  tms = localtime(&t);
  strftime(message, sizeof(message), "Last used %b %d %H:%M", tms);
  const AppGlanceSlice slice = (AppGlanceSlice) {
    .layout = {
      .icon = APP_GLANCE_SLICE_DEFAULT_ICON,
      .subtitle_template_string = message
    },
    .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION
  };
  AppGlanceResult result = app_glance_add_slice(session, slice);
  if (result != APP_GLANCE_RESULT_SUCCESS) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Error adding AppGlanceSlice: %d", result);
  }
}
#endif // !PBL_PLATFORM_APLITE

static void deinit(void) {
#ifndef PBL_PLATFORM_APLITE
  app_glance_reload(prv_update_app_glance, NULL);
#endif
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
