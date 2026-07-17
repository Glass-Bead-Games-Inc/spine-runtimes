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
var _timeline_row: HBoxContainer
var _headers: VBoxContainer
var _headers_sig: String = ""
var _strip: VBoxContainer
var _name_edit: LineEdit
var _channel_chip: Label
var _selected_notify
var _payload_box: VBoxContainer
var _payload_rows: Array = []          # each: {row, key, type, value}
var _add_key_btn: Button
const _PAYLOAD_TYPES := ["String", "int", "float", "bool"]

var playing: bool = false
var loop_enabled: bool = true
var play_dir: int = 1
var _tp_bw_from: Button
var _tp_bw_end: Button
var _tp_stop: Button
var _tp_start: Button
var _tp_play: Button
var _active_tp: Button
var _tp_group: HBoxContainer
var _loop_btn: Button
var _snap_btn: Button
var _time_label: Label
var _extra_channels: Array = []

# palette (matches the approved mock)
const D_PANEL2 := Color(0.173, 0.184, 0.216)   # #2c2f37
const D_HAIR := Color(0.078, 0.082, 0.102)     # #14151a
const D_INK := Color(0.827, 0.847, 0.878)
const D_INKDIM := Color(0.569, 0.596, 0.647)
const D_ACCENT := Color(0.353, 0.624, 0.831)   # #5a9fd4
const D_NOTIFY := Color(0.878, 0.600, 0.243)   # #e0993e
const D_EVENT := Color(0.286, 0.718, 0.686)    # #49b7b0
const D_INKON := Color(0.06, 0.09, 0.12)       # ink on accent

func _flat(bg: Color, radius: int, border := 0, bcol := Color(0, 0, 0, 0)) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.set_corner_radius_all(radius)
	s.set_content_margin_all(0)
	if border > 0:
		s.set_border_width_all(border)
		s.border_color = bcol
	return s

func _pad(sb: StyleBoxFlat, h: int, v: int) -> StyleBoxFlat:
	sb.content_margin_left = h
	sb.content_margin_right = h
	sb.content_margin_top = v
	sb.content_margin_bottom = v
	return sb

func _style_icon_btn(b: Button) -> void:
	b.add_theme_stylebox_override("normal", _flat(Color(0, 0, 0, 0), 4))
	b.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.09), 4))
	b.add_theme_stylebox_override("pressed", _flat(D_ACCENT, 4))
	b.add_theme_stylebox_override("focus", _flat(Color(0, 0, 0, 0), 4))
	b.add_theme_color_override("font_color", D_INK)
	b.custom_minimum_size = Vector2(30, 26)

func _style_toggle(b: Button) -> void:
	b.toggle_mode = true
	b.add_theme_stylebox_override("normal", _pad(_flat(Color(0, 0, 0, 0.15), 5, 1, D_HAIR), 10, 5))
	b.add_theme_stylebox_override("hover", _pad(_flat(Color(1, 1, 1, 0.06), 5, 1, D_HAIR), 10, 5))
	b.add_theme_stylebox_override("pressed", _pad(_flat(D_ACCENT, 5), 10, 5))
	b.add_theme_stylebox_override("hover_pressed", _pad(_flat(D_ACCENT, 5), 10, 5))
	b.add_theme_stylebox_override("focus", _flat(Color(0, 0, 0, 0), 5))
	b.add_theme_color_override("font_color", D_INKDIM)
	b.add_theme_color_override("font_hover_color", D_INK)
	b.add_theme_color_override("font_pressed_color", D_INKON)
	b.add_theme_color_override("font_hover_pressed_color", D_INKON)
	b.add_theme_color_override("icon_normal_color", D_INKDIM)
	b.add_theme_color_override("icon_pressed_color", D_INKON)

func _style_btn(b: Button) -> void:
	b.add_theme_stylebox_override("normal", _pad(_flat(Color(0, 0, 0, 0.18), 4, 1, D_HAIR), 10, 4))
	b.add_theme_stylebox_override("hover", _pad(_flat(Color(1, 1, 1, 0.08), 4, 1, D_HAIR), 10, 4))
	b.add_theme_stylebox_override("pressed", _pad(_flat(Color(1, 1, 1, 0.14), 4, 1, D_HAIR), 10, 4))
	b.add_theme_stylebox_override("focus", _flat(Color(0, 0, 0, 0), 4))
	b.add_theme_color_override("font_color", D_INK)

func setup(p_undo_redo, p_editor_interface) -> void:
	undo_redo = p_undo_redo
	editor_interface = p_editor_interface
	_build_ui()

