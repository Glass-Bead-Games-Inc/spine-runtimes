extends Node3D

# Demonstrates driving a SpineSprite3D via an AnimationPlayer that is
# auto-created at runtime by SpineAnimationTrack (module-only feature).
# The AnimationPlayer lives under $Spineboy/AnimationTrack and is populated
# with one Animation per skeleton animation by SpineAnimationTrack._ready().

func _ready() -> void:
	var track: SpineAnimationTrack = $Spineboy/AnimationTrack

	# SpineAnimationTrack.setup_animation_player() runs in its own _ready(),
	# which fires before this Node3D's _ready() (children ready before parents).
	# So the AnimationPlayer child should already exist here.
	var anim_player: AnimationPlayer = null
	for child in track.get_children():
		if child is AnimationPlayer:
			anim_player = child
			break

	if anim_player == null:
		push_warning("AnimationPlayer not found under AnimationTrack – SpineAnimationTrack may not have initialised yet.")
		return

	# Pick "walk_looped" if it exists, otherwise fall back to the first
	# animation that isn't a housekeeping entry.
	var anim_name := ""
	if anim_player.has_animation("walk_looped"):
		anim_name = "walk_looped"
	else:
		for name in anim_player.get_animation_list():
			if name != "RESET" and name != "-- Empty --":
				anim_name = name
				break

	if anim_name.is_empty():
		push_warning("No playable animation found in AnimationPlayer.")
		return

	# Play the animation – SpineAnimationTrack will pick up the keyed
	# animation_name/loop values on the next before_animation_state_update signal.
	anim_player.play(anim_name)
	print("AnimationPlayer3D: playing '", anim_name, "'")
