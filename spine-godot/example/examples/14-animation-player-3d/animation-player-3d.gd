extends Node3D

# Faithful 3D port of example 08 (AnimationPlayer cutscene).
# The top-level AnimationPlayer plays a "cutscene" Animation that drives
# three per-track AnimationPlayers auto-created at runtime by SpineAnimationTrack.
# After the cutscene ends, arrow-key input moves Spineboy.

@onready var player: AnimationPlayer = $AnimationPlayer
@onready var spineboy: SpineSprite3D = $Spineboy

var speed: float = 8.0
var velocity_x: float = 0.0

func _ready() -> void:
	player.play("cutscene")

func _process(delta: float) -> void:
	if not player.is_playing():
		if Input.is_action_just_released("ui_left"):
			spineboy.get_animation_state().set_animation("idle", true, 0)
			velocity_x = 0.0

		if Input.is_action_just_released("ui_right"):
			spineboy.get_animation_state().set_animation("idle", true, 0)
			velocity_x = 0.0

		if Input.is_action_just_pressed("ui_right"):
			spineboy.get_animation_state().set_animation("run", true, 0)
			spineboy.get_skeleton().set_scale_x(1)
			velocity_x = 1.0

		if Input.is_action_just_pressed("ui_left"):
			spineboy.get_animation_state().set_animation("run", true, 0)
			spineboy.get_skeleton().set_scale_x(-1)
			velocity_x = -1.0

		spineboy.position.x += velocity_x * speed * delta
