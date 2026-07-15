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
