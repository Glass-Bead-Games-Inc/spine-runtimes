# Spine Notify Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A timeline dock to preview a spine animation (scrub/play) and author `SpineNotify` markers on it, backed by two small C++ helpers.

**Architecture:** Two C++ additions to the GDExtension (`SpineSprite3D::pose_at`, `SpineSkeletonDataResource::get_animation_events`), plus a GDScript `@tool` `EditorPlugin` addon (`spine_notify_editor`) whose dock binds to the selected `SpineAnimationPlayer`, drives the preview via `pose_at`, shows the read-only spine events, and edits the player's `SpineNotifyTrack` through `EditorUndoRedoManager`.

**Tech Stack:** C++ (spine-godot GDExtension + module dual-build), GDScript `@tool` EditorPlugin, Godot 4.7.

## Global Constraints

- New `.cpp`/`.h` start with the Spine Runtimes License header (copy verbatim, lines 1–28, from `spine-godot/spine_godot/SpineBoneNode3D.h`).
- C++ classes touched (`SpineSprite3D`, `SpineSkeletonDataResource`) already exist and are already in the extension `SConstruct` — **no `SConstruct` edits** for C++ this plan. `SpineSprite3D` is 3D-gated (`#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)`); `SpineSkeletonDataResource` is not gated.
- spine `String` → Godot `String`: use the codebase idiom (see `SpineEventData.cpp::get_event_name` / `SPINE_STRING`); animation lookups use `SPINE_STRING_TMP(name)` as in `SpineSkeletonDataResource.cpp:481`.
- Build (extension, from `spine-godot/build/`): `./dev-extension.ps1` (PowerShell, **foreground**). Success ends `scons: done building targets.`, no `error:`.
- Headless test (**foreground**, bounded): `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://<scene>.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|error"`. PASS = a `TEST-OK` line, no `TEST-FAIL`. `godot` on PATH is 4.7. (If GDScript 4.7's analyzer errors on `:=` with `abs()`/mixed types, add an explicit type annotation in the disposable test only.)
- The addon lives at `spine-godot/example-v4-extension/addons/spine_notify_editor/`.
- **Editor-UI note:** the interactive dock (drawing, mouse, feel) is NOT headless-testable here. GDScript tasks (3–5) test their **logic methods** headlessly and end with a **manual editor checklist** the human runs; do not claim the UI works from a headless run.
- Commit trailer: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Do not push.

---

## File structure

- Modify `spine-godot/spine_godot/SpineSprite3D.h` / `.cpp` — add `pose_at`.
- Modify `spine-godot/spine_godot/SpineSkeletonDataResource.h` / `.cpp` — add `get_animation_events`.
- Create `spine-godot/example-v4-extension/addons/spine_notify_editor/plugin.cfg`.
- Create `.../addons/spine_notify_editor/plugin.gd` — `EditorPlugin` (lifecycle, selection binding, owns undo/redo).
- Create `.../addons/spine_notify_editor/timeline_dock.gd` — dock: toolbar + owns the view + marker strip.
- Create `.../addons/spine_notify_editor/timeline_view.gd` — timeline canvas: draw + input + the testable logic (mapping, snap, CRUD).

---

## Task 1: `SpineSprite3D::pose_at(animation_name, time)`

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h` (declare), `spine-godot/spine_godot/SpineSprite3D.cpp` (implement + bind)
- Test: `spine-godot/example-v4-extension/_t_poseat.gd` + `.tscn`

**Interfaces:**
- Produces: `void pose_at(const String &animation_name, float time)` — poses the skeleton at `time` of `animation_name` transiently (no `preview_*` change), rebuilding the mesh.

- [ ] **Step 1: Write the failing test** — `_t_poseat.gd`

```gdscript
extends Node
func _ready() -> void:
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new()
	sprite.skeleton_data_res = data
	add_child(sprite)
	var pt_before: float = sprite.preview_time
	sprite.pose_at("walk", 0.0)
	var a: Vector3 = sprite.get_global_bone_transform_3d("front-fist").origin
	sprite.pose_at("walk", 0.5)
	var b: Vector3 = sprite.get_global_bone_transform_3d("front-fist").origin
	var moved: bool = a.distance_to(b) > 0.001
	var no_dirty: bool = (sprite.preview_time == pt_before)     # pose_at must not touch preview_time
	print("TEST-OK" if (moved and no_dirty) else "TEST-FAIL moved=%s no_dirty=%s" % [moved, no_dirty])
	get_tree().quit()
```
`_t_poseat.tscn`: a `Node` root with `script = _t_poseat.gd`.

- [ ] **Step 2: Run to verify it fails**

Run the headless test command on `res://_t_poseat.tscn`.
Expected: FAIL — `Invalid call. Nonexistent function 'pose_at'`.

- [ ] **Step 3: Declare in `SpineSprite3D.h`** (next to `bone_to_transform3d`, inside the 3D-gated public section)

```cpp
	// Editor/manual scrub: pose the skeleton at `time` of `animation_name` and rebuild the mesh, WITHOUT
	// changing the stored preview_* properties (so scrubbing does not dirty the scene).
	void pose_at(const String &animation_name, float time);
```

- [ ] **Step 4: Implement in `SpineSprite3D.cpp`** (near `get_global_bone_transform_3d`)

```cpp
void SpineSprite3D::pose_at(const String &animation_name, float time) {
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;
	if (!animation_state.is_valid() || !animation_state->get_spine_object()) return;
	skeleton->set_to_setup_pose();
	if (animation_name.is_empty()) return;
	Ref<SpineTrackEntry> entry = animation_state->set_animation(animation_name, false, 0);
	if (entry.is_valid() && entry->get_spine_object()) {
		entry->set_mix_duration(0);
		entry->set_time_scale(0);
		entry->set_track_time(time);
	}
	animation_state->update(0);
	animation_state->apply(skeleton);
	skeleton->update_world_transform(SpineConstant::Physics_Update);
	if (is_visible_in_tree()) {
		build_meshes();
		build_debug_mesh();
	}
}
```
(No `is_editor_hint` gate — callable anytime. `SpineTrackEntry.h` is already included by `SpineSprite3D.cpp`; if not, add it inside the `#if`.)

- [ ] **Step 5: Bind in `_bind_methods()`** (in `SpineSprite3D.cpp`, near the other bone-transform binds)

```cpp
	ClassDB::bind_method(D_METHOD("pose_at", "animation_name", "time"), &SpineSprite3D::pose_at);
```

- [ ] **Step 6: Build** — `./dev-extension.ps1` → no errors.

- [ ] **Step 7: Run the test** — `res://_t_poseat.tscn` → `TEST-OK`.

- [ ] **Step 8: Remove test files + commit**

```bash
rm spine-godot/example-v4-extension/_t_poseat.gd spine-godot/example-v4-extension/_t_poseat.gd.uid spine-godot/example-v4-extension/_t_poseat.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "feat(spine-godot): SpineSprite3D::pose_at(anim, time) transient scrub pose

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: `SpineSkeletonDataResource::get_animation_events(animation_name)`

**Files:**
- Modify: `spine-godot/spine_godot/SpineSkeletonDataResource.h` / `.cpp`
- Test: `spine-godot/example-v4-extension/_t_animevents.gd` + `.tscn`

**Interfaces:**
- Produces: `Array get_animation_events(const String &animation_name)` → `[{ "time": float, "name": String }, …]` for the animation's spine events; `[]` if none.

- [ ] **Step 1: Write the failing test** — `_t_animevents.gd`

```gdscript
extends Node
func _ready() -> void:
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var walk: Array = data.get_animation_events("walk")   # spineboy walk has 'footstep' events
	var names := walk.map(func(e): return e.get("name"))
	var has_footstep := "footstep" in names
	var has_time := walk.size() > 0 and walk[0].has("time")
	var empty_anim: Array = data.get_animation_events("death")  # (no events) -> []
	var ok := has_footstep and has_time and empty_anim.size() == 0
	print("TEST-OK n=%d" % walk.size() if ok else "TEST-FAIL footstep=%s time=%s empty=%d" % [has_footstep, has_time, empty_anim.size()])
	get_tree().quit()
```
`_t_animevents.tscn`: `Node` root + the script. (If `death` happens to have events, pick another event-less animation from the printed data.)

- [ ] **Step 2: Run to verify it fails** → FAIL `Nonexistent function 'get_animation_events'`.

- [ ] **Step 3: Declare in `SpineSkeletonDataResource.h`** (public)

```cpp
	// Returns the animation's spine EVENT keys as [{ "time": float, "name": String }, ...] (empty if none).
	Array get_animation_events(const String &animation_name);
```

- [ ] **Step 4: Implement in `SpineSkeletonDataResource.cpp`**

Add includes near the top (with the other `spine/…` includes):
```cpp
#include <spine/EventTimeline.h>
#include <spine/Event.h>
#include <spine/EventData.h>
```
Method:
```cpp
Array SpineSkeletonDataResource::get_animation_events(const String &animation_name) {
	Array result;
	if (!is_skeleton_data_loaded() || !get_skeleton_data()) return result;
	spine::Animation *anim = get_skeleton_data()->findAnimation(SPINE_STRING_TMP(animation_name));
	if (!anim) return result;
	spine::Vector<spine::Timeline *> &timelines = anim->getTimelines();
	for (int i = 0; i < (int) timelines.size(); i++) {
		spine::Timeline *tl = timelines[i];
		if (!tl || !tl->getRTTI().isExactly(spine::EventTimeline::rtti)) continue;
		spine::EventTimeline *et = (spine::EventTimeline *) tl;
		spine::Vector<spine::Event *> &events = et->getEvents();
		for (int j = 0; j < (int) events.size(); j++) {
			spine::Event *e = events[j];
			if (!e) continue;
			Dictionary d;
			d["time"] = e->getTime();
			d["name"] = String(e->getData().getName().buffer());
			result.append(d);
		}
	}
	return result;
}
```
(`String(x.getName().buffer())` — match the exact spine-String→String idiom used elsewhere in this file if it differs, e.g. a `SPINE_STRING` helper.)

- [ ] **Step 5: Bind in `_bind_methods()`**

```cpp
	ClassDB::bind_method(D_METHOD("get_animation_events", "animation_name"), &SpineSkeletonDataResource::get_animation_events);
```

- [ ] **Step 6: Build** → no errors.
- [ ] **Step 7: Run the test** → `TEST-OK`.
- [ ] **Step 8: Remove test files + commit**

```bash
rm spine-godot/example-v4-extension/_t_animevents.gd spine-godot/example-v4-extension/_t_animevents.gd.uid spine-godot/example-v4-extension/_t_animevents.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/spine_godot/SpineSkeletonDataResource.h spine-godot/spine_godot/SpineSkeletonDataResource.cpp
git commit -m "feat(spine-godot): SpineSkeletonDataResource::get_animation_events()

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Addon scaffold — plugin lifecycle, selection binding, dock shell

**Files:**
- Create: `plugin.cfg`, `plugin.gd`, `timeline_dock.gd`, `timeline_view.gd` (stub) under `spine-godot/example-v4-extension/addons/spine_notify_editor/`
- Modify: `spine-godot/example-v4-extension/project.godot` (enable the plugin)
- Test: `spine-godot/example-v4-extension/_t_dockbind.gd` + `.tscn` (headless logic test — instantiate + bind, no editor GUI)

**Interfaces:**
- Produces: `timeline_dock.gd` with `func bind(player) -> void` (player: `SpineAnimationPlayer` or null), `var sprite`, `var track`, `var animation_names: PackedStringArray`, `func _current_animation() -> String`. `timeline_view.gd` stub with `func setup(dock) -> void`.

- [ ] **Step 1: Write the failing test** — `_t_dockbind.gd` (instantiates the dock directly, no EditorPlugin)

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new()
	add_child(dock)
	dock.setup(null, null)                   # builds the UI (undo_redo/editor_interface null in headless)
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player)
	var bound_ok := dock.sprite == sprite and dock.animation_names.size() > 0 and ("walk" in dock.animation_names)
	dock.bind(null)                          # unbind must not error
	var unbind_ok := dock.sprite == null
	print("TEST-OK" if (bound_ok and unbind_ok) else "TEST-FAIL bound=%s unbind=%s" % [bound_ok, unbind_ok])
	get_tree().quit()
