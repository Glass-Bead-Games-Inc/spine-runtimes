# SpineAnimation dock redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Rework the SpineAnimation dock into an AnimationPlayer-style timeline: taller labelled tracks, icon transport, events drawn as markers, marker labels + hover tooltips, and named/renameable channel tracks.

**Architecture:** Pure GDScript in the `spine_notify_editor` addon (`timeline_view.gd`, `timeline_dock.gd`). No C++, no DLL rebuild. Design: `docs/superpowers/specs/2026-07-16-spineanimation-dock-redesign-design.md`. Mock: `https://claude.ai/code/artifact/21473bf0-1838-4e52-8f2a-d109778bb679`.

**Tech Stack:** Godot 4.7 GDScript (`@tool`), the already-built spine-godot GDExtension (`SpineNotify.channel:String`, `.payload:Dictionary`).

## Global Constraints

- **GDScript-only**, files in `spine-godot/example-v4-extension/addons/spine_notify_editor/`. No C++, no DLL rebuild, no `plugin.cfg` change.
- **`@tool`**; every notify mutation goes through `EditorUndoRedoManager` with a **null-`undo_redo` direct-assignment fallback** (headless). Session channel add/rename of an EMPTY lane is not a resource mutation.
- **Frame-exact** snapping preserved (`round(t*fps)/fps`; magnet resolves to target's frame; Ctrl frees).
- Preserve shipped behavior: selection/strip/playhead reset on bind & animation-change; `_prune_selection`; `_sync_strip_if_stale` payload/name resync; per-frame work gated on `is_visible_in_tree()`.
- Editor icons via `get_theme_icon(name, "EditorIcons")` with a **null/text fallback** when the editor theme isn't loaded (headless / `--write-movie`).
- Each task: TDD headless logic test → RED → implement → GREEN (`TEST-OK`). Run: `timeout 90 godot --headless --path "D:/Workspace/spine-runtimes/spine-godot/example-v4-extension" "res://<t>.tscn" 2>&1 | grep -E "TEST-OK|TEST-FAIL|SCRIPT ERROR|Parse Error|error"`. Manual editor checklist is **pending-human** — record it, don't block the commit, don't claim interactive UI from a headless run.
- Before each commit: delete disposable `_t_*.gd/.gd.uid/.tscn`; `git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'`. Commit ONLY the addon dir. Trailer: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Do NOT push.
- GDScript 4.7 `:=` inference quirk: annotate explicit types (`var x: Type =`) where the parser needs it; never leave a real type error.

---

### Task 1: Timeline restructure — events-as-track, flag markers with labels, hover tooltips, taller rows, header column

Replaces `timeline_view.gd` wholesale and splits the dock layout into a left header column + the canvas.

**Files:**
- Rewrite: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_view.gd`
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_dock.gd` (layout split + `_rebuild_headers` + header-signature refresh)
- Test: `spine-godot/example-v4-extension/_t_redesign1.gd` + `.tscn`

**Interfaces (produced, later tasks depend on these):**
- `timeline_view.gd`: `tracks() -> Array` (`[{kind:"events",channel:""}]` + channels), `channels() -> Array`, `_row_at_y(y)->int` (−1 = ruler), `_row_top/_row_center(i)`, `_channel_row_index(ch)`, `marker_at_pos(x,y)` → `{kind,ref,row}|null`, `add_notify_at(t,channel)`, `notifies_for_current()`, `_notifies_in_channel(ch)`, `set_playhead`, `snap`, `duration`, `x_from_time`/`time_from_x`, members `playhead_time`,`snap_enabled`,`fps`,`_drag_notify`,`_hovered`. Constants `RULER_H=28`,`LANE_H=36`,`PAD=6`,`HIT_PX=9`,`LABEL_MAX=120`,`MAGNET_PX=10`,`FLAG_H=22`.
- `timeline_dock.gd`: `_headers` (VBoxContainer), `_timeline_row` (HBoxContainer), `_rebuild_headers()`, `_headers_sig` cache; `_view` now lives inside `_timeline_row`.

- [ ] **Step 1: Write the failing test** — `_t_redesign1.gd`

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new(); add_child(dock)
	dock.setup(null, null)
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player); dock.select_animation("walk"); dock.ensure_track()
	var view = dock._view
	view.size = Vector2(1000, 320); view.refresh()
	view.add_notify_at(0.20, "vfx")
	view.add_notify_at(0.60, "sfx")
	# tracks(): events first, then default/sfx/vfx
	var tks: Array = view.tracks()
	var tk_ok: bool = tks.size() == 4 and tks[0].kind == "events" and tks[1].channel == "default" and tks[2].channel == "sfx" and tks[3].channel == "vfx"
	# row geometry: events row 0, vfx is row 3
	var vfx_row: int = view._channel_row_index("vfx")
	var row_ok: bool = vfx_row == 3 and view._row_at_y(view._row_center(3)) == 3 and view._row_at_y(4.0) == -1
	# marker hit-test: a notify only in its channel row; events row has the spine event
	var nvfx = null
	for n in dock.track.notifies:
		if n.channel == "vfx": nvfx = n
	var hit = view.marker_at_pos(view.x_from_time(0.20), view._row_center(3))
	var hit_ok: bool = hit != null and hit.kind == "notify" and hit.ref == nvfx
	var ev_hit = view.marker_at_pos(view.x_from_time(0.5), view._row_center(0))   # footstep event at 0.5
	var ev_ok: bool = ev_hit != null and ev_hit.kind == "event"
	# events row is not addable (clicking it scrubs, not adds) -> covered by _gui_input; here assert marker_at_pos on empty channel spot is null
	var empty_ok: bool = view.marker_at_pos(view.x_from_time(0.9), view._row_center(1)) == null
	# header column exists and rebuilds to match tracks (events + 3 channels = 4 header rows under the +New cell)
	dock._rebuild_headers()
	var head_ok: bool = dock._headers != null and dock._header_row_count() == 4
	var ok: bool = tk_ok and row_ok and hit_ok and ev_ok and empty_ok and head_ok
	print("TEST-OK" if ok else "TEST-FAIL tk=%s row=%s hit=%s ev=%s empty=%s head=%s" % [tk_ok, row_ok, hit_ok, ev_ok, empty_ok, head_ok])
	get_tree().quit()