func _build_ui() -> void:
	custom_minimum_size = Vector2(0, 240)
	_placeholder = Label.new()
	_placeholder.text = "Select a SpineAnimationPlayer to edit its notifies."
	add_child(_placeholder)

	_toolbar = HBoxContainer.new()
	_toolbar.add_theme_constant_override("separation", 8)
	_anim_dropdown = OptionButton.new()
	_anim_dropdown.custom_minimum_size = Vector2(120, 0)
	_anim_dropdown.item_selected.connect(func(_i): _on_animation_changed())
	_toolbar.add_child(_anim_dropdown)
	add_child(_toolbar)

	var tpc := PanelContainer.new()
	tpc.add_theme_stylebox_override("panel", _pad(_flat(Color(0, 0, 0, 0.15), 5, 1, D_HAIR), 2, 2))
	_tp_group = HBoxContainer.new()
	_tp_group.add_theme_constant_override("separation", 2)
	tpc.add_child(_tp_group)
	_toolbar.add_child(tpc)
	_tp_bw_from = _make_tp("PlayBackwards", "◀", "Play backwards from current pos.", _play_bw_from_current)
	_tp_bw_end  = _make_tp("PlayStartBackwards", "◀◀", "Play backwards from end.", _play_bw_from_end)
	_tp_stop    = _make_tp("Stop", "■", "Pause/stop.", _stop)
	_tp_start   = _make_tp("PlayStart", "▶|", "Play from start.", _play_from_start)
	_tp_play    = _make_tp("Play", "▶", "Play from current pos.", _play_from_current)

	_loop_btn = Button.new(); _loop_btn.text = " Loop"
	var loop_ic := _editor_icon("Loop")
	if loop_ic: _loop_btn.icon = loop_ic
	_style_toggle(_loop_btn)
	_loop_btn.button_pressed = true          # after toggle_mode is set, so it sticks
	_loop_btn.toggled.connect(func(on): loop_enabled = on)
	_toolbar.add_child(_loop_btn)
	_snap_btn = Button.new(); _snap_btn.text = " Snap"
	var snap_ic := _editor_icon("SnapGrid")
	if snap_ic: _snap_btn.icon = snap_ic
	_style_toggle(_snap_btn)
	_snap_btn.button_pressed = true
	_snap_btn.toggled.connect(func(on): if _view: _view.snap_enabled = on; _view.queue_redraw())
	_toolbar.add_child(_snap_btn)
	_time_label = Label.new()
	_time_label.add_theme_color_override("font_color", D_INKDIM)
	_toolbar.add_child(_time_label)
	set_process(true)

	_timeline_row = HBoxContainer.new()
	_timeline_row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_timeline_row.add_theme_constant_override("separation", 0)
	var hpanel := PanelContainer.new()
	hpanel.custom_minimum_size = Vector2(176, 0)
	var hsb := _flat(D_PANEL2, 0)
	hsb.border_width_right = 1
	hsb.border_color = D_HAIR
	hsb.content_margin_left = 10
	hsb.content_margin_right = 8
	hpanel.add_theme_stylebox_override("panel", hsb)
	_headers = VBoxContainer.new()
	_headers.add_theme_constant_override("separation", 0)
	_headers.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hpanel.add_child(_headers)
	_timeline_row.add_child(hpanel)
	var ViewScript := load("res://addons/spine_notify_editor/timeline_view.gd")
	_view = ViewScript.new()
	_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_view.setup(self)
	_timeline_row.add_child(_view)
	add_child(_timeline_row)
	_rebuild_headers()

	_strip = VBoxContainer.new()
	_strip.add_theme_constant_override("separation", 6)
	var row1 := HBoxContainer.new()
	row1.add_theme_constant_override("separation", 8)
	var nlbl := Label.new(); nlbl.text = "notify"; nlbl.add_theme_color_override("font_color", D_INKDIM)
	row1.add_child(nlbl)
	_name_edit = LineEdit.new(); _name_edit.placeholder_text = "name"; _name_edit.custom_minimum_size = Vector2(180, 0)
	_name_edit.text_submitted.connect(func(_s): _apply_strip())
	_name_edit.focus_exited.connect(_apply_strip)
	row1.add_child(_name_edit)
	_channel_chip = Label.new()
	_channel_chip.add_theme_font_size_override("font_size", 12)
	_channel_chip.add_theme_color_override("font_color", D_NOTIFY)
	var csb := _flat(Color(D_NOTIFY.r, D_NOTIFY.g, D_NOTIFY.b, 0.15), 9, 1, Color(D_NOTIFY.r, D_NOTIFY.g, D_NOTIFY.b, 0.4))
	_channel_chip.add_theme_stylebox_override("normal", _pad(csb, 10, 3))
	row1.add_child(_channel_chip)
	var spacer := Control.new(); spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row1.add_child(spacer)
	var insp := Button.new(); insp.text = "Edit in Inspector"
	insp.pressed.connect(func(): if _selected_notify and editor_interface: editor_interface.edit_resource(_selected_notify))
	_style_btn(insp)
	row1.add_child(insp)
	var trash := Button.new(); trash.text = "🗑 Delete"; trash.tooltip_text = "Delete this notify"
	trash.pressed.connect(func(): if _selected_notify and _view: _view.delete_notify(_selected_notify))
	_style_btn(trash)
	trash.add_theme_color_override("font_color", Color(0.93, 0.55, 0.55))
	row1.add_child(trash)
	_strip.add_child(row1)
	_payload_box = VBoxContainer.new()
	_payload_box.add_theme_constant_override("separation", 4)
	_strip.add_child(_payload_box)
	_add_key_btn = Button.new(); _add_key_btn.text = "+ Add key"
	_add_key_btn.pressed.connect(_on_add_key)
	_style_btn(_add_key_btn)
	_add_key_btn.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_strip.add_child(_add_key_btn)
	add_child(_strip)
	_strip.visible = false

	_set_active(false)