```
`_t_dockbind.tscn`: `Node` root + script.

- [ ] **Step 2: Run to verify it fails** → FAIL (dock script/dir missing).

- [ ] **Step 3: `plugin.cfg`**

```
[plugin]
name="Spine Notify Editor"
description="Timeline dock to author SpineNotify markers on a SpineAnimationPlayer."
author="Spine Runtimes"
version="1.0"
script="plugin.gd"
```

- [ ] **Step 4: `plugin.gd`**

```gdscript
@tool
extends EditorPlugin

const DockScript := preload("res://addons/spine_notify_editor/timeline_dock.gd")
var _dock

func _enter_tree() -> void:
	_dock = DockScript.new()
	_dock.setup(get_undo_redo(), get_editor_interface())
	add_control_to_bottom_panel(_dock, "Spine Notifies")
	get_editor_interface().get_selection().selection_changed.connect(_on_selection_changed)
	_on_selection_changed()

func _exit_tree() -> void:
	if get_editor_interface().get_selection().selection_changed.is_connected(_on_selection_changed):
		get_editor_interface().get_selection().selection_changed.disconnect(_on_selection_changed)
	if _dock:
		remove_control_from_bottom_panel(_dock)
		_dock.queue_free()
		_dock = null

func _on_selection_changed() -> void:
	if _dock == null: return
	var player = null
	for n in get_editor_interface().get_selection().get_selected_nodes():
		if n is SpineAnimationPlayer:
			player = n; break
	_dock.bind(player)
