# SpineAnimationPlayer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a runtime `SpineAnimationPlayer` node that fires a single `notified` signal at precise frames of a spine animation — for both Godot-authored notifies and spine-native events — so a game can trigger VFX/gameplay without a custom engine build.

**Architecture:** A passive observer node, child of a `SpineSprite3D`, that hooks the sprite's existing per-frame `before_animation_state_apply` signal (Godot notifies via track-time crossing) and `animation_event` signal (spine events), normalizing both into `notified(name, time, payload)`. Notify data lives in a reusable `SpineNotifyTrack` resource holding `SpineNotify` items. No second animation state or skeleton is created.

**Tech Stack:** C++ (spine-godot GDExtension + module dual-build), Godot 4.7, GDScript headless tests.

## Global Constraints

- Every new `.cpp`/`.h` starts with the Spine Runtimes License header — copy verbatim from `spine-godot/spine_godot/SpineBoneNode3D.h` lines 1–28.
- All three classes are gated `#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)` (they reference `SpineSprite3D`), matching `SpineBoneNode3D`/`SpineSlotNode3D`.
- Dual-build split uses `#ifdef SPINE_GODOT_EXTENSION` (godot-cpp headers) vs `#else` (module headers), following existing files.
- **Module build** auto-globs `spine_godot/*.cpp` (no build-file edit). **Extension build** needs each new `.cpp` added explicitly to `spine-godot/SConstruct` (the `Glob` is disabled).
- Build command (extension, from `spine-godot/build/`): `./dev-extension.ps1` (PowerShell). Installs the DLL into `example-v4-extension/bin/windows/`.
- Headless test command: `godot --headless --path spine-godot/example-v4-extension "res://<scene>.tscn"`. A test passes when it prints a line containing `TEST-OK` and no `TEST-FAIL`.
- Node base class is `Node` (the player is non-spatial). Resources extend `Resource`.
- Property `name` on `SpineNotify` is exposed as **`notify_name`** (avoids confusion with `Resource`/`Node` naming); the `notified` signal carries its value as the arg `name`.

---

## File structure

