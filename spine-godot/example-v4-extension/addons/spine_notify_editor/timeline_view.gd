@tool
extends Control

const RULER_H := 22.0
const EVENTS_H := 16.0
const PAD := 6.0
const NOTIFY_Y := RULER_H + EVENTS_H + 14.0
const EVENTS_Y := RULER_H + 10.0
const HIT_PX := 8.0

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
	# notify lane (diamonds)
	for n in notifies_for_current():
		var nx := x_from_time(n.time)
		var sel: bool = (dock._selected_notify == n)
		var col := Color(1.0,0.85,0.2) if sel else Color(0.9,0.5,0.2)
		var pts := PackedVector2Array([Vector2(nx,NOTIFY_Y-6),Vector2(nx+6,NOTIFY_Y),Vector2(nx,NOTIFY_Y+6),Vector2(nx-6,NOTIFY_Y)])
		draw_colored_polygon(pts, col)
	# playhead
	var px := x_from_time(playhead_time)
	draw_line(Vector2(px, 0), Vector2(px, size.y), Color(1,0.8,0.2), 1.0)