```

- [ ] **Step 5: `timeline_dock.gd`** (shell: toolbar + owns the view + binding; no timeline drawing yet)

```gdscript
@tool
extends VBoxContainer

var undo_redo                       # EditorUndoRedoManager (set via setup)
var editor_interface                # EditorInterface (set via setup)

var player                          # SpineAnimationPlayer
var sprite                          # SpineSprite3D (player's parent)
var track                           # SpineNotifyTrack
var animation_names: PackedStringArray = PackedStringArray()

var _placeholder: Label
var _toolbar: HBoxContainer
var _anim_dropdown: OptionButton
var _view                           # timeline_view

func setup(p_undo_redo, p_editor_interface) -> void:
	undo_redo = p_undo_redo
	editor_interface = p_editor_interface
	_build_ui()

func _build_ui() -> void:
	custom_minimum_size = Vector2(0, 220)
	_placeholder = Label.new()
	_placeholder.text = "Select a SpineAnimationPlayer to edit its notifies."
	add_child(_placeholder)

	_toolbar = HBoxContainer.new()
	_anim_dropdown = OptionButton.new()
	_anim_dropdown.item_selected.connect(func(_i): _on_animation_changed())
	_toolbar.add_child(_anim_dropdown)
	add_child(_toolbar)

	var ViewScript := load("res://addons/spine_notify_editor/timeline_view.gd")
	_view = ViewScript.new()
	_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_view.setup(self)
	add_child(_view)

	_set_active(false)

