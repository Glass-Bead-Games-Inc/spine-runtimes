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
			player = n
			break
		if n is SpineSprite3D:
			for c in n.get_children():
				if c is SpineAnimationPlayer:
					player = c
					break
			if player != null:
				break
	_dock.bind(player)
