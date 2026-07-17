@tool
extends Control

const RULER_H := 28.0
const LANE_H := 36.0
const PAD := 6.0
const HIT_PX := 9.0
const LABEL_MAX := 120.0
const MAGNET_PX := 10.0
const FLAG_H := 24.0

# palette (matches the approved mock)
const C_LANE := Color(0.122, 0.129, 0.149)      # #1f2126
const C_LANE_ALT := Color(0.137, 0.149, 0.188)  # events row tint
const C_ROWLINE := Color(0.20, 0.212, 0.247)
const C_RULER := Color(0.42, 0.44, 0.50)
const C_TICK := Color(0.32, 0.34, 0.40)
const C_QLABEL := Color(0.55, 0.58, 0.66)
const C_NOTIFY := Color(0.878, 0.600, 0.243)    # #e0993e
const C_NOTIFY_SEL := Color(1.0, 0.792, 0.29)   # #ffca4a
const C_EVENT := Color(0.286, 0.718, 0.686)     # #49b7b0
const C_PLAYHEAD := Color(0.949, 0.761, 0.290)  # #f2c24a

var dock
var playhead_time: float = 0.0
var snap_enabled: bool = true
var fps: float = 30.0
var _events: Array = []
var _drag_notify = null
var _drag_start_time := 0.0
var _hovered = null                 # {kind, ref, row} or null
var _flag_sb: StyleBoxFlat
var _tip_sb: StyleBoxFlat
var _chip_sb: StyleBoxFlat

func setup(p_dock) -> void:
	dock = p_dock
	focus_mode = Control.FOCUS_CLICK
	mouse_filter = Control.MOUSE_FILTER_STOP
	_flag_sb = StyleBoxFlat.new()
	_flag_sb.set_corner_radius_all(4)
	_flag_sb.shadow_size = 2
	_flag_sb.shadow_color = Color(0, 0, 0, 0.38)
	_flag_sb.shadow_offset = Vector2(0, 1)
	_tip_sb = StyleBoxFlat.new()
	_tip_sb.bg_color = Color(0.071, 0.075, 0.102, 0.98)
	_tip_sb.set_corner_radius_all(6)
	_tip_sb.set_border_width_all(1)
	_tip_sb.border_color = Color(0.20, 0.21, 0.25)
	_tip_sb.shadow_size = 10
	_tip_sb.shadow_color = Color(0, 0, 0, 0.5)
	_chip_sb = StyleBoxFlat.new()
	_chip_sb.set_corner_radius_all(9)
	_chip_sb.set_content_margin_all(3)

func refresh() -> void:
	_hovered = null
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
	if arr.has(n):
		arr.erase(n)
	else:
		# fallback: the selected instance may differ from the one in the track
		# (e.g. after a resource reload) — match by fields instead of identity.
		for i in range(arr.size()):
			var m = arr[i]
			if m != null and m.animation_name == n.animation_name and is_equal_approx(m.time, n.time) and m.channel == n.channel and m.notify_name == n.notify_name:
				arr.remove_at(i)
				break
	if arr.size() == dock.track.notifies.size():
		return   # nothing matched — don't record a no-op action
	_set_notifies(arr, "Delete Notify")
	if dock._selected_notify != null and not arr.has(dock._selected_notify):
		dock.edit_notify(null)
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
	# Apply directly so the change ALWAYS takes effect, then record the undo/redo
	# without re-executing (commit_action(false)). Relying on commit_action(true) to
	# apply is fragile: after switching animations/panels the EditorUndoRedoManager
	# history for an inline sub-resource can go stale and the commit silently no-ops.
	var old: Array = dock.track.notifies.duplicate()
	dock.track.notifies = arr
	dock.track.emit_changed()
	if dock.undo_redo:
		dock.undo_redo.create_action(action)
		dock.undo_redo.add_do_property(dock.track, "notifies", arr)
		dock.undo_redo.add_undo_property(dock.track, "notifies", old)
		dock.undo_redo.commit_action(false)