func _set_active(active: bool) -> void:
	if _placeholder: _placeholder.visible = not active
	if _toolbar: _toolbar.visible = active
	if _view: _view.visible = active

func bind(p_player) -> void:
	player = p_player if (p_player != null and is_instance_valid(p_player)) else null
	sprite = null
	track = null
	animation_names = PackedStringArray()
	if player != null:
		var parent = player.get_parent()
		if parent is SpineSprite3D:
			sprite = parent
		track = player.notify_track
		if sprite != null and sprite.skeleton_data_res != null:
			# get_animation_names() is a C++-only out-param helper (not bound); use the bound get_animations().
			animation_names = PackedStringArray()
			for a in sprite.skeleton_data_res.get_animations():
				if a != null: animation_names.append(a.get_name())
	_refresh_toolbar()
	_set_active(player != null)
	if _view: _view.refresh()

func _refresh_toolbar() -> void:
	if _anim_dropdown == null: return
	_anim_dropdown.clear()
	for n in animation_names:
		_anim_dropdown.add_item(n)

func _current_animation() -> String:
	if _anim_dropdown and _anim_dropdown.item_count > 0 and _anim_dropdown.selected >= 0:
		return _anim_dropdown.get_item_text(_anim_dropdown.selected)
	return ""

func _on_animation_changed() -> void:
	if _view: _view.refresh()

func _on_animation_changed_external() -> void:
	_on_animation_changed()
```

- [ ] **Step 6: `timeline_view.gd`** (stub — filled in Tasks 4–5)

```gdscript
@tool
extends Control

var dock

func setup(p_dock) -> void:
	dock = p_dock

func refresh() -> void:
	queue_redraw()
```

- [ ] **Step 7: Enable the plugin** in `spine-godot/example-v4-extension/project.godot` — add (or extend) the `[editor_plugins]` section:

```
[editor_plugins]

enabled=PackedStringArray("res://addons/spine_notify_editor/plugin.cfg")
```
(If an `[editor_plugins] enabled=...` already exists, append the path to its array instead of duplicating the section.)

- [ ] **Step 8: Run the logic test** — `res://_t_dockbind.tscn` → `TEST-OK` (verifies the dock instantiates, `bind()` populates `sprite`/`animation_names`, and unbind is clean — no GUI needed).

- [ ] **Step 9: Manual editor checklist (human) — record in the task report, do not block the commit on it:**
  - Open `example-v4-extension` in the Godot editor; enable the plugin if prompted; a **"Spine Notifies"** bottom-panel tab appears.
  - With nothing selected → placeholder text. Select a `SpineAnimationPlayer` under a `SpineSprite3D` → the animation dropdown lists the skeleton's animations.

- [ ] **Step 10: Commit**

