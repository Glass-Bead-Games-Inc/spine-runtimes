extends Node3D

func _ready() -> void:
	for child in get_children():
		if child is SpineSprite3D:
			child.get_animation_state().set_animation("walk", true, 0)
