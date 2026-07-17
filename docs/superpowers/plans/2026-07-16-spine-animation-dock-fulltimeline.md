# SpineAnimation dock — full-timeline follow-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add channel lanes, frame-magnet snapping, and an inline typed payload editor to the SpineAnimation timeline dock.

**Architecture:** Pure GDScript changes to the `spine_notify_editor` addon's two files (`timeline_view.gd`, `timeline_dock.gd`). No C++, no DLL rebuild. Builds on the shipped MVP; design in `docs/superpowers/specs/2026-07-16-spine-animation-dock-fulltimeline-design.md`.

**Tech Stack:** Godot 4.7 GDScript (`@tool`), the already-built spine-godot GDExtension (`SpineNotify` has `channel: String` + `payload: Dictionary`).

## Global Constraints

- **GDScript-only**, in `spine-godot/example-v4-extension/addons/spine_notify_editor/`. No C++, no DLL rebuild, no `plugin.gd`/`plugin.cfg` change.
- **`@tool`**; every notify mutation goes through `EditorUndoRedoManager` with a **null-`undo_redo` direct-assignment fallback** (headless). Adding a session channel lane is NOT a resource mutation (no undo entry).
- **Frame-exact:** all snapped times resolve to `round(t*fps)/fps`; the magnet resolves to its target's frame.
- Do **not** break the existing MVP behaviors (scrub/play/loop/snap, add/move/delete, events lane, selection strip, undo, `_prune_selection`, playhead reset on bind/animation-change).
- Each task: TDD headless logic test → RED → implement → GREEN (`TEST-OK`, no parse/script errors). Run Godot via Bash `timeout 90 godot --headless --path "…/example-v4-extension" "res://<test>.tscn"`. The **manual editor checklist is pending-human** — record it, do not block the commit, do not claim interactive UI works from a headless run.
- Before each commit: delete the disposable `_t_*.gd/.gd.uid/.tscn`; `git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'`. Commit ONLY the addon dir. Commit trailer: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Do **not** push.
- GDScript 4.7 `:=` inference quirk with `abs()`/untyped-`dock.*`/Variant indexing: add explicit `var x: Type =` annotations (in production where needed, as the existing view already does, and in tests). Never leave a real type error.

---

### Task 1: Channel lanes (auto-derived + "+ Channel")

Turn the single notify lane into N stacked, labelled channel lanes. The timeline track shifts right by `LANE_LABEL_W` to make a left gutter for channel names.

**Files:**
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_view.gd`
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_dock.gd`
- Test: `spine-godot/example-v4-extension/_t_lanes.gd` + `.tscn`

**Interfaces:**
- Produces on `timeline_view.gd`: `channels() -> Array`, `add_notify_at(t: float, channel: String) -> void` (signature change: adds `channel`), `notify_at_pos(x: float, y: float)` (replaces `notify_at_x`), lane helpers `_lane_index`/`_lane_center`/`_channel_at_y`. `_track_left()` now returns `PAD + LANE_LABEL_W`.
- Produces on `timeline_dock.gd`: `_extra_channels: Array`, `_add_channel()`, toolbar `_new_channel_edit`/`_add_channel_btn`.

- [ ] **Step 1: Write the failing test** — `_t_lanes.gd`

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
	dock.ensure_track()
	var view = dock._view
	view.size = Vector2(1000, 260)
	# add notifies to two channels
	view.add_notify_at(0.20, "vfx")
	view.add_notify_at(0.60, "sfx")
	var n_vfx = null
	for n in dock.track.notifies:
		if n.channel == "vfx": n_vfx = n
	var tagged: bool = n_vfx != null and abs(n_vfx.time - 0.20) < 1e-3
	# channels(): default first, rest sorted
	var chans: Array = view.channels()
	var chans_ok: bool = chans.size() == 3 and chans[0] == "default" and chans[1] == "sfx" and chans[2] == "vfx"
	# "+ Channel" adds an empty session lane
	dock._new_channel_edit.text = "gameplay"
	dock._add_channel()
	var added_lane: bool = "gameplay" in view.channels()
	# 2-D hit test: the vfx diamond is only hittable inside the vfx lane's y
	var vx: float = view.x_from_time(0.20)
	var vy: float = view._lane_center(view._lane_index("vfx"))
	var hit_ok: bool = view.notify_at_pos(vx, vy) == n_vfx
	var miss_ok: bool = view.notify_at_pos(vx, view._lane_center(view._lane_index("sfx"))) == null
	# _channel_at_y maps a lane's center back to its channel
	var caty_ok: bool = view._channel_at_y(vy) == "vfx"
	var ok: bool = tagged and chans_ok and added_lane and hit_ok and miss_ok and caty_ok
	print("TEST-OK" if ok else "TEST-FAIL tag=%s chans=%s addlane=%s hit=%s miss=%s caty=%s" % [tagged, chans_ok, added_lane, hit_ok, miss_ok, caty_ok])
	get_tree().quit()