```
`_t_redesign1.tscn`: `Node` root + script. (`_header_row_count()` is a tiny test helper added in Step 3.)

- [ ] **Step 2: Run to verify it fails** → FAIL (`tracks`/`_row_center`/`marker_at_pos`/`_rebuild_headers` undefined).

- [ ] **Step 3: Rewrite `timeline_view.gd`** with this exact content:

```gdscript
@tool
extends Control

const RULER_H := 28.0
const LANE_H := 36.0
const PAD := 6.0
const HIT_PX := 9.0
const LABEL_MAX := 120.0
const MAGNET_PX := 10.0
const FLAG_H := 22.0

var dock
var playhead_time: float = 0.0
var snap_enabled: bool = true
var fps: float = 30.0
var _events: Array = []
var _drag_notify = null
var _drag_start_time := 0.0
var _hovered = null                 # {kind, ref, row} or null

func setup(p_dock) -> void:
	dock = p_dock
	focus_mode = Control.FOCUS_CLICK
	mouse_filter = Control.MOUSE_FILTER_STOP

func refresh() -> void:
	fps = _skeleton_fps()
	_events = _load_events()
	custom_minimum_size.y = RULER_H + tracks().size() * LANE_H + PAD
	queue_redraw()

func _skeleton_fps() -> float:
	if dock and dock.sprite and dock.sprite.skeleton_data_res:
		var f: float = dock.sprite.skeleton_data_res.get_fps()
		if f > 0.0: return f
	return 30.0

func _load_events() -> Array:
	if dock and dock.sprite and dock.sprite.skeleton_data_res:
		return dock.sprite.skeleton_data_res.get_animation_events(dock._current_animation())
	return []

func tracks() -> Array:
	var out: Array = [{"kind": "events", "channel": ""}]
	for ch in channels():
		out.append({"kind": "notify", "channel": ch})
	return out

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

func _channel_row_index(ch: String) -> int:
	var c: String = ch if ch != "" else "default"
	var idx: int = channels().find(c)
	return (idx + 1) if idx >= 0 else 1

func _row_top(i: int) -> float:
	return RULER_H + i * LANE_H

func _row_center(i: int) -> float:
	return _row_top(i) + LANE_H * 0.5

func _row_at_y(y: float) -> int:
	if y < RULER_H: return -1
	var i: int = int((y - RULER_H) / LANE_H)
	if i < 0 or i >= tracks().size(): return -1
	return i

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
	if not snap_enabled or fps <= 0.0:
		return t
	var f: float = round(t * fps) / fps
	var tx: float = x_from_time(t)
	var best = null
	var best_px: float = MAGNET_PX
	for e in _events:
		var et: float = e.get("time", 0.0)
		var d: float = abs(x_from_time(et) - tx)
		if d <= best_px: best = et; best_px = d
	for n in notifies_for_current():
		if n == _drag_notify: continue
		var dn: float = abs(x_from_time(n.time) - tx)
		if dn <= best_px: best = n.time; best_px = dn
	if best != null: return round(float(best) * fps) / fps
	return f

func set_playhead(t: float, do_pose := true) -> void:
	playhead_time = clamp(t, 0.0, duration())
	if do_pose and dock and dock.sprite:
		dock.sprite.pose_at(dock._current_animation(), playhead_time)
	queue_redraw()

func notifies_for_current() -> Array:
	var out := []
	if dock and dock.track:
		var anim: String = dock._current_animation()
		for n in dock.track.notifies:
			if n != null and n.animation_name == anim:
				out.append(n)
	return out

func _notifies_in_channel(ch: String) -> Array:
	var c: String = ch if ch != "" else "default"
	var out := []
	for n in notifies_for_current():
		var nch: String = n.channel if n.channel != "" else "default"
		if nch == c: out.append(n)
	return out

func marker_at_pos(x: float, y: float):
	var row: int = _row_at_y(y)
	if row < 0: return null
	var tk: Dictionary = tracks()[row]
	if tk.kind == "events":
		for e in _events:
			if abs(x_from_time(e.get("time", 0.0)) - x) <= HIT_PX:
				return {"kind": "event", "ref": e, "row": row}
		return null
	for n in _notifies_in_channel(tk.channel):
		if abs(x_from_time(n.time) - x) <= HIT_PX:
			return {"kind": "notify", "ref": n, "row": row}
	return null

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

