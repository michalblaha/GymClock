// Clay configuration page for the GymClock interval timer.
//
// Clay has no built-in repeatable/dynamic list component, so the exercise list
// is presented as a fixed set of single-line input slots. Empty slots are
// skipped when the configuration is sent to the watch (see index.js), so the
// effective exercise count is simply the number of non-empty slots.
//
// The exercise slot inputs intentionally use Clay-only messageKeys ("EX0"..)
// that are NOT declared in package.json: index.js reads them with
// clay.getSettings(response, false) and maps them onto the EXERCISES[] array
// message key manually. WORK / REST / REPEAT map straight to package.json keys.

// --- Configuration constants (no magic numbers elsewhere in this file) ---
var NUM_EXERCISE_SLOTS = 12; // visible exercise input fields (watch hard cap is MAX_EXERCISES = 50)
var EXERCISE_MAX_LEN = 19; // must equal EXERCISE_LENGTH - 1 in gymclock.c (room for NUL)

var WORK_MIN = 5, WORK_MAX = 600, WORK_STEP = 5, WORK_DEFAULT = 90;
var REST_MIN = 0, REST_MAX = 300, REST_STEP = 5, REST_DEFAULT = 30;
var REPEAT_MIN = 1, REPEAT_MAX = 50, REPEAT_STEP = 1, REPEAT_DEFAULT = 4;

var DEFAULT_EXERCISES = ["Push-ups", "Sit-ups", "Lunges", "Pull-ups"];

// Clay-side identifier for exercise slot i. Kept here so index.js can rebuild it.
function exerciseSlotKey(i) {
  return "EX" + i;
}

var items = [
  {
    "type": "heading",
    "defaultValue": "GymClock"
  },
  {
    "type": "text",
    "defaultValue": "Customizable interval timer for your workouts."
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Timing" },
      {
        "type": "slider",
        "messageKey": "WORK",
        "label": "Work (seconds)",
        "defaultValue": WORK_DEFAULT,
        "min": WORK_MIN,
        "max": WORK_MAX,
        "step": WORK_STEP
      },
      {
        "type": "slider",
        "messageKey": "REST",
        "label": "Rest (seconds)",
        "defaultValue": REST_DEFAULT,
        "min": REST_MIN,
        "max": REST_MAX,
        "step": REST_STEP
      },
      {
        "type": "slider",
        "messageKey": "REPEAT",
        "label": "Rounds (laps)",
        "defaultValue": REPEAT_DEFAULT,
        "min": REPEAT_MIN,
        "max": REPEAT_MAX,
        "step": REPEAT_STEP
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Exercises" },
      { "type": "text", "defaultValue": "Fill in order. Leave a field blank to skip it." }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];

// Insert the exercise input slots just before the trailing submit button.
var exerciseSectionItems = items[items.length - 2].items;
for (var i = 0; i < NUM_EXERCISE_SLOTS; i++) {
  exerciseSectionItems.push({
    "type": "input",
    "messageKey": exerciseSlotKey(i),
    "label": "Exercise " + (i + 1),
    "defaultValue": DEFAULT_EXERCISES[i] || "",
    "attributes": {
      "placeholder": "(empty)",
      "limit": EXERCISE_MAX_LEN,
      "type": "text"
    }
  });
}

module.exports = {
  items: items,
  NUM_EXERCISE_SLOTS: NUM_EXERCISE_SLOTS,
  exerciseSlotKey: exerciseSlotKey
};