```
`_t_lanes.tscn`: a `Node` root with the script attached.

- [ ] **Step 2: Run to verify it fails**

Run: `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://_t_lanes.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|Parse Error|error"`
Expected: FAIL — `channels`/`notify_at_pos`/`_lane_center`/`add_notify_at(t, channel)` not defined.

- [ ] **Step 3: `timeline_view.gd` — constants + lane geometry**

Replace the constants block (currently lines 4-9) with:
```gdscript
const RULER_H := 22.0
const EVENTS_H := 16.0
const PAD := 6.0
const EVENTS_Y := RULER_H + 10.0
const HIT_PX := 8.0
const LANE_H := 20.0
const LANE_LABEL_W := 72.0
const LANES_TOP := EVENTS_Y + 12.0     # 44.0
```
Replace `_track_left()`/`_track_width()` (currently lines 103-107) with (track shifts right past the label gutter):
```gdscript
func _track_left() -> float:
	return PAD + LANE_LABEL_W

func _track_width() -> float:
	return max(size.x - _track_left() - PAD, 1.0)
```
Add lane helpers (place after `_track_width`):
```gdscript
func channels() -> Array:
	var seen := {}
	if dock and dock.track:
		var anim: String = dock._current_animation()
		for n in dock.track.notifies:
			if n != null and n.animation_name == anim:
				var ch: String = n.channel if n.channel != "" else "default"
				seen[ch] = true
	if dock:
		for ch in dock._extra_channels:
			if ch != "": seen[ch] = true
	seen["default"] = true
	var arr: Array = seen.keys()
	arr.sort()
	arr.erase("default")
	arr.push_front("default")
	return arr

func _lane_index(ch: String) -> int:
	var c: String = ch if ch != "" else "default"
	var idx: int = channels().find(c)
	return idx if idx >= 0 else 0

func _lane_top(index: int) -> float:
	return LANES_TOP + index * LANE_H

func _lane_center(index: int) -> float:
	return _lane_top(index) + LANE_H * 0.5

func _channel_at_y(y: float) -> String:
	if y < LANES_TOP:
		return ""
	var chans: Array = channels()
	var idx: int = int((y - LANES_TOP) / LANE_H)
	if idx < 0 or idx >= chans.size():
		return ""
	return chans[idx]
```

- [ ] **Step 4: `timeline_view.gd` — grow min height in `refresh()`, add-to-channel, 2-D hit test**

Replace `refresh()` (currently lines 23-26) with:
```gdscript
func refresh() -> void:
	fps = _skeleton_fps()
	_events = _load_events()
	custom_minimum_size.y = LANES_TOP + channels().size() * LANE_H + PAD
	queue_redraw()
```
Replace `notify_at_x` (currently lines 48-52) with a 2-D hit test:
```gdscript
func notify_at_pos(x: float, y: float):
	var ch: String = _channel_at_y(y)
	if ch == "":
		return null
	for n in notifies_for_current():
		var nch: String = n.channel if n.channel != "" else "default"
		if nch == ch and abs(x_from_time(n.time) - x) <= HIT_PX:
			return n
	return null
```
Replace `add_notify_at` (currently lines 54-65) with the channel-aware version:
```gdscript
func add_notify_at(t: float, channel: String) -> void:
	dock.ensure_track()
	if dock.track == null: return
	var n = SpineNotify.new()
	n.animation_name = dock._current_animation()
	n.time = clamp(t, 0.0, duration())
	n.notify_name = "notify"
	n.channel = channel if channel != "" else "default"
	var arr: Array = dock.track.notifies.duplicate()
	arr.append(n)
	_set_notifies(arr, "Add Notify")
	dock.edit_notify(n)
	queue_redraw()
```