func delete_notify(n) -> void:
	if dock.track == null or n == null: return
	var arr: Array = dock.track.notifies.duplicate()
	arr.erase(n)
	_set_notifies(arr, "Delete Notify")
	if dock._selected_notify == n: dock.edit_notify(null)
	queue_redraw()

func move_notify(n, t: float) -> void:
	if n == null: return
	n.time = clamp(t, 0.0, duration())
	if dock.track: dock.track.emit_changed()
	queue_redraw()

func _commit_move(n, old_t: float, new_t: float) -> void:
	if dock.undo_redo:
		dock.undo_redo.create_action("Move Notify")
		dock.undo_redo.add_do_property(n, "time", new_t)
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

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			var row: int = _row_at_y(event.position.y)
			if row >= 1:
				var tk: Dictionary = tracks()[row]
				var hit = marker_at_pos(event.position.x, event.position.y)
				if hit != null and hit.kind == "notify":
					_drag_notify = hit.ref; _drag_start_time = hit.ref.time; dock.edit_notify(hit.ref)
				else:
					var t := time_from_x(event.position.x)
					if not event.ctrl_pressed: t = snap(t)
					add_notify_at(t, tk.channel)
				accept_event(); return
			var t2 := time_from_x(event.position.x)
			if not event.ctrl_pressed: t2 = snap(t2)
			set_playhead(t2); accept_event()
		else:
			if _drag_notify != null and _drag_notify.time != _drag_start_time:
				_commit_move(_drag_notify, _drag_start_time, _drag_notify.time)
			_drag_notify = null
	elif event is InputEventMouseMotion:
		if event.button_mask & MOUSE_BUTTON_MASK_LEFT:
			var t := time_from_x(event.position.x)
			if not event.ctrl_pressed: t = snap(t)
			if _drag_notify != null:
				move_notify(_drag_notify, t); set_playhead(t)
			else:
				set_playhead(t)
			accept_event()
		else:
			_update_hover(event.position)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_DELETE:
		if dock._selected_notify != null: delete_notify(dock._selected_notify); accept_event()

func _update_hover(pos: Vector2) -> void:
	var h = marker_at_pos(pos.x, pos.y)
	var changed: bool = (h == null) != (_hovered == null)
	if not changed and h != null and _hovered != null:
		changed = h.ref != _hovered.ref
	if changed:
		_hovered = h
		queue_redraw()

func _notification(what: int) -> void:
	if what == NOTIFICATION_MOUSE_EXIT and _hovered != null:
		_hovered = null
		queue_redraw()

func _elide(nm: String, font: Font, fsize: int) -> String:
	if font.get_string_size(nm, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize).x <= LABEL_MAX:
		return nm
	var s := nm
	while s.length() > 1 and font.get_string_size(s + "…", HORIZONTAL_ALIGNMENT_LEFT, -1, fsize).x > LABEL_MAX:
		s = s.substr(0, s.length() - 1)
	return s + "…"

func _draw() -> void:
	var font := get_theme_default_font()
	var fsize := 13
	var dur := duration()
	var w := _track_width()
	var left := _track_left()
	draw_line(Vector2(left, RULER_H), Vector2(left + w, RULER_H), Color(0.5,0.5,0.55), 1.0)
	if snap_enabled and fps > 0.0:
		var frames := int(dur * fps)
		for f in range(frames + 1):
			var fx := x_from_time(f / fps)
			draw_line(Vector2(fx, RULER_H - 4), Vector2(fx, RULER_H), Color(0.4,0.4,0.45), 1.0)
	for q in range(5):
		var qx := left + w * q / 4.0
		draw_string(font, Vector2(qx + 2, 14), "%.2f" % (dur * q / 4.0), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(0.6,0.62,0.7))
	var tks := tracks()
	for i in tks.size():
		var top := _row_top(i)
		draw_line(Vector2(left, top), Vector2(left + w, top), Color(0.22,0.23,0.28), 1.0)
		var tk: Dictionary = tks[i]
		if tk.kind == "events":
			for e in _events:
				_draw_flag(x_from_time(e.get("time", 0.0)), _row_center(i), String(e.get("name","event")), Color(0.29,0.72,0.69), false, font, fsize)
		else:
			for n in _notifies_in_channel(tk.channel):
				var sel: bool = (dock._selected_notify == n)
				var col := Color(1.0,0.79,0.29) if sel else Color(0.88,0.60,0.24)
				_draw_flag(x_from_time(n.time), _row_center(i), n.notify_name, col, sel, font, fsize)
	var px := x_from_time(playhead_time)
	draw_line(Vector2(px, RULER_H - 6), Vector2(px, size.y), Color(0.95,0.76,0.29), 2.0)
	if _hovered != null:
		_draw_tooltip(font)