func _editor_icon(nm: String) -> Texture2D:
	if editor_interface:
		var base: Control = editor_interface.get_base_control()
		if base and base.has_theme_icon(nm, "EditorIcons"):
			return base.get_theme_icon(nm, "EditorIcons")
	return null

func _make_tp(icon_name: String, fallback: String, tip: String, fn: Callable) -> Button:
	var b := Button.new()
	var ic := _editor_icon(icon_name)
	if ic != null: b.icon = ic
	else: b.text = fallback
	b.tooltip_text = tip
	b.pressed.connect(fn)
	_style_icon_btn(b)
	_tp_group.add_child(b)
	return b

func _set_active_tp(b) -> void:
	if _active_tp and is_instance_valid(_active_tp):
		_active_tp.add_theme_stylebox_override("normal", _flat(Color(0, 0, 0, 0), 4))
		_active_tp.add_theme_color_override("icon_normal_color", D_INK)
		_active_tp.add_theme_color_override("font_color", D_INK)
	_active_tp = b
	if b:
		b.add_theme_stylebox_override("normal", _flat(D_ACCENT, 4))
		b.add_theme_color_override("icon_normal_color", D_INKON)
		b.add_theme_color_override("font_color", D_INKON)

func _play_from_start() -> void:
	if _view: _view.set_playhead(0.0)
	play_dir = 1; playing = true
	_set_active_tp(_tp_start)

func _play_from_current() -> void:
	play_dir = 1; playing = true
	_set_active_tp(_tp_play)

func _play_bw_from_current() -> void:
	play_dir = -1; playing = true
	_set_active_tp(_tp_bw_from)

func _play_bw_from_end() -> void:
	if _view: _view.set_playhead(_view.duration())
	play_dir = -1; playing = true
	_set_active_tp(_tp_bw_end)

func _stop() -> void:
	if not playing and _view and _view.playhead_time > 0.0:
		_view.set_playhead(0.0)     # second press when stopped -> rewind
	playing = false
	_set_active_tp(null)

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
	addbtn.custom_minimum_size = Vector2(0, 24)
	addbtn.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	addbtn.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_style_btn(addbtn)
	addbtn.add_theme_color_override("font_color", D_INKDIM)
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
		row.add_theme_constant_override("separation", 9)
		var sw := ColorRect.new(); sw.custom_minimum_size = Vector2(10, 10)
		sw.color = D_EVENT if tk.kind == "events" else D_NOTIFY
		sw.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		row.add_child(sw)
		var is_events: bool = tk.kind == "events"
		var cname: String = "Events" if is_events else String(tk.channel)
		var renameable: bool = (not is_events) and cname != "default"
		if renameable:
			var nb := Button.new(); nb.flat = true; nb.text = cname
			nb.add_theme_font_size_override("font_size", 13)
			nb.add_theme_color_override("font_color", D_INK)
			nb.add_theme_color_override("font_hover_color", Color(1, 1, 1))
			nb.tooltip_text = "Double-click to rename"
			nb.size_flags_vertical = Control.SIZE_SHRINK_CENTER
			nb.gui_input.connect(func(ev):
				if ev is InputEventMouseButton and ev.double_click and ev.pressed:
					_begin_rename(row, nb, cname))
			row.add_child(nb)
		else:
			var lbl := Label.new(); lbl.text = cname
			lbl.add_theme_font_size_override("font_size", 13)
			lbl.size_flags_vertical = Control.SIZE_SHRINK_CENTER
			lbl.add_theme_color_override("font_color", D_EVENT if is_events else D_INK)
			row.add_child(lbl)
		if is_events:
			var lk := Label.new(); lk.text = "🔒"; lk.add_theme_font_size_override("font_size", 10)
			lk.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			lk.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
			lk.size_flags_vertical = Control.SIZE_SHRINK_CENTER
			lk.add_theme_color_override("font_color", D_INKDIM)
			row.add_child(lk)
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