- [ ] **Step 5: `timeline_view.gd` — `_gui_input` uses lanes, `_draw` renders lanes**

Replace `_gui_input` (currently lines 126-154) with:
```gdscript
func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			var ch: String = _channel_at_y(event.position.y)
			if ch != "":                                          # inside a lane
				var hit = notify_at_pos(event.position.x, event.position.y)
				if hit != null:
					_drag_notify = hit; _drag_start_time = hit.time; dock.edit_notify(hit)
				else:
					var t := time_from_x(event.position.x)
					if not event.ctrl_pressed: t = snap(t)
					add_notify_at(t, ch)
				accept_event(); return
			var t2 := time_from_x(event.position.x)               # above lanes -> scrub
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
Replace the events-lane + notify-lane portion of `_draw` (currently lines 172-182, i.e. the `# events lane` and `# notify lane` blocks) with lane rendering — keep the ruler/ticks/quarter-labels block above it and the playhead block below it unchanged:
```gdscript
	# events lane (read-only)
	for e in _events:
		var ex := x_from_time(e.get("time", 0.0))
		draw_line(Vector2(ex, EVENTS_Y - 5), Vector2(ex, EVENTS_Y + 5), Color(0.5,0.7,1.0), 2.0)
	# channel lanes: separators + labels
	var font := get_theme_default_font()
	var chans := channels()
	for i in chans.size():
		var ltop := _lane_top(i)
		draw_line(Vector2(left, ltop), Vector2(left + w, ltop), Color(0.3,0.3,0.3), 1.0)
		draw_string(font, Vector2(2, _lane_center(i) + 4), chans[i], HORIZONTAL_ALIGNMENT_LEFT, LANE_LABEL_W - 4, 10, Color(0.7,0.7,0.75))
	# notify diamonds, per lane
	for n in notifies_for_current():
		var nx := x_from_time(n.time)
		var ny := _lane_center(_lane_index(n.channel))
		var sel: bool = (dock._selected_notify == n)
		var col := Color(1.0,0.85,0.2) if sel else Color(0.9,0.5,0.2)
		var pts := PackedVector2Array([Vector2(nx,ny-6),Vector2(nx+6,ny),Vector2(nx,ny+6),Vector2(nx-6,ny)])
		draw_colored_polygon(pts, col)
```

- [ ] **Step 6: `timeline_dock.gd` — `_extra_channels`, "+ Channel" toolbar control, reset on player switch**

Add members near line 26 (after `_time_label`):
```gdscript
var _extra_channels: Array = []
var _new_channel_edit: LineEdit
var _add_channel_btn: Button
```
In `_build_ui()`, after the `_time_label` is added to `_toolbar` (currently line 55) and before `set_process(true)`, add:
```gdscript
	_new_channel_edit = LineEdit.new()
	_new_channel_edit.placeholder_text = "new channel…"
	_new_channel_edit.custom_minimum_size.x = 100
	_new_channel_edit.text_submitted.connect(func(_s): _add_channel())
	_toolbar.add_child(_new_channel_edit)
	_add_channel_btn = Button.new(); _add_channel_btn.text = "+ Channel"
	_add_channel_btn.pressed.connect(_add_channel)
	_toolbar.add_child(_add_channel_btn)
```
Add the handler (place after `ensure_track()`):
```gdscript
func _add_channel() -> void:
	var cname: String = _new_channel_edit.text.strip_edges()
	if cname == "": return
	if not (cname in _extra_channels): _extra_channels.append(cname)
	_new_channel_edit.text = ""
	if _view:
		_view.refresh()
		_view.queue_redraw()
```
In `bind()` (currently lines 144-164), clear `_extra_channels` only when the player actually changes. Replace the first two lines of `bind()`:
```gdscript
func bind(p_player) -> void:
	player = p_player if (p_player != null and is_instance_valid(p_player)) else null
```
with:
```gdscript
func bind(p_player) -> void:
	var new_player = p_player if (p_player != null and is_instance_valid(p_player)) else null
	if new_player != player:
		_extra_channels = []
	player = new_player
```

