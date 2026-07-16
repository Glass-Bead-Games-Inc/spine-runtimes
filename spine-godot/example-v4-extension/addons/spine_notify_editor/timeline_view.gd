@tool
extends Control

const RULER_H := 22.0
const EVENTS_H := 16.0
const PAD := 6.0
const EVENTS_Y := RULER_H + 10.0
const HIT_PX := 8.0
const LANE_H := 20.0
const LANE_LABEL_W := 72.0
const LANES_TOP := EVENTS_Y + 12.0     # 44.0
const MAGNET_PX := 10.0

var dock
var playhead_time: float = 0.0
var snap_enabled: bool = true
var fps: float = 30.0
var _events: Array = []              # [{time,name}] read-only spine events (filled in Task 5)
var _drag_notify = null
var _drag_start_time := 0.0

func setup(p_dock) -> void:
	dock = p_dock
	focus_mode = Control.FOCUS_CLICK

func refresh() -> void:
	fps = _skeleton_fps()
	_events = _load_events()
	custom_minimum_size.y = LANES_TOP + channels().size() * LANE_H + PAD
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

func notifies_for_current() -> Array:
	var out := []
	if dock and dock.track:
		var anim: String = dock._current_animation()
		for n in dock.track.notifies:
			if n != null and n.animation_name == anim:
				out.append(n)
	return out

func notify_at_pos(x: float, y: float):
	var ch: String = _channel_at_y(y)
	if ch == "":
		return null
	for n in notifies_for_current():
		var nch: String = n.channel if n.channel != "" else "default"
		if nch == ch and abs(x_from_time(n.time) - x) <= HIT_PX:
			return n
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

func duration() -> float:
	if dock and dock.sprite and dock.sprite.skeleton_data_res:
		var anim = dock.sprite.skeleton_data_res.find_animation(dock._current_animation())
		if anim != null: return max(anim.get_duration(), 0.0001)
	return 1.0

func _track_left() -> float:
	return PAD + LANE_LABEL_W

func _track_width() -> float:
	return max(size.x - _track_left() - PAD, 1.0)

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

func set_playhead(t: float, do_pose := true) -> void:
	playhead_time = clamp(t, 0.0, duration())
	if do_pose and dock and dock.sprite:
		dock.sprite.pose_at(dock._current_animation(), playhead_time)
	queue_redraw()

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
	# playhead
	var px := x_from_time(playhead_time)
	draw_line(Vector2(px, 0), Vector2(px, size.y), Color(1,0.8,0.2), 1.0)
