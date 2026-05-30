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
  function flag(key) {
    var item = settings[key];
    return item ? !!item.value : true; // default enabled when missing
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

  // Send every named slot (enabled or not); a disabled exercise keeps its name
  // but is flagged off in the bitmask so the watch skips it. Empty slots are
  // dropped entirely. Bit position = index in the compacted list.
  var exercises = [];
  var enabledMask = 0;
  for (var i = 0; i < config.NUM_EXERCISE_SLOTS; i++) {
    var name = str(config.exerciseSlotKey(i));
    if (name) {
      if (flag(config.exerciseEnabledKey(i))) {
        enabledMask |= (1 << exercises.length);
      }
      exercises.push(name);
    }
  }

  var dict = {};
  dict[keys.WORK] = work;
  dict[keys.REST] = rest;
  dict[keys.REPEAT] = repeat;
  dict[keys.EXERCISE_COUNT] = exercises.length;
  dict[keys.EXERCISE_ENABLED] = enabledMask;
  for (var j = 0; j < exercises.length; j++) {
    dict[keys.EXERCISES + j] = exercises[j];
  }

  Pebble.sendAppMessage(dict, function() {
    console.log('Sent workout config to Pebble: ' + JSON.stringify(dict));
  }, function(err) {
    console.log('Failed to send workout config: ' + JSON.stringify(err));
  });
});

// Watch -> phone sync: when settings are changed on the watch it pushes back the
// timing values and the enabled bitmask. Exercise names never change on-watch,
// so we map the bitmask onto the existing Clay slots (by name, same compaction
// the watch uses) and write the result into Clay's stored settings.
Pebble.addEventListener('appmessage', function(e) {
  var p = e && e.payload;
  if (!p) {
    return;
  }
  var update = {};
  if (p.WORK != null) { update.WORK = p.WORK; }
  if (p.REST != null) { update.REST = p.REST; }
  if (p.REPEAT != null) { update.REPEAT = p.REPEAT; }

  if (p.EXERCISE_ENABLED != null) {
    var stored = {};
    try {
      stored = JSON.parse(localStorage.getItem('clay-settings')) || {};
    } catch (x) {
      stored = {};
    }
    var mask = p.EXERCISE_ENABLED;
    var j = 0;
    for (var i = 0; i < config.NUM_EXERCISE_SLOTS; i++) {
      var name = stored[config.exerciseSlotKey(i)];
      if (name && ('' + name).trim() !== '') {
        update[config.exerciseEnabledKey(i)] = ((mask >> j) & 1) === 1;
        j++;
      }
    }
  }

  clay.setSettings(update);
  console.log('Synced settings from watch into Clay: ' + JSON.stringify(update));
});