func _del_hit(n, pos: Vector2) -> bool:
	# geometry of the × on a hovered notify flag, computed fresh (no dependency on the last _draw)
	var font := get_theme_default_font()
	var fsize := 13
	var tw := font.get_string_size(_elide(n.notify_name, font, fsize), HORIZONTAL_ALIGNMENT_LEFT, -1, fsize).x
	var rect_end_x := x_from_time(n.time) + 5.0 + tw + 18.0 + 18.0
	var cy := _row_center(_channel_row_index(n.channel))
	return Rect2(rect_end_x - 19.0, cy - 8.0, 16.0, 16.0).has_point(pos)

func _gui_input(event: InputEvent) -> void:
	# right-click a notify marker -> delete (state-independent, always works)
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
		var rhit = marker_at_pos(event.position.x, event.position.y)
		if rhit != null and rhit.kind == "notify":
			delete_notify(rhit.ref)
			accept_event()
		return
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			# hovered-notify × delete button takes priority over select/drag
			if _hovered != null and _hovered.kind == "notify" and _del_hit(_hovered.ref, event.position):
				delete_notify(_hovered.ref)
				accept_event(); return
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
			set_playhead(t2)
			if dock: dock._on_scrub()
			accept_event()
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
			if dock: dock._on_scrub()
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
	# lane background + per-row tint/separators
	draw_rect(Rect2(Vector2.ZERO, size), C_LANE, true)
	var tks := tracks()
	for i in tks.size():
		var top := _row_top(i)
		if tks[i].kind == "events":
			draw_rect(Rect2(0, top, size.x, LANE_H), C_LANE_ALT, true)
		draw_line(Vector2(0, top), Vector2(size.x, top), C_ROWLINE, 1.0)
	# ruler baseline + frame ticks + quarter labels
	draw_line(Vector2(left, RULER_H), Vector2(left + w, RULER_H), C_RULER, 1.0)
	if snap_enabled and fps > 0.0:
		var frames := int(dur * fps)
		for f in range(frames + 1):
			var fx := x_from_time(f / fps)
			draw_line(Vector2(fx, RULER_H - 4), Vector2(fx, RULER_H), C_TICK, 1.0)
	for q in range(5):
		var qx := left + w * q / 4.0
		draw_string(font, Vector2(qx + 2, 15), "%.2f" % (dur * q / 4.0), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, C_QLABEL)
	# markers on top of the row backgrounds
	for i in tks.size():
		var tk: Dictionary = tks[i]
		if tk.kind == "events":
			for e in _events:
				_draw_flag(x_from_time(e.get("time", 0.0)), _row_center(i), String(e.get("name", "event")), C_EVENT, false, font, fsize, false)
		else:
			for n in _notifies_in_channel(tk.channel):
				var sel: bool = (dock._selected_notify == n)
				var del_hover: bool = (_hovered != null and _hovered.kind == "notify" and _hovered.ref == n)
				_draw_flag(x_from_time(n.time), _row_center(i), n.notify_name, C_NOTIFY_SEL if sel else C_NOTIFY, sel, font, fsize, del_hover)
	# playhead with a cap
	var px := x_from_time(playhead_time)
	draw_line(Vector2(px, RULER_H - 8), Vector2(px, size.y), C_PLAYHEAD, 2.0)
	draw_colored_polygon(PackedVector2Array([Vector2(px - 5, RULER_H - 9), Vector2(px + 5, RULER_H - 9), Vector2(px, RULER_H - 2)]), C_PLAYHEAD)
	if _hovered != null:
		_draw_tooltip(font)