func _draw_flag(cx: float, cy: float, label: String, col: Color, sel: bool, font: Font, fsize: int) -> void:
	var txt := _elide(label, font, fsize)
	var tw := font.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize).x
	var bw := tw + 16.0
	var half := FLAG_H * 0.5
	var rect := Rect2(cx + 3, cy - half, bw, FLAG_H)
	var nub := PackedVector2Array([Vector2(cx, cy), Vector2(cx + 5, cy - 5), Vector2(cx + 5, cy + 5)])
	draw_colored_polygon(nub, col)
	draw_rect(rect, col, true)
	if sel:
		draw_rect(rect.grow(1.5), Color(1,1,1,0.85), false, 1.5)
	var ink := Color(0.10,0.09,0.05) if col.get_luminance() > 0.5 else Color(1,1,1,0.92)
	draw_string(font, Vector2(cx + 11, cy + fsize * 0.35), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize, ink)

func _draw_tooltip(font: Font) -> void:
	var ref = _hovered.ref
	var is_ev: bool = _hovered.kind == "event"
	var t: float = ref.get("time", 0.0) if is_ev else ref.time
	var nm: String = String(ref.get("name","event")) if is_ev else ref.notify_name
	var lines: Array = []
	lines.append(nm + ("  (event)" if is_ev else "  (%s)" % ref.channel))
	lines.append("%.3f s · f%d" % [t, int(round(t * fps))])
	if not is_ev:
		lines.append("%d payload key(s)" % ref.payload.size())
	var fsize := 12
	var wmax := 0.0
	for ln in lines:
		wmax = max(wmax, font.get_string_size(ln, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize).x)
	var bw := wmax + 16.0
	var bh := lines.size() * (fsize + 6) + 8.0
	var cx: float = clamp(x_from_time(t) + 8.0, 2.0, max(2.0, size.x - bw - 2.0))
	var cy: float = max(2.0, _row_center(_hovered.row) - bh - 10.0)
	var box := Rect2(cx, cy, bw, bh)
	draw_rect(box, Color(0.07,0.075,0.1,0.97), true)
	draw_rect(box, Color(0.3,0.32,0.38), false, 1.0)
	var yy := cy + fsize + 4.0
	for idx in lines.size():
		var c := Color(1,1,1,0.95) if idx == 0 else Color(0.7,0.72,0.8)
		draw_string(font, Vector2(cx + 8, yy), lines[idx], HORIZONTAL_ALIGNMENT_LEFT, -1, fsize, c)
		yy += fsize + 6
```

- [ ] **Step 4: `timeline_dock.gd` — split the layout, add the header column**

In `_build_ui()`, replace the block that creates `_view` and adds it directly to the dock. Currently it is:
```gdscript
	var ViewScript := load("res://addons/spine_notify_editor/timeline_view.gd")
	_view = ViewScript.new()
	_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_view.setup(self)
	add_child(_view)
```
Replace with a header column + view inside an HBox:
```gdscript
	_timeline_row = HBoxContainer.new()
	_timeline_row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_timeline_row.add_theme_constant_override("separation", 0)
	_headers = VBoxContainer.new()
	_headers.custom_minimum_size = Vector2(176, 0)
	_headers.add_theme_constant_override("separation", 0)
	_timeline_row.add_child(_headers)
	var ViewScript := load("res://addons/spine_notify_editor/timeline_view.gd")
	_view = ViewScript.new()
	_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_view.setup(self)
	_timeline_row.add_child(_view)
	add_child(_timeline_row)
	_rebuild_headers()
```
Add these members near the other `_view` members (top of file):
```gdscript
var _timeline_row: HBoxContainer
var _headers: VBoxContainer
var _headers_sig: String = ""
```
Add the header builder + a signature check (rebuild only when the channel set changes) + the test helper:
```gdscript
func _rebuild_headers() -> void:
	if _headers == null: return
	for c in _headers.get_children():
		c.queue_free()
	# top cell (ruler height) — "+ New track" is wired in a later task; a placeholder for now
	var top := Control.new()
	top.custom_minimum_size = Vector2(0, 28)   # RULER_H
	_headers.add_child(top)
	for tk in _view.tracks():
		var row := HBoxContainer.new()
		row.custom_minimum_size = Vector2(0, 36)  # LANE_H
		var sw := ColorRect.new()
		sw.custom_minimum_size = Vector2(9, 9)
		sw.color = Color(0.29,0.72,0.69) if tk.kind == "events" else Color(0.88,0.60,0.24)
		row.add_child(sw)
		var lbl := Label.new()
		lbl.text = "Events" if tk.kind == "events" else String(tk.channel)
		lbl.add_theme_font_size_override("font_size", 13)
		row.add_child(lbl)
		_headers.add_child(row)
	_headers_sig = ",".join(_view.channels())

func _header_row_count() -> int:
	# header rows excluding the top ruler-height cell
	return _headers.get_child_count() - 1 if _headers else 0
```
In `_process(delta)`, inside the existing `if _view.is_visible_in_tree():` block (which calls `_prune_selection()` + `_sync_strip_if_stale()` + `_view.queue_redraw()`), add a header-signature check so headers rebuild only when channels change:
```gdscript
		var sig: String = ",".join(_view.channels())
		if sig != _headers_sig:
			_rebuild_headers()