- [ ] **Step 7: Run the logic test**

Run: `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://_t_lanes.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|Parse Error|error"`
Expected: `TEST-OK`, no parse/script errors.

- [ ] **Step 8: Manual editor checklist (human, record in report — do not run):** lanes stack under the events lane with channel labels in the left gutter; clicking an empty spot in a lane adds a notify to that channel; a diamond is only grabbable within its lane; "+ Channel" adds an empty labelled lane you can click into; editing the strip's `channel` field moves the marker to the matching lane.

- [ ] **Step 9: Commit**

```bash
rm spine-godot/example-v4-extension/_t_lanes.gd spine-godot/example-v4-extension/_t_lanes.gd.uid spine-godot/example-v4-extension/_t_lanes.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): channel lanes (auto-derived + + Channel)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Frame-magnet snap (folded into the existing Snap toggle)

Give event/notify times a `MAGNET_PX` capture radius; the magnet resolves to the target's frame so everything stays frame-exact.

**Files:**
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_view.gd`
- Test: `spine-godot/example-v4-extension/_t_snap.gd` + `.tscn`

**Interfaces:**
- Consumes: `_events`, `notifies_for_current()`, `x_from_time`, `_drag_notify`, `fps` (all existing on the view after Task 1).
- Produces: revised `snap(t: float) -> float`; new `const MAGNET_PX := 10.0`.

- [ ] **Step 1: Write the failing test** — `_t_snap.gd`

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
	dock.select_animation("walk")           # walk has a spine event at 0.5
	dock.ensure_track()
	var view = dock._view
	# NARROW view so frames are dense (~3.9 px/frame) and the 10px magnet genuinely
	# overrides plain frame-snap — otherwise (wide view) magnet==frame and the test can't discriminate.
	view.size = Vector2(200, 260)
	view.refresh()                          # loads _events (footstep at 0.0, 0.5)
	view.snap_enabled = true
	view.add_notify_at(0.30, "vfx")         # a notify target at 0.30
	# magnet: ~3.5px from the 0.5 event -> 0.5, vs plain frame round(0.47*30)/30 = 14/30 ≈ 0.4667
	var mag_event: bool = abs(view.snap(0.47) - 0.5) < 1e-4
	# magnet: ~2.3px from the 0.30 notify -> 0.30, vs plain frame round(0.32*30)/30 = 10/30 ≈ 0.3333
	var mag_notify: bool = abs(view.snap(0.32) - 0.30) < 1e-4
	# far from every target -> plain frame round(0.80*30)/30 = 0.80
	var frame_ok: bool = abs(view.snap(0.80) - (round(0.80*30.0)/30.0)) < 1e-4
	# dragged notify excludes itself: drag the 0.30 notify, snap(0.32) no longer sticks to 0.30
	var n30 = null
	for n in dock.track.notifies:
		if abs(n.time - 0.30) < 1e-3: n30 = n
	view._drag_notify = n30
	var dragged: float = view.snap(0.32)
	var excl_ok: bool = abs(dragged - (round(0.32*30.0)/30.0)) < 1e-4 and abs(dragged - 0.30) > 1e-3
	view._drag_notify = null
	var ok: bool = mag_event and mag_notify and frame_ok and excl_ok
	print("TEST-OK" if ok else "TEST-FAIL magev=%s magnote=%s frame=%s excl=%s" % [mag_event, mag_notify, frame_ok, excl_ok])
	get_tree().quit()