- Create `spine-godot/spine_godot/SpineNotify.h` / `.cpp` — one notify (data).
- Create `spine-godot/spine_godot/SpineNotifyTrack.h` / `.cpp` — reusable list of notifies.
- Create `spine-godot/spine_godot/SpineAnimationPlayer.h` / `.cpp` — the observer node.
- Modify `spine-godot/spine_godot/register_types.cpp` — includes + `GDREGISTER_CLASS` (3D-gated blocks).
- Modify `spine-godot/SConstruct` — `sources.append(...)` for the three new `.cpp` (after line 119).
- Create test scenes/scripts under `spine-godot/example-v4-extension/` (removed after each task's verification).

---

## Task 1: `SpineNotify` + `SpineNotifyTrack` resources

**Files:**
- Create: `spine-godot/spine_godot/SpineNotify.h`, `spine-godot/spine_godot/SpineNotify.cpp`
- Create: `spine-godot/spine_godot/SpineNotifyTrack.h`, `spine-godot/spine_godot/SpineNotifyTrack.cpp`
- Modify: `spine-godot/spine_godot/register_types.cpp`
- Modify: `spine-godot/SConstruct`
- Test: `spine-godot/example-v4-extension/_t_notify.gd` + `_t_notify.tscn`

**Interfaces:**
- Produces: `SpineNotify` with `String get/set_animation_name`, `float get/set_time`, `String get/set_notify_name`, `String get/set_channel`, `Dictionary get/set_payload`. `SpineNotifyTrack` with `Array get/set_notifies` (array of `SpineNotify`).

- [ ] **Step 1: Write the failing test** — `spine-godot/example-v4-extension/_t_notify.gd`

```gdscript
extends Node
func _ready() -> void:
	var n := SpineNotify.new()
	n.animation_name = "walk"
	n.time = 0.5
	n.notify_name = "footstep"
	n.channel = "vfx"
	n.payload = { "bone": "gun-tip" }
	var track := SpineNotifyTrack.new()
	track.notifies = [n]
	var ok := (track.notifies.size() == 1
		and track.notifies[0].notify_name == "footstep"
		and abs(track.notifies[0].time - 0.5) < 1e-6
		and track.notifies[0].payload.get("bone") == "gun-tip")
	print("TEST-OK" if ok else "TEST-FAIL notify roundtrip")
	get_tree().quit()
```

And `_t_notify.tscn`:
```
[gd_scene load_steps=2 format=3]
[ext_resource type="Script" path="res://_t_notify.gd" id="1"]
[node name="T" type="Node"]
script = ExtResource("1")
```

- [ ] **Step 2: Run to verify it fails**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_notify.tscn"`
Expected: FAIL — parser/runtime error `Identifier "SpineNotify" not declared` (class not registered yet).

- [ ] **Step 3: Create `SpineNotify.h`** (after the license header)

```cpp
#pragma once
#include "SpineCommon.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/resource.hpp>
#else
#include "core/io/resource.h"
#endif

// One "notify": a named trigger placed at a time on a specific animation.
class SpineNotify : public Resource {
	GDCLASS(SpineNotify, Resource)

protected:
	String animation_name;
	float time = 0.0f;
	String notify_name;
	String channel = "default";
	Dictionary payload;
	static void _bind_methods();

public:
	void set_animation_name(const String &v) { animation_name = v; }
	String get_animation_name() const { return animation_name; }
	void set_time(float v) { time = v; }
	float get_time() const { return time; }
	void set_notify_name(const String &v) { notify_name = v; }
	String get_notify_name() const { return notify_name; }
	void set_channel(const String &v) { channel = v; }
	String get_channel() const { return channel; }
	void set_payload(const Dictionary &v) { payload = v; }
	Dictionary get_payload() const { return payload; }
};
#endif
```

- [ ] **Step 4: Create `SpineNotify.cpp`** (after the license header)

```cpp
#include "SpineCommon.h"
#include "SpineNotify.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

void SpineNotify::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animation_name", "v"), &SpineNotify::set_animation_name);
	ClassDB::bind_method(D_METHOD("get_animation_name"), &SpineNotify::get_animation_name);
	ClassDB::bind_method(D_METHOD("set_time", "v"), &SpineNotify::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &SpineNotify::get_time);
	ClassDB::bind_method(D_METHOD("set_notify_name", "v"), &SpineNotify::set_notify_name);
	ClassDB::bind_method(D_METHOD("get_notify_name"), &SpineNotify::get_notify_name);
	ClassDB::bind_method(D_METHOD("set_channel", "v"), &SpineNotify::set_channel);
	ClassDB::bind_method(D_METHOD("get_channel"), &SpineNotify::get_channel);
	ClassDB::bind_method(D_METHOD("set_payload", "v"), &SpineNotify::set_payload);
	ClassDB::bind_method(D_METHOD("get_payload"), &SpineNotify::get_payload);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "animation_name"), "set_animation_name", "get_animation_name");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "time"), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "notify_name"), "set_notify_name", "get_notify_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "channel"), "set_channel", "get_channel");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "payload"), "set_payload", "get_payload");
}
#endif
```

(`VARIANT_FLOAT` is the codebase macro for the float variant type — see `SpineSprite3D.cpp` usage.)

- [ ] **Step 5: Create `SpineNotifyTrack.h`** (after the license header)

```cpp
#pragma once
#include "SpineCommon.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/resource.hpp>
#else
#include "core/io/resource.h"
#endif

// A reusable per-character list of notifies (assign to a SpineAnimationPlayer).
class SpineNotifyTrack : public Resource {
	GDCLASS(SpineNotifyTrack, Resource)

protected:
	Array notifies;// of SpineNotify
	static void _bind_methods();

public:
	void set_notifies(const Array &v) { notifies = v; }
	Array get_notifies() const { return notifies; }
};
#endif
```

- [ ] **Step 6: Create `SpineNotifyTrack.cpp`** (after the license header)

```cpp
#include "SpineCommon.h"
#include "SpineNotifyTrack.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

void SpineNotifyTrack::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_notifies", "v"), &SpineNotifyTrack::set_notifies);
	ClassDB::bind_method(D_METHOD("get_notifies"), &SpineNotifyTrack::get_notifies);
	// Typed-array inspector hint: Array of SpineNotify resources.
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "notifies", PROPERTY_HINT_ARRAY_TYPE,
							  vformat("%d/%d:%s", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpineNotify")),
				 "set_notifies", "get_notifies");
}
#endif
```

- [ ] **Step 7: Register both classes** in `spine-godot/spine_godot/register_types.cpp`

Add to the 3D-gated include block (the one around line 76 that already `#include "SpineSlotNode3D.h"` / `"SpineBoneNode3D.h"`):
```cpp
#include "SpineNotify.h"
#include "SpineNotifyTrack.h"
```
Add to the 3D-gated register block (the one around line 198 that already registers `SpineSlotNode3D`/`SpineBoneNode3D`), inside the same `#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)`:
```cpp
	GDREGISTER_CLASS(SpineNotify);
	GDREGISTER_CLASS(SpineNotifyTrack);
```

- [ ] **Step 8: Add sources to the extension build** in `spine-godot/SConstruct` (immediately after line 119 `sources.append("spine_godot/SpineBoneNode3D.cpp")`)

```python
sources.append("spine_godot/SpineNotify.cpp")
sources.append("spine_godot/SpineNotifyTrack.cpp")
```

- [ ] **Step 9: Build**

Run (from `spine-godot/build/`): `./dev-extension.ps1`
Expected: compiles `SpineNotify.cpp` + `SpineNotifyTrack.cpp`, links, installs DLL. No errors.

- [ ] **Step 10: Run the test to verify it passes**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_notify.tscn"`
Expected: prints `TEST-OK`.

- [ ] **Step 11: Remove test files, then commit**

```bash
rm spine-godot/example-v4-extension/_t_notify.gd spine-godot/example-v4-extension/_t_notify.gd.uid spine-godot/example-v4-extension/_t_notify.tscn
git add spine-godot/spine_godot/SpineNotify.h spine-godot/spine_godot/SpineNotify.cpp spine-godot/spine_godot/SpineNotifyTrack.h spine-godot/spine_godot/SpineNotifyTrack.cpp spine-godot/spine_godot/register_types.cpp spine-godot/SConstruct
git commit -m "feat(spine-godot): add SpineNotify + SpineNotifyTrack resources

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: `SpineAnimationPlayer` node — attach + properties + signal (inert)

**Files:**
- Create: `spine-godot/spine_godot/SpineAnimationPlayer.h`, `spine-godot/spine_godot/SpineAnimationPlayer.cpp`
- Modify: `spine-godot/spine_godot/register_types.cpp`, `spine-godot/SConstruct`
- Test: `spine-godot/example-v4-extension/_t_player.gd` + `_t_player.tscn`

**Interfaces:**
- Consumes: `SpineNotifyTrack` (Task 1); `SpineSprite3D::get_animation_state()`, signals `before_animation_state_apply(sprite)` and `animation_event(sprite, state, entry, event)`.
- Produces: node `SpineAnimationPlayer` with `set/get_notify_track(Ref<SpineNotifyTrack>)`, `set/get_forward_spine_events(bool)`, signal `notified(String name, float time, Dictionary payload)`, methods `play(String, bool, int)` and `seek(float, int)` (defined in Task 5 — declared here).

- [ ] **Step 1: Write the failing test** — `_t_player.gd`

```gdscript
extends Node
func _ready() -> void:
	var sprite := SpineSprite3D.new()
	var player := SpineAnimationPlayer.new()
	sprite.add_child(player)                     # PARENTED -> should connect, no crash
	add_child(sprite)
	var has_signal := player.has_signal("notified")
	var ff_default := player.forward_spine_events # default true
	player.forward_spine_events = false
	var ff_set := (player.forward_spine_events == false)
	var ok := has_signal and ff_default and ff_set
	print("TEST-OK" if ok else "TEST-FAIL player basic")
	get_tree().quit()
```

`_t_player.tscn`: same shape as `_t_notify.tscn` but pointing at `_t_player.gd`, root node type `Node`.

- [ ] **Step 2: Run to verify it fails**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_player.tscn"`
Expected: FAIL — `Identifier "SpineAnimationPlayer" not declared`.

- [ ] **Step 3: Create `SpineAnimationPlayer.h`** (after license header)

```cpp
#pragma once
#include "SpineCommon.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)
#include "SpineSprite3D.h"
#include "SpineNotifyTrack.h"
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/node.hpp>
#else
#include "scene/main/node.h"
#endif

class SpineAnimationPlayer : public Node {
	GDCLASS(SpineAnimationPlayer, Node)

	static const int MAX_TRACKS = 32;

protected:
	Ref<SpineNotifyTrack> notify_track;
	bool forward_spine_events = true;

	// Per-track crossing state.
	float prev_time[MAX_TRACKS];
	String prev_anim[MAX_TRACKS];
	bool discontinuity_pending = true;// reset all tracks on next observe (set on parent/seek/play)

	static void _bind_methods();
	void _notification(int what);
	void on_before_apply(const Variant &sprite);
	void on_spine_event(const Variant &sprite, const Variant &state, const Variant &entry, const Variant &event);
	void emit_forward(const String &anim, float lo, float hi);// fire notifies with lo < time <= hi

public:
	SpineAnimationPlayer() {
		for (int i = 0; i < MAX_TRACKS; i++) prev_time[i] = 0.0f;
	}

	void set_notify_track(const Ref<SpineNotifyTrack> &v) { notify_track = v; }
	Ref<SpineNotifyTrack> get_notify_track() const { return notify_track; }
	void set_forward_spine_events(bool v) { forward_spine_events = v; }
	bool get_forward_spine_events() const { return forward_spine_events; }

	void play(const String &animation_name, bool loop, int track);
	void seek(float time, int track);
};
#endif
```

- [ ] **Step 4: Create `SpineAnimationPlayer.cpp`** (after license header) — attach + bindings only; firing bodies are stubs filled in Tasks 3–5.

```cpp
#include "SpineCommon.h"
#include "SpineAnimationPlayer.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

void SpineAnimationPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_before_apply", "spine_sprite"), &SpineAnimationPlayer::on_before_apply);
	ClassDB::bind_method(D_METHOD("_on_spine_event", "spine_sprite", "animation_state", "track_entry", "event"), &SpineAnimationPlayer::on_spine_event);
	ClassDB::bind_method(D_METHOD("set_notify_track", "v"), &SpineAnimationPlayer::set_notify_track);
	ClassDB::bind_method(D_METHOD("get_notify_track"), &SpineAnimationPlayer::get_notify_track);
	ClassDB::bind_method(D_METHOD("set_forward_spine_events", "v"), &SpineAnimationPlayer::set_forward_spine_events);
	ClassDB::bind_method(D_METHOD("get_forward_spine_events"), &SpineAnimationPlayer::get_forward_spine_events);
	ClassDB::bind_method(D_METHOD("play", "animation_name", "loop", "track"), &SpineAnimationPlayer::play, DEFVAL(true), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("seek", "time", "track"), &SpineAnimationPlayer::seek, DEFVAL(0));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "notify_track", PROPERTY_HINT_RESOURCE_TYPE, "SpineNotifyTrack"), "set_notify_track", "get_notify_track");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "forward_spine_events"), "set_forward_spine_events", "get_forward_spine_events");

	ADD_SIGNAL(MethodInfo("notified",
						  PropertyInfo(Variant::STRING, "name"),
						  PropertyInfo(VARIANT_FLOAT, "time"),
						  PropertyInfo(Variant::DICTIONARY, "payload")));
}

void SpineAnimationPlayer::_notification(int what) {
	switch (what) {
		case NOTIFICATION_PARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
				sprite->connect(SNAME("before_animation_state_apply"), callable_mp(this, &SpineAnimationPlayer::on_before_apply));
				sprite->connect(SNAME("animation_event"), callable_mp(this, &SpineAnimationPlayer::on_spine_event));
				discontinuity_pending = true;
			} else {
				WARN_PRINT("SpineAnimationPlayer parent is not a SpineSprite3D.");
			}
			break;
		}
		case NOTIFICATION_UNPARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
				sprite->disconnect(SNAME("before_animation_state_apply"), callable_mp(this, &SpineAnimationPlayer::on_before_apply));
				sprite->disconnect(SNAME("animation_event"), callable_mp(this, &SpineAnimationPlayer::on_spine_event));
			}
			break;
		}
		default:
			break;
	}
}

// Filled in Task 4/5.
void SpineAnimationPlayer::on_before_apply(const Variant &_sprite) {}
void SpineAnimationPlayer::emit_forward(const String &anim, float lo, float hi) {}
// Filled in Task 3.
void SpineAnimationPlayer::on_spine_event(const Variant &_sprite, const Variant &_state, const Variant &_entry, const Variant &_event) {}
// Filled in Task 5.
void SpineAnimationPlayer::play(const String &animation_name, bool loop, int track) {}
void SpineAnimationPlayer::seek(float time, int track) {}
#endif
```

- [ ] **Step 5: Register + add source**

`register_types.cpp`: add `#include "SpineAnimationPlayer.h"` to the 3D-gated include block; add `GDREGISTER_CLASS(SpineAnimationPlayer);` to the 3D-gated register block (after the `SpineNotifyTrack` line from Task 1).
`SConstruct`: after the Task-1 appends, add `sources.append("spine_godot/SpineAnimationPlayer.cpp")`.

- [ ] **Step 6: Build**

Run: `./dev-extension.ps1` → no errors.

- [ ] **Step 7: Run the test to verify it passes**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_player.tscn"`
Expected: `TEST-OK`.

- [ ] **Step 8: Remove test files + commit**

```bash
rm spine-godot/example-v4-extension/_t_player.gd spine-godot/example-v4-extension/_t_player.gd.uid spine-godot/example-v4-extension/_t_player.tscn
git add spine-godot/spine_godot/SpineAnimationPlayer.h spine-godot/spine_godot/SpineAnimationPlayer.cpp spine-godot/spine_godot/register_types.cpp spine-godot/SConstruct
git commit -m "feat(spine-godot): add SpineAnimationPlayer node (attach + inert)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Spine-event forwarding

**Files:**
- Modify: `spine-godot/spine_godot/SpineAnimationPlayer.cpp` (fill `on_spine_event`)
- Test: `spine-godot/example-v4-extension/_t_events.gd` + `.tscn`

**Interfaces:**
- Consumes: `animation_event` args — `SpineEvent` with `get_data().get_event_name()`, `get_time()`, `get_int_value()`, `get_float_value()`, `get_string_value()`, `get_volume()`, `get_balance()`.
- Produces: `notified("<event_name>", <event_time>, { source:"spine_event", int, float, string, volume, balance })`.

- [ ] **Step 1: Write the failing test** — `_t_events.gd`

```gdscript
extends Node
var got := []
func _ready() -> void:
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new()
	sprite.skeleton_data_res = data
	var player := SpineAnimationPlayer.new()
	sprite.add_child(player)
	add_child(sprite)
	player.notified.connect(_on_notified)
	sprite.get_animation_state().set_animation("run", true, 0)  # run has footstep events
	# advance ~0.7s so both footsteps (0.233, 0.567) pass
	for i in range(45):
		sprite.update_skeleton(1.0 / 60.0)
	var footsteps := got.filter(func(e): return e.name == "footstep" and e.payload.get("source") == "spine_event")
	print("TEST-OK" if footsteps.size() >= 2 else "TEST-FAIL got %d footsteps" % footsteps.size())
	get_tree().quit()
func _on_notified(name: String, time: float, payload: Dictionary) -> void:
	got.append({ "name": name, "time": time, "payload": payload })
```

`_t_events.tscn` points at `_t_events.gd`.

- [ ] **Step 2: Run to verify it fails**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_events.tscn"`
Expected: FAIL — `got 0 footsteps` (forwarding not implemented).

- [ ] **Step 3: Implement `on_spine_event`** — replace the stub in `SpineAnimationPlayer.cpp`

```cpp
void SpineAnimationPlayer::on_spine_event(const Variant &_sprite, const Variant &_state, const Variant &_entry, const Variant &_event) {
	if (!forward_spine_events) return;
	Ref<SpineEvent> event = _event;
	if (event.is_null()) return;
	Ref<SpineEventData> data = event->get_data();
	if (data.is_null()) return;
	Dictionary payload;
	payload["source"] = "spine_event";
	payload["int"] = event->get_int_value();
	payload["float"] = event->get_float_value();
	payload["string"] = event->get_string_value();
	payload["volume"] = event->get_volume();
	payload["balance"] = event->get_balance();
	emit_signal(SNAME("notified"), data->get_event_name(), event->get_time(), payload);
}
```

Add includes near the top of `SpineAnimationPlayer.cpp` (inside the `#if`), following the dual-build split:
```cpp
#include "SpineEvent.h"
#include "SpineEventData.h"
```

- [ ] **Step 4: Build**

Run: `./dev-extension.ps1` → no errors.

- [ ] **Step 5: Run the test to verify it passes**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_events.tscn"`
Expected: `TEST-OK`.

- [ ] **Step 6: Remove test files + commit**

```bash
rm spine-godot/example-v4-extension/_t_events.gd spine-godot/example-v4-extension/_t_events.gd.uid spine-godot/example-v4-extension/_t_events.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/spine_godot/SpineAnimationPlayer.cpp
git commit -m "feat(spine-godot): SpineAnimationPlayer forwards spine events into notified

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Godot notify crossing — forward play + loop wrap

**Files:**
- Modify: `spine-godot/spine_godot/SpineAnimationPlayer.cpp` (fill `on_before_apply`, `emit_forward`)
- Test: `spine-godot/example-v4-extension/_t_cross.gd` + `.tscn`

**Interfaces:**
- Consumes: `SpineSprite3D::get_animation_state()` → `SpineAnimationState::get_num_tracks()`, `get_track(i)` → `SpineTrackEntry`; `SpineTrackEntry::get_animation()` → `SpineAnimation` (`get_name()`, `get_duration()`), `get_animation_time()`, `get_loop()`.
- Produces: `notified(notify_name, time, payload + {source:"notify"})` when the playhead crosses a notify.

- [ ] **Step 1: Write the failing test** — `_t_cross.gd`

```gdscript
extends Node
var counts := {}
func _mk(anim, t, nm):
	var n := SpineNotify.new(); n.animation_name = anim; n.time = t; n.notify_name = nm; return n
func _ready() -> void:
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data
	var player := SpineAnimationPlayer.new()
	player.forward_spine_events = false                      # isolate Godot notifies
	var track := SpineNotifyTrack.new()
	track.notifies = [ _mk("walk", 0.10, "a"), _mk("walk", 0.30, "b") ]
	player.notify_track = track
	sprite.add_child(player); add_child(sprite)
	player.notified.connect(func(nm, t, p): counts[nm] = counts.get(nm, 0) + 1)
	sprite.get_animation_state().set_animation("walk", true, 0) # walk duration ~1.0s
	for i in range(90):                                        # ~1.5 loops
		sprite.update_skeleton(1.0 / 60.0)
	# 'a' and 'b' should each fire ~1-2 times (fired at least once per pass; looped once)
	var ok := counts.get("a", 0) >= 1 and counts.get("b", 0) >= 1 and counts.get("a", 0) <= 3
	print("TEST-OK a=%d b=%d" % [counts.get("a",0), counts.get("b",0)] if ok else "TEST-FAIL a=%d b=%d" % [counts.get("a",0), counts.get("b",0)])
	get_tree().quit()
```

`_t_cross.tscn` points at `_t_cross.gd`.

- [ ] **Step 2: Run to verify it fails**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_cross.tscn"`
Expected: FAIL — `a=0 b=0` (crossing not implemented).

- [ ] **Step 3: Implement `emit_forward` + `on_before_apply`** — replace the two stubs

```cpp
void SpineAnimationPlayer::emit_forward(const String &anim, float lo, float hi) {
	if (notify_track.is_null()) return;
	Array notifies = notify_track->get_notifies();
	for (int i = 0; i < notifies.size(); i++) {
		Ref<SpineNotify> n = notifies[i];
		if (n.is_null() || n->get_animation_name() != anim) continue;
		float t = n->get_time();
		if (t > lo && t <= hi) {
			Dictionary payload = n->get_payload().duplicate();
			payload["source"] = "notify";
			emit_signal(SNAME("notified"), n->get_notify_name(), t, payload);
		}
	}
}

void SpineAnimationPlayer::on_before_apply(const Variant &_sprite) {
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (!sprite) return;
	Ref<SpineAnimationState> state = sprite->get_animation_state();
	if (state.is_null() || !state->get_spine_object()) return;

	int n = state->get_num_tracks();
	if (n > MAX_TRACKS) n = MAX_TRACKS;
	for (int i = 0; i < n; i++) {
		Ref<SpineTrackEntry> entry = state->get_track(i);
		if (entry.is_null() || !entry->get_spine_object()) { prev_anim[i] = String(); continue; }
		Ref<SpineAnimation> anim = entry->get_animation();
		if (anim.is_null()) { prev_anim[i] = String(); continue; }
		String anim_name = anim->get_name();
		float cur = entry->get_animation_time();
		float dur = anim->get_duration();

		// Discontinuity: first-seen, animation changed, or a global reset (seek/play). Fire nothing.
		if (discontinuity_pending || prev_anim[i] != anim_name) {
			prev_anim[i] = anim_name;
			prev_time[i] = cur;
			continue;
		}
		float prev = prev_time[i];
		if (cur >= prev) {
			emit_forward(anim_name, prev, cur);// normal forward
		} else if (entry->get_loop()) {
			emit_forward(anim_name, prev, dur);// wrapped: tail of the cycle...
			emit_forward(anim_name, -1.0f, cur);// ...then head of the next
		}
		prev_time[i] = cur;
	}
	discontinuity_pending = false;
}
```

Add includes (dual-build split, inside the `#if`):
```cpp
#include "SpineAnimationState.h"
#include "SpineTrackEntry.h"
#include "SpineAnimation.h"
```

- [ ] **Step 4: Build**

Run: `./dev-extension.ps1` → no errors.

- [ ] **Step 5: Run the test to verify it passes**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_cross.tscn"`
Expected: `TEST-OK a=… b=…` with both ≥ 1.

- [ ] **Step 6: Remove test files + commit**

```bash
rm spine-godot/example-v4-extension/_t_cross.gd spine-godot/example-v4-extension/_t_cross.gd.uid spine-godot/example-v4-extension/_t_cross.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/spine_godot/SpineAnimationPlayer.cpp
git commit -m "feat(spine-godot): SpineAnimationPlayer fires Godot notifies (forward + loop)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: reverse, seek discontinuity, and `play()`/`seek()`

**Files:**
- Modify: `spine-godot/spine_godot/SpineAnimationPlayer.cpp` (reverse branch in `on_before_apply`; fill `play`/`seek`)
- Test: `spine-godot/example-v4-extension/_t_ctrl.gd` + `.tscn`

**Interfaces:**
- Consumes: `SpineTrackEntry::get_time_scale()`, `set_track_time(float)`; `SpineAnimationState::set_animation(name, loop, track)` → `SpineTrackEntry`.
- Produces: `play(animation_name, loop=true, track=0)`, `seek(time, track=0)`; reverse-play notify firing; no retro-fire after `seek()`.

- [ ] **Step 1: Write the failing test** — `_t_ctrl.gd`

```gdscript
extends Node
var counts := {}
func _mk(anim, t, nm):
	var n := SpineNotify.new(); n.animation_name = anim; n.time = t; n.notify_name = nm; return n
func _ready() -> void:
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data
	var player := SpineAnimationPlayer.new(); player.forward_spine_events = false
	var track := SpineNotifyTrack.new()
	track.notifies = [ _mk("walk", 0.20, "a"), _mk("walk", 0.60, "b") ]
	player.notify_track = track
	sprite.add_child(player); add_child(sprite)
	player.notified.connect(func(nm, t, p): counts[nm] = counts.get(nm, 0) + 1)
	# play() convenience drives the sprite:
	player.play("walk", true, 0)
	for i in range(20): sprite.update_skeleton(1.0/60.0)   # ~0.33s -> 'a' fires, not 'b'
	var a_before := counts.get("a", 0)
	# seek forward PAST 'b' — must NOT retro-fire 'b'
	player.seek(0.90, 0)
	sprite.update_skeleton(1.0/60.0)
	var b_after_seek := counts.get("b", 0)
	var ok := a_before >= 1 and b_after_seek == 0
	print("TEST-OK a=%d b=%d" % [a_before, b_after_seek] if ok else "TEST-FAIL a=%d b=%d" % [a_before, b_after_seek])
	get_tree().quit()
```

`_t_ctrl.tscn` points at `_t_ctrl.gd`.

- [ ] **Step 2: Run to verify it fails**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_ctrl.tscn"`
Expected: FAIL — `play`/`seek` are stubs (compile OK but `play` does nothing → `a=0`), or `b` retro-fires.

- [ ] **Step 3: Implement `play` + `seek`** — replace the stubs

```cpp
void SpineAnimationPlayer::play(const String &animation_name, bool loop, int track) {
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (!sprite) return;
	Ref<SpineAnimationState> state = sprite->get_animation_state();
	if (state.is_null() || !state->get_spine_object()) return;
	state->set_animation(animation_name, loop, track);
	discontinuity_pending = true;// the new animation starts fresh; don't retro-fire
}

void SpineAnimationPlayer::seek(float time, int track) {
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (!sprite) return;
	Ref<SpineAnimationState> state = sprite->get_animation_state();
	if (state.is_null() || !state->get_spine_object()) return;
	Ref<SpineTrackEntry> entry = state->get_track(track);
	if (entry.is_null() || !entry->get_spine_object()) return;
	entry->set_track_time(time);
	discontinuity_pending = true;// jump: resync, fire nothing for the skipped span
}
```

- [ ] **Step 4: Add the reverse branch** in `on_before_apply` — change the `else if (entry->get_loop())` block so reverse is handled first:

```cpp
		if (cur >= prev) {
			emit_forward(anim_name, prev, cur);
		} else if (entry->get_time_scale() < 0.0f) {
			// reverse: playhead moved backward without wrapping; fire notifies in [cur, prev)
			if (!notify_track.is_null()) {
				Array notifies = notify_track->get_notifies();
				for (int k = 0; k < notifies.size(); k++) {
					Ref<SpineNotify> nt = notifies[k];
					if (nt.is_null() || nt->get_animation_name() != anim_name) continue;
					float t = nt->get_time();
					if (t >= cur && t < prev) {
						Dictionary payload = nt->get_payload().duplicate();
						payload["source"] = "notify";
						emit_signal(SNAME("notified"), nt->get_notify_name(), t, payload);
					}
				}
			}
		} else if (entry->get_loop()) {
			emit_forward(anim_name, prev, dur);
			emit_forward(anim_name, -1.0f, cur);
		}
		// else: cur < prev, not looping, not reverse -> treated as a jump; fire nothing.
```

- [ ] **Step 5: Build**

Run: `./dev-extension.ps1` → no errors.

- [ ] **Step 6: Run the test to verify it passes**

Run: `godot --headless --path spine-godot/example-v4-extension "res://_t_ctrl.tscn"`
Expected: `TEST-OK a=… b=0`.

- [ ] **Step 7: Remove test files + commit**

```bash
rm spine-godot/example-v4-extension/_t_ctrl.gd spine-godot/example-v4-extension/_t_ctrl.gd.uid spine-godot/example-v4-extension/_t_ctrl.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/spine_godot/SpineAnimationPlayer.cpp
git commit -m "feat(spine-godot): SpineAnimationPlayer reverse/seek handling + play()/seek()

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for the executor

- **Include style:** the codebase uses `#include "SpineXxx.h"` for its own wrappers (they handle the module/extension header split internally). Only the base-engine headers (`Node`, `Resource`) need the `#ifdef SPINE_GODOT_EXTENSION` split shown above.
- **`get_spine_object()` guards:** every Spine wrapper (`SpineAnimationState`, `SpineTrackEntry`, `SpineAnimation`, `SpineEvent`) can be a valid `Ref` wrapping a null spine object mid-teardown — always guard `!x->get_spine_object()` (mirrors existing code in `SpineBoneNode3D.cpp`).
- **Module parity:** no module build is run here, but keep the `#ifdef` splits correct so the module keeps compiling (it auto-globs the new files).
- **Editor icons** (optional, not in this plan): the three classes can get SVGs later via the `[icons]` block in `spine_godot_extension.gdextension`.