```

- [ ] **Step 5: Run the logic test** → `TEST-OK`.

- [ ] **Step 6: Manual editor checklist (human, record — do not run):** tracks are taller with 13px labels; an Events track shows teal event flags with names; channel tracks show amber notify flags with names; the left header column lists Events + channels aligned to the rows; hovering a marker shows a tooltip; clicking a channel row adds a notify; the Events row and ruler scrub (don't add).

- [ ] **Step 7: Commit**

```bash
rm spine-godot/example-v4-extension/_t_redesign1.gd spine-godot/example-v4-extension/_t_redesign1.gd.uid spine-godot/example-v4-extension/_t_redesign1.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): AnimationPlayer-style timeline — events track, flag markers, labels, tooltips, header column

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Icon transport + playback (forward/backward, from start/end/current)

**Files:**
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_dock.gd`
- Test: `spine-godot/example-v4-extension/_t_transport.gd` + `.tscn`

**Interfaces:**
- Produces: `play_dir: int`, `playing: bool` (already exists), `_play_from_start()`, `_play_from_current()`, `_play_bw_from_current()`, `_play_bw_from_end()`, `_stop()`, `_editor_icon(name) -> Texture2D`. `_process` handles bidirectional advance + loop.

- [ ] **Step 1: Write the failing test** — `_t_transport.gd`

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new(); add_child(dock)
	dock.setup(null, null)
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player); dock.select_animation("walk"); dock.ensure_track()
	var view = dock._view; view.size = Vector2(1000, 320); view.refresh()
	var dur: float = view.duration()
	# play from start
	dock._play_from_start()
	var s1: bool = abs(view.playhead_time) < 1e-6 and dock.play_dir == 1 and dock.playing
	# play backwards from end
	dock._play_bw_from_end()
	var s2: bool = abs(view.playhead_time - dur) < 1e-4 and dock.play_dir == -1 and dock.playing
	# forward wrap: at near-end, a big step with loop wraps to 0
	dock.loop_enabled = true; dock.play_dir = 1; dock.playing = true
	view.set_playhead(dur - 0.01)
	dock._process(0.1)                       # overshoots -> wraps
	var s3: bool = view.playhead_time < dur * 0.5 and dock.playing
	# forward stop (no loop): clamps to end and stops
	dock.loop_enabled = false; dock.play_dir = 1; dock.playing = true
	view.set_playhead(dur - 0.01)
	dock._process(0.1)
	var s4: bool = abs(view.playhead_time - dur) < 1e-4 and not dock.playing
	# backward wrap with loop
	dock.loop_enabled = true; dock.play_dir = -1; dock.playing = true
	view.set_playhead(0.01)
	dock._process(0.1)
	var s5: bool = view.playhead_time > dur * 0.5 and dock.playing
	# stop clears playing
	dock._stop()
	var s6: bool = not dock.playing
	var ok: bool = s1 and s2 and s3 and s4 and s5 and s6
	print("TEST-OK" if ok else "TEST-FAIL s1=%s s2=%s s3=%s s4=%s s5=%s s6=%s" % [s1,s2,s3,s4,s5,s6])
	get_tree().quit()
```
`_t_transport.tscn`: `Node` root + script.

- [ ] **Step 2: Run to verify it fails** → FAIL (`_play_from_start`/`play_dir`/`_stop` undefined).

- [ ] **Step 3: Replace the toolbar transport in `_build_ui()`**

Currently the toolbar builds `_play_btn` (a text Button toggling `playing`) plus `_loop_btn`/`_snap_btn`. Replace the `_play_btn` creation and its `pressed` handler with an icon transport group. Add members near `_play_btn`:
```gdscript
var play_dir: int = 1
var _tp_bw_from: Button
var _tp_bw_end: Button
var _tp_stop: Button
var _tp_start: Button
var _tp_play: Button
```
Where `_play_btn` was created and added to `_toolbar`, instead build the group (keep `_loop_btn`/`_snap_btn`/`_time_label` after it):
```gdscript
	_tp_bw_from = _make_tp("PlayBackwards", "|<", "Play backwards from current pos.", _play_bw_from_current)
	_tp_bw_end  = _make_tp("PlayStartBackwards", "<|", "Play backwards from end.", _play_bw_from_end)
	_tp_stop    = _make_tp("Stop", "[]", "Pause/stop.", _stop)
	_tp_start   = _make_tp("PlayStart", "|>", "Play from start.", _play_from_start)
	_tp_play    = _make_tp("Play", ">", "Play from current pos.", _play_from_current)
```
Delete the old `_play_btn` var/usages. Add helpers (place after `_build_ui`):
```gdscript
func _editor_icon(nm: String) -> Texture2D:
	if has_theme_icon(nm, "EditorIcons"):
		return get_theme_icon(nm, "EditorIcons")
	return null

func _make_tp(icon_name: String, fallback: String, tip: String, fn: Callable) -> Button:
	var b := Button.new()
	var ic := _editor_icon(icon_name)
	if ic != null: b.icon = ic
	else: b.text = fallback
	b.tooltip_text = tip
	b.pressed.connect(fn)
	_toolbar.add_child(b)
	return b

func _play_from_start() -> void:
	if _view: _view.set_playhead(0.0)
	play_dir = 1; playing = true

func _play_from_current() -> void:
	play_dir = 1; playing = true

func _play_bw_from_current() -> void:
	play_dir = -1; playing = true

func _play_bw_from_end() -> void:
	if _view: _view.set_playhead(_view.duration())
	play_dir = -1; playing = true

func _stop() -> void:
	if not playing and _view and _view.playhead_time > 0.0:
		_view.set_playhead(0.0)     # second press when stopped -> rewind
	playing = false
```
Replace the playback advance in `_process(delta)` (currently the `if playing and _view.visible:` forward-only block) with a bidirectional version:
```gdscript
	if playing and _view.visible:
		var dur: float = _view.duration()
		var t: float = _view.playhead_time + play_dir * delta
		if play_dir > 0 and t >= dur:
			if loop_enabled: t = 0.0
			else: t = dur; playing = false
		elif play_dir < 0 and t <= 0.0:
			if loop_enabled: t = dur
			else: t = 0.0; playing = false
		_view.set_playhead(t)
```