```
`_t_snap.tscn`: a `Node` root with the script attached.

- [ ] **Step 2: Run to verify it fails**

Run: `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://_t_snap.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|Parse Error|error"`
Expected: `TEST-FAIL magev=false …` — the old `snap()` ignores the magnet, so `snap(0.47)` returns the plain frame `14/30 ≈ 0.4667`, not `0.5`. (The narrow view makes the magnet genuinely differ from frame-snap; this is a real RED.)

- [ ] **Step 3: `timeline_view.gd` — magnet `snap()`**

Add the constant to the constants block: `const MAGNET_PX := 10.0`. Replace `snap()` (currently lines 116-118) with:
```gdscript
func snap(t: float) -> float:
	if not snap_enabled or fps <= 0.0:
		return t
	var f: float = round(t * fps) / fps
	var tx: float = x_from_time(t)
	var best = null
	var best_px: float = MAGNET_PX
	for e in _events:
		var et: float = e.get("time", 0.0)
		var d: float = abs(x_from_time(et) - tx)
		if d <= best_px:
			best = et; best_px = d
	for n in notifies_for_current():
		if n == _drag_notify:
			continue
		var dn: float = abs(x_from_time(n.time) - tx)
		if dn <= best_px:
			best = n.time; best_px = dn
	if best != null:
		return round(float(best) * fps) / fps
	return f
```

- [ ] **Step 4: Run the logic test**

Run: `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://_t_snap.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|Parse Error|error"`
Expected: `TEST-OK`, no parse/script errors.

- [ ] **Step 5: Manual editor checklist (human, record — do not run):** with Snap on, dragging a marker or the playhead near a footstep tick or another notify sticks exactly onto it (from several frames away); away from any target it snaps to the frame grid; Ctrl frees it.

- [ ] **Step 6: Commit**

```bash
rm spine-godot/example-v4-extension/_t_snap.gd spine-godot/example-v4-extension/_t_snap.gd.uid spine-godot/example-v4-extension/_t_snap.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): frame-magnet snap to events + notifies

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Typed payload editor (inline in the marker strip)

Turn the one-row strip into a small panel with typed key/value payload rows, committed through undo/redo.

**Files:**
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_dock.gd`
- Test: `spine-godot/example-v4-extension/_t_payload.gd` + `.tscn`

**Interfaces:**
- Produces on `timeline_dock.gd`: `_strip` becomes a `VBoxContainer`; new `_payload_box`, `_payload_rows`, `_add_key_btn`, `_PAYLOAD_TYPES`, `_add_payload_row`, `_remove_payload_row`, `_on_add_key`, `_clear_payload_rows`, `_type_index_of`, `_parse_payload_value`, `_apply_payload`. `edit_notify` also (re)builds payload rows.

- [ ] **Step 1: Write the failing test** — `_t_payload.gd`

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new(); add_child(dock)
	dock.setup(null, null)                       # undo_redo null -> fallback
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player)
	dock.select_animation("walk")
	dock.ensure_track()
	var view = dock._view
	view.add_notify_at(0.30, "vfx")
	var n = dock.track.notifies[dock.track.notifies.size() - 1]
	dock.edit_notify(n)
	# three typed rows
	dock._on_add_key(); dock._payload_rows[0].key.text = "speed"; dock._payload_rows[0].type.select(2); dock._payload_rows[0].value.text = "2.5"
	dock._on_add_key(); dock._payload_rows[1].key.text = "loop";  dock._payload_rows[1].type.select(3); dock._payload_rows[1].value.text = "true"
	dock._on_add_key(); dock._payload_rows[2].key.text = "tag";   dock._payload_rows[2].type.select(0); dock._payload_rows[2].value.text = "hi"
	dock._apply_payload()
	var p: Dictionary = n.payload
	var typed_ok: bool = p.size() == 3 and typeof(p.get("speed")) == TYPE_FLOAT and abs(p["speed"] - 2.5) < 1e-4 \
		and typeof(p.get("loop")) == TYPE_BOOL and p["loop"] == true \
		and typeof(p.get("tag")) == TYPE_STRING and p["tag"] == "hi"
	# blank key dropped
	dock._on_add_key(); dock._payload_rows[3].key.text = ""; dock._payload_rows[3].value.text = "x"
	dock._apply_payload()
	var blank_ok: bool = n.payload.size() == 3
	# round-trip: reselect rebuilds rows with inferred types
	dock.edit_notify(n)
	var rt_ok: bool = dock._payload_rows.size() == 3
	var ok: bool = typed_ok and blank_ok and rt_ok
	print("TEST-OK" if ok else "TEST-FAIL typed=%s blank=%s rt=%s" % [typed_ok, blank_ok, rt_ok])
	get_tree().quit()