```bash
rm spine-godot/example-v4-extension/_t_dockbind.gd spine-godot/example-v4-extension/_t_dockbind.gd.uid spine-godot/example-v4-extension/_t_dockbind.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/ spine-godot/example-v4-extension/project.godot
git commit -m "feat(spine-notify-editor): addon scaffold + dock binding

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Timeline view — ruler, playhead, scrub, play/pause/loop, frame snapping

**Files:**
- Modify: `timeline_view.gd` (drawing + input + logic), `timeline_dock.gd` (play/pause/loop toolbar + `_process` timer)
- Test: `spine-godot/example-v4-extension/_t_viewlogic.gd` + `.tscn` (headless logic: mapping + snap + scrub)

**Interfaces:**
- Produces on `timeline_view.gd`: `func duration() -> float`, `func time_from_x(x: float) -> float`, `func x_from_time(t: float) -> float`, `func snap(t: float) -> float`, `func set_playhead(t: float, do_pose := true) -> void`, `var playhead_time: float`, `var snap_enabled: bool`, `var fps: float`. On `timeline_dock.gd`: `var playing: bool`, `var loop_enabled: bool`, play/pause via `_process`.

- [ ] **Step 1: Write the failing test** — `_t_viewlogic.gd`

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new(); add_child(dock)
	dock.setup(null, null)
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player)
	dock.select_animation("walk")                      # dock helper: set dropdown to walk + refresh
	var view = dock._view
	view.size = Vector2(1000, 200)
	var dur := view.duration()
	# x<->time round-trips over the view width
	var t := dur * 0.5
	var rt: float = view.time_from_x(view.x_from_time(t))
	var maps := abs(rt - t) < 0.01
	# frame snap: fps grid
	view.snap_enabled = true
	view.fps = 30.0
	var snapped: float = view.snap(0.111)              # nearest 1/30 = 0.1 (frame 3) or 0.1333 (frame 4)
	var on_grid := abs(snapped * 30.0 - round(snapped * 30.0)) < 1e-3
	# scrub poses the sprite (bone moves vs t=0)
	view.set_playhead(0.0)
	var a: Vector3 = sprite.get_global_bone_transform_3d("front-fist").origin
	view.set_playhead(0.5)
	var b: Vector3 = sprite.get_global_bone_transform_3d("front-fist").origin
	var scrubs := a.distance_to(b) > 0.001
	var ok := maps and on_grid and scrubs and dur > 0.0
	print("TEST-OK" if ok else "TEST-FAIL maps=%s grid=%s scrub=%s dur=%f" % [maps, on_grid, scrubs, dur])
	get_tree().quit()
```
`_t_viewlogic.tscn`: `Node` + script.

- [ ] **Step 2: Run to verify it fails** → FAIL (`select_animation`/`duration`/… not defined).

- [ ] **Step 3: Add a `select_animation` helper to `timeline_dock.gd`** (drives the dropdown from code, for tests + external use):

```gdscript
func select_animation(anim: String) -> void:
	for i in _anim_dropdown.item_count:
		if _anim_dropdown.get_item_text(i) == anim:
			_anim_dropdown.select(i)
			break
	_on_animation_changed()
```

- [ ] **Step 4: Implement `timeline_view.gd`** (replace the stub body; keep `setup`/`refresh`)

```gdscript
@tool
extends Control

const RULER_H := 22.0
const EVENTS_H := 16.0
const PAD := 6.0

var dock
var playhead_time: float = 0.0
var snap_enabled: bool = true
var fps: float = 30.0
var _events: Array = []              # [{time,name}] read-only spine events (filled in Task 5)

func setup(p_dock) -> void:
	dock = p_dock

func refresh() -> void:
	fps = _skeleton_fps()
	_events = _load_events()
	queue_redraw()

func _skeleton_fps() -> float:
	if dock and dock.sprite and dock.sprite.skeleton_data_res:
		var f: float = dock.sprite.skeleton_data_res.get_fps()
		if f > 0.0: return f
	return 30.0

func _load_events() -> Array:
	return []                        # Task 5

func duration() -> float:
	if dock and dock.sprite and dock.sprite.skeleton_data_res:
		var anim = dock.sprite.skeleton_data_res.find_animation(dock._current_animation())
		if anim != null: return max(anim.get_duration(), 0.0001)
	return 1.0

func _track_left() -> float:
	return PAD

func _track_width() -> float:
	return max(size.x - PAD * 2.0, 1.0)

func time_from_x(x: float) -> float:
	var t := (x - _track_left()) / _track_width() * duration()
	return clamp(t, 0.0, duration())

func x_from_time(t: float) -> float:
	return _track_left() + (t / duration()) * _track_width()

func snap(t: float) -> float:
	if not snap_enabled or fps <= 0.0: return t
	return round(t * fps) / fps

func set_playhead(t: float, do_pose := true) -> void:
	playhead_time = clamp(t, 0.0, duration())
	if do_pose and dock and dock.sprite:
		dock.sprite.pose_at(dock._current_animation(), playhead_time)
	queue_redraw()

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		var t := time_from_x(event.position.x)
		if not event.ctrl_pressed: t = snap(t)
		set_playhead(t)
		accept_event()
	elif event is InputEventMouseMotion and (event.button_mask & MOUSE_BUTTON_MASK_LEFT):
		var t := time_from_x(event.position.x)
		if not event.ctrl_pressed: t = snap(t)
		set_playhead(t)
		accept_event()

func _draw() -> void:
	var dur := duration()
	var w := _track_width()
	var left := _track_left()
	# ruler baseline
	draw_line(Vector2(left, RULER_H), Vector2(left + w, RULER_H), Color(0.5,0.5,0.5), 1.0)
	# frame ticks when snapping
	if snap_enabled and fps > 0.0:
		var frames := int(dur * fps)
		for f in range(frames + 1):
			var x := x_from_time(f / fps)
			draw_line(Vector2(x, RULER_H - 4), Vector2(x, RULER_H), Color(0.4,0.4,0.4), 1.0)
	# quarter labels
	for q in range(5):
		var x := left + w * q / 4.0
		draw_string(get_theme_default_font(), Vector2(x + 2, 12), "%.2f" % (dur * q / 4.0), HORIZONTAL_ALIGNMENT_LEFT, -1, 10)
	# playhead
	var px := x_from_time(playhead_time)
	draw_line(Vector2(px, 0), Vector2(px, size.y), Color(1,0.8,0.2), 1.0)
```