func _header_row_count() -> int:
	# header rows excluding the top ruler-height cell
	return _headers.get_child_count() - 1 if _headers else 0

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
	if _selected_notify != null and _selected_notify in targets:
		edit_notify(_selected_notify)

func edit_notify(n) -> void:
	_selected_notify = n
	_strip.visible = n != null
	_clear_payload_rows()
	if n != null:
		_name_edit.text = n.notify_name
		_channel_chip.text = "  " + n.channel
		for key in n.payload.keys():
			var val = n.payload[key]
			_add_payload_row(str(key), _type_index_of(val), str(val))
	if _view: _view.queue_redraw()

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

func _add_payload_row(key: String, type_idx: int, value_text: String) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	var k := LineEdit.new(); k.placeholder_text = "key"; k.text = key; k.custom_minimum_size.x = 120
	var ty := OptionButton.new(); ty.custom_minimum_size.x = 96
	for tn in _PAYLOAD_TYPES: ty.add_item(tn)
	ty.select(clampi(type_idx, 0, _PAYLOAD_TYPES.size() - 1))
	var v := LineEdit.new(); v.placeholder_text = "value"; v.text = value_text; v.custom_minimum_size.x = 140
	var rm := Button.new(); rm.text = "−"; rm.custom_minimum_size = Vector2(26, 0); _style_btn(rm)
	rm.add_theme_color_override("font_color", D_INKDIM)
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

func _strip_has_focus() -> bool:
	if _strip == null: return false
	var vp := get_viewport()
	if vp == null: return false
	var f = vp.gui_get_focus_owner()
	return f != null and _strip.is_ancestor_of(f)

func _notify_signature(n) -> String:
	if n == null or not is_instance_valid(n): return ""
	return "%s|%s|%d" % [n.notify_name, n.channel, n.payload.hash()]

func _displayed_signature() -> String:
	if _selected_notify == null: return ""
	var d := {}
	for e in _payload_rows:
		if not is_instance_valid(e.key): continue
		var k: String = e.key.text.strip_edges()
		if k == "": continue
		d[k] = _parse_payload_value(e.type.selected, e.value.text)
	return "%s|%s|%d" % [_name_edit.text, _selected_notify.channel, d.hash()]

func _sync_strip_if_stale() -> void:
	if _selected_notify == null or not is_instance_valid(_selected_notify): return
	if _strip_has_focus(): return
	if _notify_signature(_selected_notify) != _displayed_signature():
		edit_notify(_selected_notify)

func _prune_selection() -> void:
	if _selected_notify != null and track != null and not (_selected_notify in track.notifies):
		edit_notify(null)

func _process(delta: float) -> void:
	if _view == null: return
	if playing and _view.is_visible_in_tree():
		var dur: float = _view.duration()
		var t: float = _view.playhead_time + play_dir * delta
		if play_dir > 0 and t >= dur:
			if loop_enabled: t = 0.0
			else: t = dur; playing = false; _set_active_tp(null)
		elif play_dir < 0 and t <= 0.0:
			if loop_enabled: t = dur
			else: t = 0.0; playing = false; _set_active_tp(null)
		_view.set_playhead(t)
	if _view.is_visible_in_tree():
		_prune_selection()
		_sync_strip_if_stale()
		_view.queue_redraw()
		var sig: String = ",".join(_view.channels())
		if sig != _headers_sig:
			_rebuild_headers()
	if _time_label and _view:
		var ph: float = _view.playhead_time
		var du: float = _view.duration()
		_time_label.text = "  %.2f s · f%d / %d · %.2f s" % [ph, int(round(ph * _view.fps)), int(_view.fps), du]

func _set_active(active: bool) -> void:
	if _placeholder: _placeholder.visible = not active
	if _toolbar: _toolbar.visible = active
	if _timeline_row: _timeline_row.visible = active
	if _view: _view.visible = active

func bind(p_player) -> void:
	var new_player = p_player if (p_player != null and is_instance_valid(p_player)) else null
	if new_player != player:
		_extra_channels = []
	player = new_player
	sprite = null
	track = null
	animation_names = PackedStringArray()
	edit_notify(null)
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
	if _view:
		_view.refresh()
		playing = false
		_set_active_tp(null)
		_view.set_playhead(0.0)

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
	edit_notify(null)
	if _view:
		playing = false
		_set_active_tp(null)
		_view.set_playhead(0.0)
		_view.refresh()

func select_animation(anim: String) -> void:
	for i in _anim_dropdown.item_count:
		if _anim_dropdown.get_item_text(i) == anim:
			_anim_dropdown.select(i)
			break
	_on_animation_changed()