```
`_t_payload.tscn`: a `Node` root with the script attached.

- [ ] **Step 2: Run to verify it fails**

Run: `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://_t_payload.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|Parse Error|error"`
Expected: FAIL — `_on_add_key`/`_apply_payload`/`_payload_rows` not defined.

- [ ] **Step 3: `timeline_dock.gd` — members + strip becomes a VBox with a payload area**

Change the `_strip` declaration (currently line 16 `var _strip: HBoxContainer`) to:
```gdscript
var _strip: VBoxContainer
```
Add members after it (near line 19):
```gdscript
var _payload_box: VBoxContainer
var _payload_rows: Array = []          # each: {row, key, type, value}
var _add_key_btn: Button
const _PAYLOAD_TYPES := ["String", "int", "float", "bool"]
```
Replace the strip-building block in `_build_ui()` (currently lines 64-76, from `_strip = HBoxContainer.new()` through `_strip.visible = false`) with:
```gdscript
	_strip = VBoxContainer.new()
	var row1 := HBoxContainer.new()
	row1.add_child(Label.new()); row1.get_child(0).text = "notify:"
	_name_edit = LineEdit.new(); _name_edit.placeholder_text = "name"
	_name_edit.text_submitted.connect(func(_s): _apply_strip())
	_name_edit.focus_exited.connect(_apply_strip)
	_channel_edit = LineEdit.new(); _channel_edit.placeholder_text = "channel"
	_channel_edit.text_submitted.connect(func(_s): _apply_strip())
	_channel_edit.focus_exited.connect(_apply_strip)
	var insp := Button.new(); insp.text = "Edit in Inspector"
	insp.pressed.connect(func(): if _selected_notify and editor_interface: editor_interface.edit_resource(_selected_notify))
	row1.add_child(_name_edit); row1.add_child(_channel_edit); row1.add_child(insp)
	_strip.add_child(row1)
	_payload_box = VBoxContainer.new()
	_strip.add_child(_payload_box)
	_add_key_btn = Button.new(); _add_key_btn.text = "+ Add key"
	_add_key_btn.pressed.connect(_on_add_key)
	_strip.add_child(_add_key_btn)
	add_child(_strip)
	_strip.visible = false
```

- [ ] **Step 4: `timeline_dock.gd` — payload row CRUD, type parse, apply, populate**