- [ ] **Step 4: Run the logic test** → `TEST-OK`.

- [ ] **Step 5: Manual editor checklist (human, record):** transport shows AnimationPlayer icons; from-start/from-current/backwards/backwards-from-end all play in the right direction; Stop pauses, a second Stop rewinds to 0; Loop wraps both directions; the time readout tracks.

- [ ] **Step 6: Commit**

```bash
rm spine-godot/example-v4-extension/_t_transport.gd spine-godot/example-v4-extension/_t_transport.gd.uid spine-godot/example-v4-extension/_t_transport.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): AnimationPlayer-style icon transport + bidirectional playback

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Track headers — add named track + double-click rename

**Files:**
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_dock.gd`
- Test: `spine-godot/example-v4-extension/_t_tracks.gd` + `.tscn`

**Interfaces:**
- Produces: `_add_named_channel(name)`, `_rename_channel(old, new)`, and a "+ New track" control + per-header rename entry in `_rebuild_headers()`.

- [ ] **Step 1: Write the failing test** — `_t_tracks.gd`

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new(); add_child(dock)
	dock.setup(null, null)
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player); dock.select_animation("walk"); dock.ensure_track()
	var view = dock._view; view.size = Vector2(1000, 320); view.refresh()
	view.add_notify_at(0.3, "vfx")
	# add a named session channel
	dock._add_named_channel("combat")
	var added: bool = "combat" in view.channels()
	# blank / duplicate rejected
	dock._add_named_channel("")
	dock._add_named_channel("vfx")
	var noblank: bool = view.channels().count("combat") == 1 and not ("" in view.channels())
	# rename re-tags the current-animation notifies in that lane
	dock._rename_channel("vfx", "muzzle")
	var n = dock.track.notifies[dock.track.notifies.size() - 1]
	var renamed: bool = n.channel == "muzzle" and ("muzzle" in view.channels()) and not ("vfx" in view.channels())
	# duplicate / default / blank rename rejected
	dock._rename_channel("muzzle", "default")     # collides with reserved -> rejected
	var rej: bool = n.channel == "muzzle"
	var ok: bool = added and noblank and renamed and rej
	print("TEST-OK" if ok else "TEST-FAIL add=%s noblank=%s ren=%s rej=%s" % [added, noblank, renamed, rej])
	get_tree().quit()
```
`_t_tracks.tscn`: `Node` root + script.

- [ ] **Step 2: Run to verify it fails** → FAIL (`_add_named_channel`/`_rename_channel` undefined).

- [ ] **Step 3: Add the channel add/rename logic**

Add to `timeline_dock.gd`:
```gdscript
func _add_named_channel(cname: String) -> void:
	var nm: String = cname.strip_edges()
	if nm == "": return
	if nm in _view.channels(): return
	if not (nm in _extra_channels): _extra_channels.append(nm)
	_rebuild_headers()
	if _view: _view.refresh()

func _rename_channel(old_name: String, new_name: String) -> void:
	var nn: String = new_name.strip_edges()
	if nn == "" or nn == old_name or old_name == "default": return
	if nn in _view.channels(): return                 # duplicate rejected
	# re-tag current-animation notifies in that lane (one undoable action)
	var targets: Array = []
	if track != null:
		var anim: String = _current_animation()
		for n in track.notifies:
			if n != null and n.animation_name == anim and (n.channel if n.channel != "" else "default") == old_name:
				targets.append(n)
	if undo_redo and not targets.is_empty():
		undo_redo.create_action("Rename Notify Channel")
		for n in targets:
			undo_redo.add_do_property(n, "channel", nn)
			undo_redo.add_undo_property(n, "channel", n.channel)
		undo_redo.commit_action()
	else:
		for n in targets: n.channel = nn
	if old_name in _extra_channels:
		_extra_channels[_extra_channels.find(old_name)] = nn
	if track: track.emit_changed()
	_rebuild_headers()
	if _view: _view.refresh()