- [ ] **Step 5: Add play/pause/loop to `timeline_dock.gd`** — in `_build_ui()` extend the toolbar, and add `_process`:

```gdscript
# add as members:
var playing: bool = false
var loop_enabled: bool = true
var _play_btn: Button
var _loop_btn: CheckButton
var _snap_btn: CheckButton
var _time_label: Label

# in _build_ui(), after adding _anim_dropdown to _toolbar:
	_play_btn = Button.new(); _play_btn.text = "Play"
	_play_btn.pressed.connect(func(): playing = not playing; _play_btn.text = ("Pause" if playing else "Play"))
	_toolbar.add_child(_play_btn)
	_loop_btn = CheckButton.new(); _loop_btn.text = "Loop"; _loop_btn.button_pressed = true
	_loop_btn.toggled.connect(func(on): loop_enabled = on)
	_toolbar.add_child(_loop_btn)
	_snap_btn = CheckButton.new(); _snap_btn.text = "Snap"; _snap_btn.button_pressed = true
	_snap_btn.toggled.connect(func(on): if _view: _view.snap_enabled = on; _view.queue_redraw())
	_toolbar.add_child(_snap_btn)
	_time_label = Label.new()
	_toolbar.add_child(_time_label)
	set_process(true)

# add:
func _process(delta: float) -> void:
	if _view == null: return
	if playing and _view.visible:
		var dur: float = _view.duration()
		var t: float = _view.playhead_time + delta
		if t >= dur:
			t = 0.0 if loop_enabled else dur
			if not loop_enabled: playing = false; _play_btn.text = "Play"
		_view.set_playhead(t)
	if _time_label and _view:
		_time_label.text = "  %.2f / %.2f" % [_view.playhead_time, _view.duration()]
```

- [ ] **Step 6: Build not needed** (GDScript only). **Run the logic test** — `res://_t_viewlogic.tscn` → `TEST-OK`.

- [ ] **Step 7: Manual editor checklist (human, in the report):** select a player, drag the playhead → the spineboy scrubs live in the viewport; Play animates, Loop wraps, Pause stops; Snap on → playhead lands on frame ticks; Ctrl-drag → fine (unsnapped).

- [ ] **Step 8: Commit**

```bash
rm spine-godot/example-v4-extension/_t_viewlogic.gd spine-godot/example-v4-extension/_t_viewlogic.gd.uid spine-godot/example-v4-extension/_t_viewlogic.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): timeline scrub/play/loop + frame snapping

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Notify authoring — markers, events lane, marker strip, undo/redo

**Files:**
- Modify: `timeline_view.gd` (notify lane + events lane draw/hit-test + CRUD), `timeline_dock.gd` (marker strip + Create-Track button)
- Test: `spine-godot/example-v4-extension/_t_authoring.gd` + `.tscn` (headless CRUD logic)

**Interfaces:**
- Produces on `timeline_view.gd`: `func add_notify_at(t: float) -> void`, `func move_notify(n, t: float) -> void`, `func delete_notify(n) -> void`, `func notify_at_x(x: float)` (returns a `SpineNotify` or null), `func notifies_for_current() -> Array`. On `timeline_dock.gd`: `func ensure_track() -> void`, marker strip `func edit_notify(n) -> void`.

- [ ] **Step 1: Write the failing test** — `_t_authoring.gd`

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new(); add_child(dock)
	dock.setup(null, null)
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player)
	dock.select_animation("walk")
	dock.ensure_track()                        # creates + assigns a SpineNotifyTrack if none
	var view = dock._view
	view.size = Vector2(1000, 200)
	var n0: int = dock.track.notifies.size()
	view.add_notify_at(0.30)
	var added := dock.track.notifies.size() == n0 + 1
	var nt = dock.track.notifies[dock.track.notifies.size() - 1]
	var tagged := nt.animation_name == "walk" and abs(nt.time - 0.30) < 1e-3
	view.move_notify(nt, 0.60)
	var moved := abs(nt.time - 0.60) < 1e-3
	# events lane reads get_animation_events
	view.refresh()
	var ev := view._events
	var has_events := ev.size() > 0
	view.delete_notify(nt)
	var deleted := dock.track.notifies.size() == n0
	var ok := added and tagged and moved and has_events and deleted
	print("TEST-OK" if ok else "TEST-FAIL add=%s tag=%s move=%s ev=%s del=%s" % [added, tagged, moved, has_events, deleted])
	get_tree().quit()
```
`_t_authoring.tscn`: `Node` + script.

