// Phone-side PebbleKit JS for the GymClock interval timer.
//
// Configuration is handled by Clay (src/pkjs/config.js). We disable Clay's
// automatic event handling so we can map the fixed exercise input slots onto
// the EXERCISES[] array message key manually. WORK / REST / REPEAT are declared
// as named message keys in package.json and are addressed via require('message_keys').
//
// The watch persists the received configuration in its own storage, so we only
// need to push settings when the user actually edits them (webviewclosed).

var Clay = require('@rebble/clay');
var keys = require('message_keys');
var config = require('./config');
var clay = new Clay(config.items, null, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) {
    return; // configuration cancelled, nothing to send
  }

  // convert=false -> object keyed by messageKey string, each value is { value: ... }
  var settings = clay.getSettings(e.response, false);

  function num(key) {
    var item = settings[key];
    return item ? parseInt(item.value, 10) : NaN;
  }
  function str(key) {
    var item = settings[key];
    return (item && item.value != null) ? ('' + item.value).trim() : '';
  }

  var work = num('WORK');
  var rest = num('REST');
  var repeat = num('REPEAT');

  // Fail loud instead of inventing values: Clay always supplies in-range
  // defaults, so NaN here means something is genuinely wrong.
  if (isNaN(work) || isNaN(rest) || isNaN(repeat)) {
    console.log('Invalid timing values from Clay, not sending: ' + JSON.stringify(settings));
    return;
  }

  var exercises = [];
  for (var i = 0; i < config.NUM_EXERCISE_SLOTS; i++) {
    var name = str(config.exerciseSlotKey(i));
    if (name) {
      exercises.push(name);
    }
  }

  var dict = {};
  dict[keys.WORK] = work;
  dict[keys.REST] = rest;
  dict[keys.REPEAT] = repeat;
  dict[keys.EXERCISE_COUNT] = exercises.length;
  for (var j = 0; j < exercises.length; j++) {
    dict[keys.EXERCISES + j] = exercises[j];
  }

  Pebble.sendAppMessage(dict, function() {
    console.log('Sent workout config to Pebble: ' + JSON.stringify(dict));
  }, function(err) {
    console.log('Failed to send workout config: ' + JSON.stringify(err));
  });
});
