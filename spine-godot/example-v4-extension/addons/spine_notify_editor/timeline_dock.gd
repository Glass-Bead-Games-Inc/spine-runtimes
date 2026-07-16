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
var _strip: HBoxContainer
var _name_edit: LineEdit
var _channel_edit: LineEdit
var _selected_notify

var playing: bool = false
var loop_enabled: bool = true
var _play_btn: Button
var _loop_btn: CheckButton
var _snap_btn: CheckButton
var _time_label: Label
var _extra_channels: Array = []
var _new_channel_edit: LineEdit
var _add_channel_btn: Button

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
	_new_channel_edit = LineEdit.new()
	_new_channel_edit.placeholder_text = "new channel…"
	_new_channel_edit.custom_minimum_size.x = 100
	_new_channel_edit.text_submitted.connect(func(_s): _add_channel())
	_toolbar.add_child(_new_channel_edit)
	_add_channel_btn = Button.new(); _add_channel_btn.text = "+ Channel"
	_add_channel_btn.pressed.connect(_add_channel)
	_toolbar.add_child(_add_channel_btn)
	set_process(true)

	var ViewScript := load("res://addons/spine_notify_editor/timeline_view.gd")
	_view = ViewScript.new()
	_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_view.setup(self)
	add_child(_view)

	_strip = HBoxContainer.new()
	_name_edit = LineEdit.new(); _name_edit.placeholder_text = "name"
	_name_edit.text_submitted.connect(func(_s): _apply_strip())
	_channel_edit = LineEdit.new(); _channel_edit.placeholder_text = "channel"
	_channel_edit.text_submitted.connect(func(_s): _apply_strip())
	_name_edit.focus_exited.connect(_apply_strip)
	_channel_edit.focus_exited.connect(_apply_strip)
	var insp := Button.new(); insp.text = "Edit in Inspector"
	insp.pressed.connect(func(): if _selected_notify and editor_interface: editor_interface.edit_resource(_selected_notify))
	_strip.add_child(Label.new()); _strip.get_child(0).text = "notify:"
	_strip.add_child(_name_edit); _strip.add_child(_channel_edit); _strip.add_child(insp)
	add_child(_strip)
	_strip.visible = false

	_set_active(false)

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

func _add_channel() -> void:
	var cname: String = _new_channel_edit.text.strip_edges()
	if cname == "": return
	if not (cname in _extra_channels): _extra_channels.append(cname)
	_new_channel_edit.text = ""
	if _view:
		_view.refresh()
		_view.queue_redraw()

func edit_notify(n) -> void:
	_selected_notify = n
	_strip.visible = n != null
	if n != null:
		_name_edit.text = n.notify_name
		_channel_edit.text = n.channel
	if _view: _view.queue_redraw()

func _apply_strip() -> void:
	if _selected_notify == null: return
	var new_name: String = _name_edit.text
	var new_channel: String = _channel_edit.text
	if new_name == _selected_notify.notify_name and new_channel == _selected_notify.channel:
		return
	if undo_redo:
		undo_redo.create_action("Edit Notify")
		undo_redo.add_do_property(_selected_notify, "notify_name", new_name)
		undo_redo.add_do_property(_selected_notify, "channel", new_channel)
		undo_redo.add_undo_property(_selected_notify, "notify_name", _selected_notify.notify_name)
		undo_redo.add_undo_property(_selected_notify, "channel", _selected_notify.channel)
		undo_redo.commit_action()
	else:
		_selected_notify.notify_name = new_name
		_selected_notify.channel = new_channel
	if track: track.emit_changed()
	if _view: _view.queue_redraw()

func _prune_selection() -> void:
	if _selected_notify != null and track != null and not (_selected_notify in track.notifies):
		edit_notify(null)

func _process(delta: float) -> void:
	if _view == null: return
	if playing and _view.visible:
		var dur: float = _view.duration()
		var t: float = _view.playhead_time + delta
		if t >= dur:
			t = 0.0 if loop_enabled else dur
			if not loop_enabled: playing = false; _play_btn.text = "Play"
		_view.set_playhead(t)
	if _view.is_visible_in_tree():
		_prune_selection()
		_view.queue_redraw()
	if _time_label and _view:
		_time_label.text = "  %.2f / %.2f" % [_view.playhead_time, _view.duration()]

func _set_active(active: bool) -> void:
	if _placeholder: _placeholder.visible = not active
	if _toolbar: _toolbar.visible = active
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
		_view.set_playhead(0.0)
		_view.refresh()

func select_animation(anim: String) -> void:
	for i in _anim_dropdown.item_count:
		if _anim_dropdown.get_item_text(i) == anim:
			_anim_dropdown.select(i)
			break
	_on_animation_changed()