- [ ] **Step 2: Run to verify it fails** → FAIL (`ensure_track`/`add_notify_at`/… not defined).

- [ ] **Step 3: `ensure_track()` + marker strip in `timeline_dock.gd`**

```gdscript
# member:
var _strip: HBoxContainer
var _name_edit: LineEdit
var _channel_edit: LineEdit
var _selected_notify

func ensure_track() -> void:
	if player == null: return
	if track == null:
		var t = SpineNotifyTrack.new()
		if undo_redo:
			undo_redo.create_action("Create Notify Track")
			undo_redo.add_do_property(player, "notify_track", t)
			undo_redo.add_undo_property(player, "notify_track", null)
			undo_redo.commit_action()
		else:
			player.notify_track = t
		track = player.notify_track

# in _build_ui(), after adding _view:
	_strip = HBoxContainer.new()
	_name_edit = LineEdit.new(); _name_edit.placeholder_text = "name"
	_name_edit.text_submitted.connect(func(_s): _apply_strip())
	_channel_edit = LineEdit.new(); _channel_edit.placeholder_text = "channel"
	_channel_edit.text_submitted.connect(func(_s): _apply_strip())
	var insp := Button.new(); insp.text = "Edit in Inspector"
	insp.pressed.connect(func(): if _selected_notify and editor_interface: editor_interface.edit_resource(_selected_notify))
	_strip.add_child(Label.new()); _strip.get_child(0).text = "notify:"
	_strip.add_child(_name_edit); _strip.add_child(_channel_edit); _strip.add_child(insp)
	add_child(_strip)
	_strip.visible = false

func edit_notify(n) -> void:
	_selected_notify = n
	_strip.visible = n != null
	if n != null:
		_name_edit.text = n.notify_name
		_channel_edit.text = n.channel

func _apply_strip() -> void:
	if _selected_notify == null: return
	_selected_notify.notify_name = _name_edit.text
	_selected_notify.channel = _channel_edit.text
	if track: track.emit_changed()
	if _view: _view.queue_redraw()
```

- [ ] **Step 4: Notify + events lanes in `timeline_view.gd`** — implement `_load_events`, the CRUD, hit-test, and extend `_draw`/`_gui_input`:

