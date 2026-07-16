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
	if _hovered != null and _hovered.ref == n:
		_hovered = null
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