```

- [ ] **Step 4: Wire the header UI in `_rebuild_headers()`**

Replace the placeholder top cell and the plain Label rows from Task 1 with interactive ones. The top cell becomes a "+ New track" button that reveals an inline `LineEdit`; each non-events, non-`default` header name becomes double-click-renameable. Replace the body of `_rebuild_headers()` (from Task 1) with the version below. **Reentrancy note:** rename commits from a header `LineEdit`'s own `text_submitted` signal, which calls `_rename_channel` → `_rebuild_headers`; the cleanup MUST use `remove_child(c)` (immediate detach → correct child count) + `c.queue_free()` (deferred free → safe to free the very `LineEdit` whose signal is on the call stack). Do NOT use immediate `free()` here.
```gdscript
func _rebuild_headers() -> void:
	if _headers == null: return
	for c in _headers.get_children():
		_headers.remove_child(c)
		c.queue_free()
	var top := HBoxContainer.new()
	top.custom_minimum_size = Vector2(0, 28)
	var addbtn := Button.new()
	addbtn.text = "＋ New track"
	addbtn.tooltip_text = "Add a named channel track"
	var addfield := LineEdit.new()
	addfield.placeholder_text = "channel name…"
	addfield.visible = false
	addfield.custom_minimum_size = Vector2(120, 0)
	addbtn.pressed.connect(func():
		addbtn.visible = false; addfield.visible = true; addfield.grab_focus())
	addfield.text_submitted.connect(func(s):
		_add_named_channel(s))
	addfield.focus_exited.connect(func():
		addfield.visible = false; addbtn.visible = true)
	top.add_child(addbtn); top.add_child(addfield)
	_headers.add_child(top)
	for tk in _view.tracks():
		var row := HBoxContainer.new()
		row.custom_minimum_size = Vector2(0, 36)
		var sw := ColorRect.new(); sw.custom_minimum_size = Vector2(9, 9)
		sw.color = Color(0.29,0.72,0.69) if tk.kind == "events" else Color(0.88,0.60,0.24)
		row.add_child(sw)
		var is_events: bool = tk.kind == "events"
		var cname: String = "Events" if is_events else String(tk.channel)
		var renameable: bool = (not is_events) and cname != "default"
		if renameable:
			var nb := Button.new(); nb.flat = true; nb.text = cname
			nb.add_theme_font_size_override("font_size", 13)
			nb.tooltip_text = "Double-click to rename"
			nb.gui_input.connect(func(ev):
				if ev is InputEventMouseButton and ev.double_click and ev.pressed:
					_begin_rename(row, nb, cname))
			row.add_child(nb)
		else:
			var lbl := Label.new(); lbl.text = cname
			lbl.add_theme_font_size_override("font_size", 13)
			if is_events: lbl.add_theme_color_override("font_color", Color(0.29,0.72,0.69))
			row.add_child(lbl)
		_headers.add_child(row)
	_headers_sig = ",".join(_view.channels())

func _begin_rename(row: HBoxContainer, name_btn: Button, cur: String) -> void:
	name_btn.visible = false
	var ed := LineEdit.new(); ed.text = cur; ed.custom_minimum_size = Vector2(120, 0)
	ed.select_all()
	row.add_child(ed); ed.grab_focus()
	ed.text_submitted.connect(func(s): _rename_channel(cur, s))     # _rebuild_headers replaces the row
	ed.focus_exited.connect(func():
		if is_instance_valid(ed): ed.queue_free()
		name_btn.visible = true)
```

- [ ] **Step 5: Run the logic test** → `TEST-OK`.

- [ ] **Step 6: Manual editor checklist (human, record):** "＋ New track" reveals a name field, Enter adds a labelled lane; double-clicking a channel name (not Events / not default) opens an inline editor; committing re-tags that lane's markers and renames the header; blank/duplicate/default rejected.

- [ ] **Step 7: Commit**

```bash
rm spine-godot/example-v4-extension/_t_tracks.gd spine-godot/example-v4-extension/_t_tracks.gd.uid spine-godot/example-v4-extension/_t_tracks.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): add named channel tracks + double-click rename

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Detail strip — drop channel editor, read-only channel chip

**Files:**
- Modify: `spine-godot/example-v4-extension/addons/spine_notify_editor/timeline_dock.gd`
- Test: `spine-godot/example-v4-extension/_t_strip.gd` + `.tscn`

**Interfaces:**
- Removes `_channel_edit`; adds `_channel_chip: Label`. `_apply_strip` writes only `notify_name`. `edit_notify` sets the chip.

- [ ] **Step 1: Write the failing test** — `_t_strip.gd`

```gdscript
extends Node
func _ready() -> void:
	var DockScript := load("res://addons/spine_notify_editor/timeline_dock.gd")
	var dock = DockScript.new(); add_child(dock)
	dock.setup(null, null)
	var data := load("res://assets/spineboy/spineboy-data-res.tres")
	var sprite := SpineSprite3D.new(); sprite.skeleton_data_res = data; add_child(sprite)
	var player := SpineAnimationPlayer.new(); sprite.add_child(player)
	dock.bind(player); dock.select_animation("walk"); dock.ensure_track()
	var view = dock._view; view.size = Vector2(1000, 320); view.refresh()
	view.add_notify_at(0.3, "vfx")
	var n = dock.track.notifies[dock.track.notifies.size() - 1]
	dock.edit_notify(n)
	# chip reflects the notify's channel; there is no channel editor
	var chip_ok: bool = dock._channel_chip != null and dock._channel_chip.text.find("vfx") >= 0
	var no_edit: bool = not ("_channel_edit" in dock) or dock.get("_channel_edit") == null
	# renaming via the name field writes notify_name only, leaves channel intact
	dock._name_edit.text = "boom"
	dock._apply_strip()
	var name_ok: bool = n.notify_name == "boom" and n.channel == "vfx"
	var ok: bool = chip_ok and no_edit and name_ok
	print("TEST-OK" if ok else "TEST-FAIL chip=%s noedit=%s name=%s" % [chip_ok, no_edit, name_ok])
	get_tree().quit()
```
`_t_strip.tscn`: `Node` root + script.