```gdscript
const NOTIFY_Y := RULER_H + EVENTS_H + 14.0
const EVENTS_Y := RULER_H + 10.0
const HIT_PX := 8.0
var _drag_notify = null
var _drag_start_time := 0.0

func _load_events() -> Array:
	if dock and dock.sprite and dock.sprite.skeleton_data_res:
		return dock.sprite.skeleton_data_res.get_animation_events(dock._current_animation())
	return []

func notifies_for_current() -> Array:
	var out := []
	if dock and dock.track:
		var anim := dock._current_animation()
		for n in dock.track.notifies:
			if n != null and n.animation_name == anim:
				out.append(n)
	return out

func notify_at_x(x: float):
	for n in notifies_for_current():
		if abs(x_from_time(n.time) - x) <= HIT_PX:
			return n
	return null

func add_notify_at(t: float) -> void:
	dock.ensure_track()
	if dock.track == null: return
	var n = SpineNotify.new()
	n.animation_name = dock._current_animation()
	n.time = clamp(t, 0.0, duration())
	n.notify_name = "notify"
	var arr: Array = dock.track.notifies.duplicate()
	arr.append(n)
	_set_notifies(arr, "Add Notify")
	dock.edit_notify(n)
	queue_redraw()

func delete_notify(n) -> void:
	if dock.track == null or n == null: return
	var arr: Array = dock.track.notifies.duplicate()
	arr.erase(n)
	_set_notifies(arr, "Delete Notify")
	if dock._selected_notify == n: dock.edit_notify(null)
	queue_redraw()

func move_notify(n, t: float) -> void:               # live drag: direct set (undo committed on release)
	if n == null: return
	n.time = clamp(t, 0.0, duration())
	if dock.track: dock.track.emit_changed()
	queue_redraw()

func _commit_move(n, old_t: float, new_t: float) -> void:
	if dock.undo_redo:
		dock.undo_redo.create_action("Move Notify")
		dock.undo_redo.add_do_property(n, "time", new_t)   # already new_t from live drag; makes it undoable
		dock.undo_redo.add_undo_property(n, "time", old_t)
		dock.undo_redo.commit_action()

func _set_notifies(arr: Array, action: String) -> void:
	if dock.undo_redo:
		dock.undo_redo.create_action(action)
		dock.undo_redo.add_do_property(dock.track, "notifies", arr)
		dock.undo_redo.add_undo_property(dock.track, "notifies", dock.track.notifies.duplicate())
		dock.undo_redo.commit_action()
	else:
		dock.track.notifies = arr
```
Extend `_gui_input` (replace its body):
```gdscript
func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			if event.position.y >= NOTIFY_Y - HIT_PX:                 # notify lane
				var hit = notify_at_x(event.position.x)
				if hit != null:
					_drag_notify = hit; _drag_start_time = hit.time; dock.edit_notify(hit)
				else:
					var t := time_from_x(event.position.x)
					if not event.ctrl_pressed: t = snap(t)
					add_notify_at(t)
				accept_event(); return
			var t2 := time_from_x(event.position.x)                   # else scrub
			if not event.ctrl_pressed: t2 = snap(t2)
			set_playhead(t2); accept_event()
		else:
			if _drag_notify != null and _drag_notify.time != _drag_start_time:
				_commit_move(_drag_notify, _drag_start_time, _drag_notify.time)
			_drag_notify = null
	elif event is InputEventMouseMotion and (event.button_mask & MOUSE_BUTTON_MASK_LEFT):
		var t := time_from_x(event.position.x)
		if not event.ctrl_pressed: t = snap(t)
		if _drag_notify != null:
			move_notify(_drag_notify, t); set_playhead(t)
		else:
			set_playhead(t)
		accept_event()
	elif event is InputEventKey and event.pressed and event.keycode == KEY_DELETE:
		if dock._selected_notify != null: delete_notify(dock._selected_notify); accept_event()
```
Extend `_draw` (append before the playhead draw):
```gdscript
	# events lane (read-only)
	for e in _events:
		var ex := x_from_time(e.get("time", 0.0))
		draw_line(Vector2(ex, EVENTS_Y - 5), Vector2(ex, EVENTS_Y + 5), Color(0.5,0.7,1.0), 2.0)
	# notify lane (diamonds)
	for n in notifies_for_current():
		var nx := x_from_time(n.time)
		var sel := (dock._selected_notify == n)
		var col := Color(1.0,0.85,0.2) if sel else Color(0.9,0.5,0.2)
		var pts := PackedVector2Array([Vector2(nx,NOTIFY_Y-6),Vector2(nx+6,NOTIFY_Y),Vector2(nx,NOTIFY_Y+6),Vector2(nx-6,NOTIFY_Y)])
		draw_colored_polygon(pts, col)
```
Also make the view focusable so `KEY_DELETE` reaches it: in `setup`, `focus_mode = Control.FOCUS_CLICK`.

- [ ] **Step 5: Run the logic test** — `res://_t_authoring.tscn` → `TEST-OK`.

- [ ] **Step 6: Manual editor checklist (human, in the report):** select a player; click the notify lane → a diamond appears (and it's selected, name/channel in the strip); drag it → moves + preview follows; rename/re-channel in the strip persists; **Edit in Inspector** opens the `SpineNotify`; Del removes; **Ctrl+Z/Ctrl+Y** undo/redo add/move/delete; **Ctrl+S** saves the track; the read-only events lane shows the `footstep` ticks; play a `SpineAnimationPlayer` at runtime and confirm the authored notifies fire (`notified`).

- [ ] **Step 7: Commit**

```bash
rm spine-godot/example-v4-extension/_t_authoring.gd spine-godot/example-v4-extension/_t_authoring.gd.uid spine-godot/example-v4-extension/_t_authoring.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): notify authoring, events lane, marker strip, undo/redo

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for the executor

- GDScript tasks: after each, the headless logic test must pass; the **manual editor checklist is for the human** — record it in the report as "pending human validation", do not block the commit on it, and do not claim the UI works from a headless run.
- `SpineNotifyTrack.notifies` is a typed `Array` of `SpineNotify`; assigning a duplicated array via `add_do_property` is how add/delete become one undoable step. `emit_changed()` after in-place field edits (time/name/channel) marks the resource dirty.
- If `get_editor_interface().get_undo_redo()` / `edit_resource` names differ in Godot 4.7, use the 4.7 `EditorPlugin.get_undo_redo()` (returns `EditorUndoRedoManager`) and `EditorInterface.edit_resource()` — verify against the running 4.7 API and adjust.
- Keep `_draw`/`_gui_input` thin: they call the tested logic methods (`time_from_x`, `snap`, `add_notify_at`, …). Visual polish (colors, sizes, lane layout) is refined during human validation.