func _draw_flag(cx: float, cy: float, label: String, col: Color, sel: bool, font: Font, fsize: int, del_hover: bool) -> void:
	var txt := _elide(label, font, fsize)
	var tw := font.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize).x
	var bw := tw + 18.0 + (18.0 if del_hover else 0.0)   # room for the × on hover
	var half := FLAG_H * 0.5
	var rect := Rect2(cx + 5, cy - half, bw, FLAG_H)
	# pointer nub at the exact time
	draw_colored_polygon(PackedVector2Array([Vector2(cx, cy), Vector2(cx + 7, cy - 5), Vector2(cx + 7, cy + 5)]), col.darkened(0.08))
	_flag_sb.bg_color = col
	if sel:
		_flag_sb.set_border_width_all(2)
		_flag_sb.border_color = Color(1, 1, 1, 0.9)
	else:
		_flag_sb.set_border_width_all(0)
	draw_style_box(_flag_sb, rect)
	var ink := Color(0.11, 0.09, 0.04) if col.get_luminance() > 0.5 else Color(1, 1, 1, 0.95)
	draw_string(font, Vector2(cx + 14, cy + fsize * 0.34), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize, ink)
	if del_hover:
		var dcx := rect.end.x - 11.0
		draw_line(Vector2(dcx - 3.5, cy - 3.5), Vector2(dcx + 3.5, cy + 3.5), ink, 1.6)
		draw_line(Vector2(dcx - 3.5, cy + 3.5), Vector2(dcx + 3.5, cy - 3.5), ink, 1.6)

func _draw_tooltip(font: Font) -> void:
	var ref = _hovered.ref
	var is_ev: bool = _hovered.kind == "event"
	var t: float = ref.get("time", 0.0) if is_ev else ref.time
	var nm: String = String(ref.get("name", "event")) if is_ev else ref.notify_name
	var chip: String = "event" if is_ev else String(ref.channel)
	var chip_col: Color = C_EVENT if is_ev else C_NOTIFY
	var rows: Array = []
	rows.append(["time", "%.3f s · f%d" % [t, int(round(t * fps))]])
	if not is_ev:
		var pk: Array = ref.payload.keys()
		for k in pk:
			rows.append([str(k), str(ref.payload[k])])
	var title_fs := 13
	var row_fs := 12
	var nmw := font.get_string_size(nm, HORIZONTAL_ALIGNMENT_LEFT, -1, title_fs).x
	var chipw := font.get_string_size(chip, HORIZONTAL_ALIGNMENT_LEFT, -1, 10).x + 12.0
	var lblw := 0.0
	var valw := 0.0
	for r in rows:
		lblw = max(lblw, font.get_string_size(r[0], HORIZONTAL_ALIGNMENT_LEFT, -1, row_fs).x)
		valw = max(valw, font.get_string_size(r[1], HORIZONTAL_ALIGNMENT_LEFT, -1, row_fs).x)
	var bw: float = max(nmw + chipw + 24.0, lblw + valw + 34.0)
	var bh: float = 10.0 + title_fs + 8.0 + rows.size() * (row_fs + 5) + 6.0
	var bx: float = clamp(x_from_time(t) + 10.0, 2.0, max(2.0, size.x - bw - 2.0))
	var by: float = max(2.0, _row_center(_hovered.row) - bh - 12.0)
	draw_style_box(_tip_sb, Rect2(bx, by, bw, bh))
	draw_string(font, Vector2(bx + 11, by + 10 + title_fs * 0.8), nm, HORIZONTAL_ALIGNMENT_LEFT, -1, title_fs, Color(1, 1, 1, 0.96))
	_chip_sb.bg_color = Color(chip_col.r, chip_col.g, chip_col.b, 0.22)
	draw_style_box(_chip_sb, Rect2(bx + 11 + nmw + 8, by + 9, chipw, 16))
	draw_string(font, Vector2(bx + 11 + nmw + 14, by + 20), chip, HORIZONTAL_ALIGNMENT_LEFT, -1, 10, chip_col)
	var yy := by + 10.0 + title_fs + 12.0
	for r in rows:
		draw_string(font, Vector2(bx + 11, yy), r[0], HORIZONTAL_ALIGNMENT_LEFT, -1, row_fs, Color(0.60, 0.63, 0.72))
		draw_string(font, Vector2(bx + 11 + lblw + 14, yy), r[1], HORIZONTAL_ALIGNMENT_LEFT, -1, row_fs, Color(0.90, 0.92, 0.97))
		yy += row_fs + 5