- [ ] **Step 2: Run to verify it fails** → FAIL (`_channel_chip` undefined / `_apply_strip` still touches channel).

- [ ] **Step 3: Rework the strip row 1**

Change the member `var _channel_edit: LineEdit` to `var _channel_chip: Label`. In `_build_ui()`, where row 1 currently adds `_channel_edit` (with its `text_submitted`/`focus_exited` connections), replace it with a read-only chip:
```gdscript
	_channel_chip = Label.new()
	_channel_chip.add_theme_font_size_override("font_size", 12)
	_channel_chip.add_theme_color_override("font_color", Color(0.88,0.60,0.24))
	row1.add_child(_channel_chip)
```
(Remove the two `_channel_edit.*.connect(...)` lines.) Rework `_apply_strip()` to write only the name:
```gdscript
func _apply_strip() -> void:
	if _selected_notify == null: return
	var new_name: String = _name_edit.text
	if new_name == _selected_notify.notify_name:
		return
	if undo_redo:
		undo_redo.create_action("Rename Notify")
		undo_redo.add_do_property(_selected_notify, "notify_name", new_name)
		undo_redo.add_undo_property(_selected_notify, "notify_name", _selected_notify.notify_name)
		undo_redo.commit_action()
	else:
		_selected_notify.notify_name = new_name
	if track: track.emit_changed()
	if _view: _view.queue_redraw()
```
In `edit_notify(n)`, replace the `_channel_edit.text = n.channel` line with the chip:
```gdscript
		_channel_chip.text = "  " + n.channel
```

**Also remove the now-redundant old toolbar add-channel control** (superseded by Task 3's header "+ New track"): delete the members `var _new_channel_edit: LineEdit` and `var _add_channel_btn: Button`, their creation+wiring block in `_build_ui()` (`_new_channel_edit = LineEdit.new()` … `_toolbar.add_child(_add_channel_btn)`), and the `func _add_channel()` method. Nothing else references them (the header uses `_add_named_channel`).
Update `_displayed_signature()` (used by `_sync_strip_if_stale`) — it currently references `_channel_edit.text`; change that term to `_selected_notify.channel` (channel is no longer editable so it always matches the resource):
```gdscript
	return "%s|%s|%d" % [_name_edit.text, _selected_notify.channel, d.hash()]
```

- [ ] **Step 4: Run the logic test** → `TEST-OK`.

- [ ] **Step 5: Manual editor checklist (human, record):** the strip shows name + a read-only channel chip (no channel field); editing the name updates the marker; Ctrl+Z/Y still work; the channel only changes via track rename / adding in a lane.

- [ ] **Step 6: Commit**

```bash
rm spine-godot/example-v4-extension/_t_strip.gd spine-godot/example-v4-extension/_t_strip.gd.uid spine-godot/example-v4-extension/_t_strip.tscn
git checkout -- 'spine-godot/example-v4-extension/**/*.import' 'spine-godot/example-v4-extension/*.import'
git add spine-godot/example-v4-extension/addons/spine_notify_editor/
git commit -m "feat(spine-notify-editor): read-only channel chip in the detail strip (no channel editor)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## After all tasks

- Final whole-branch review over the 4 commits (base = pre-Task-1 HEAD → head), most-capable model, with the deferred-Minor list.
- `--write-movie` capture: taller labelled tracks, teal event flags + amber notify flags with names, a hovered tooltip, the icon transport, a header mid-rename, the strip with the read-only chip + payload.
- Copy the updated addon to Shanhai (`…/shanhai/addons/spine_notify_editor/`, DLL unchanged) for the human smoke test. Do not push.

## Self-Review (author checklist — done)

- **Spec coverage:** 1 taller/bigger → Task 1 (LANE_H/font). 2 icon transport → Task 2. 3 chip → Task 4. 4 events-as-markers → Task 1 (`_draw_flag` on events row, teal). 5 labels+tooltips → Task 1 (`_draw_flag` labels + `_draw_tooltip`/`_update_hover`). 6 add/rename → Task 3. ✓
- **Type consistency:** `add_notify_at(t, channel)` unchanged; `marker_at_pos` replaces `notify_at_pos` (callers: `_gui_input`, `_update_hover`, tests). `_channel_edit`→`_channel_chip` (Task 4) updates `edit_notify`, `_apply_strip`, `_displayed_signature`, `_build_ui`. `playing`/`play_dir` in Task 2 `_process`. `_headers`/`_rebuild_headers`/`_headers_sig` produced Task 1, extended Task 3. ✓
- **No placeholders:** every code step complete; the Task-1 header rows are intentionally simple, upgraded to interactive in Task 3. ✓