Add these methods (place after `_apply_strip()`):
```gdscript
func _add_payload_row(key: String, type_idx: int, value_text: String) -> void:
	var row := HBoxContainer.new()
	var k := LineEdit.new(); k.placeholder_text = "key"; k.text = key; k.custom_minimum_size.x = 90
	var ty := OptionButton.new()
	for tn in _PAYLOAD_TYPES: ty.add_item(tn)
	ty.select(clampi(type_idx, 0, _PAYLOAD_TYPES.size() - 1))
	var v := LineEdit.new(); v.placeholder_text = "value"; v.text = value_text; v.custom_minimum_size.x = 90
	var rm := Button.new(); rm.text = "−"
	var entry := {"row": row, "key": k, "type": ty, "value": v}
	k.text_submitted.connect(func(_s): _apply_payload())
	k.focus_exited.connect(_apply_payload)
	v.text_submitted.connect(func(_s): _apply_payload())
	v.focus_exited.connect(_apply_payload)
	ty.item_selected.connect(func(_i): _apply_payload())
	rm.pressed.connect(func(): _remove_payload_row(entry))
	row.add_child(k); row.add_child(ty); row.add_child(v); row.add_child(rm)
	_payload_box.add_child(row)
	_payload_rows.append(entry)

func _remove_payload_row(entry) -> void:
	_payload_rows.erase(entry)
	if is_instance_valid(entry.row): entry.row.queue_free()
	_apply_payload()

func _on_add_key() -> void:
	_add_payload_row("", 0, "")

func _clear_payload_rows() -> void:
	for e in _payload_rows:
		if is_instance_valid(e.row): e.row.queue_free()
	_payload_rows = []

func _type_index_of(val) -> int:
	match typeof(val):
		TYPE_INT: return 1
		TYPE_FLOAT: return 2
		TYPE_BOOL: return 3
		_: return 0

func _parse_payload_value(type_idx: int, text: String):
	match type_idx:
		1: return text.to_int()
		2: return text.to_float()
		3: return text.strip_edges().to_lower() in ["true", "1", "on", "yes"]
		_: return text

func _apply_payload() -> void:
	if _selected_notify == null: return
	var d := {}
	for e in _payload_rows:
		if not is_instance_valid(e.key): continue
		var key: String = e.key.text.strip_edges()
		if key == "": continue
		d[key] = _parse_payload_value(e.type.selected, e.value.text)
	if d.hash() == _selected_notify.payload.hash():
		return
	if undo_redo:
		undo_redo.create_action("Edit Notify Payload")
		undo_redo.add_do_property(_selected_notify, "payload", d)
		undo_redo.add_undo_property(_selected_notify, "payload", _selected_notify.payload)
		undo_redo.commit_action()
	else:
		_selected_notify.payload = d
	if track: track.emit_changed()
	if _view: _view.queue_redraw()
```
Replace `edit_notify()` (currently lines 93-99) to also (re)build the payload rows:
```gdscript
func edit_notify(n) -> void:
	_selected_notify = n
	_strip.visible = n != null
	_clear_payload_rows()
	if n != null:
		_name_edit.text = n.notify_name
		_channel_edit.text = n.channel
		for key in n.payload.keys():
			var val = n.payload[key]
			_add_payload_row(str(key), _type_index_of(val), str(val))
	if _view: _view.queue_redraw()
```

- [ ] **Step 5: Run the logic test**

Run: `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://_t_payload.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|Parse Error|error"`
Expected: `TEST-OK`, no parse/script errors.

- [ ] **Step 6: Manual editor checklist (human, record — do not run):** selecting a notify shows the payload panel; "+ Add key" adds a row; setting key/type/value and clicking away writes a typed entry; "−" removes a row; Ctrl+Z/Y undo/redo payload edits; Ctrl+S persists; "Edit in Inspector" still opens the full resource.

- [ ] **Step 7: Commit**

```bash
rm spine-godot/example-v4-extension/_t_payload.gd spine-godot/example-v4-extension/_t_payload.gd.uid spine-godot/example-v4-extension/_t_payload.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): inline typed payload editor

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## After all tasks

- Final whole-branch review over the three commits (base = pre-Task-1 HEAD → head), on the most capable model, with the deferred-Minor list.
- Capture self-validation (`--write-movie`): render the dock with notifies across `default`/`vfx`/`sfx` lanes and a selected notify showing typed payload rows; read frames; verify labelled stacked lanes, per-lane diamonds, events lane, payload panel.
- Copy the updated addon to the Shanhai project (`…/shanhai/addons/spine_notify_editor/`, DLL unchanged — Godot must be closed) for the human smoke test. Do not push.

## Self-Review (author checklist — done)

- **Spec coverage:** channel lanes (§3) → Task 1; frame-magnet snap (§4) → Task 2; typed payload (§5) → Task 3; undo/redo + fallback (§6) → all tasks; testing (§8) → per-task headless tests + capture. ✓
- **Type consistency:** `add_notify_at(t, channel)` (Task 1) is the only caller site changed (`_gui_input`); `notify_at_pos` replaces `notify_at_x` (only caller `_gui_input`); `snap()` signature unchanged (Task 2); `edit_notify(n)` signature unchanged, body extended (Task 3). `_extra_channels` produced by Task 1, consumed by `channels()`. ✓
- **No placeholders:** every code step is complete. ✓
