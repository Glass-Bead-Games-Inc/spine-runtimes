extends SceneTree
## Folder-qualified clips must survive Godot registration without renaming Spine animations.

const Fixture = preload("animation-player-fixture.gd")
# Literal expectations also catch lossy separators, escaped aliases, generated
# loop suffixes and clips that would overwrite the two special animations.
const NAMES = {
	"walk": "walk",
	"run": "run",
	"move/run": "move%2Frun",
	"move/deep/run": "move%2Fdeep%2Frun",
	"move_run": "move_run",
	"move%2Frun": "move%252Frun",
	"move:run": "move%3Arun",
	"move,run": "move%2Crun",
	"move[run]": "move%5Brun]",
	"战斗/待机": "战斗%2F待机",
	"walk_looped": "walk%5Flooped",
	"walk_looped_looped": "walk_looped%5Flooped",
	"walk%5Flooped": "walk%255Flooped",
	"unpaired_looped": "unpaired_looped",
	"RESET": "%52ESET",
	"RESET_looped": "RESET%5Flooped",
	"%52ESET": "%2552ESET",
	"-- Empty --": "%2D- Empty --",
	"-- Empty --_looped": "-- Empty --%5Flooped",
	"%2D- Empty --": "%252D- Empty --",
	"folder/run": "folder%2Frun",
	"folder/run_looped": "folder%2Frun%5Flooped",
	"folder/run_looped_looped": "folder%2Frun_looped%5Flooped",
	"pct%name": "pct%25name",
}

class Notifier extends Node:
	var calls: Array[String] = []

	func notify(payload: String) -> void:
		calls.append(payload)

var failed := false
var checks := 0

func _init():
	call_deferred("_run")

func _check(condition: bool, message: String):
	checks += 1
	if not condition:
		failed = true
		push_error(message)

func _check_clips(sprite: Node, player: AnimationPlayer):
	_check(player.get_animation_list().size() == 50, "all 24 Spine clips retain both variants and RESET/Empty on " + sprite.name)
	var identities := {}
	for original: String in NAMES:
		for looped: bool in [false, true]:
			var key: String = NAMES[original] + ("_looped" if looped else "")
			_check(player.has_animation(key), "generated key exists: " + key + " on " + sprite.name)
			if not player.has_animation(key):
				continue
			var clip := player.get_animation(key)
			var name_value: String = clip.track_get_key_value(0, 0)
			var loop_value: bool = clip.track_get_key_value(1, 0)
			_check(name_value == original and loop_value == looped, "key targets original Spine clip and loop mode: " + key)
			var identity := name_value + ("|looped" if loop_value else "|once")
			_check(not identities.has(identity), "no generated clip silently overwrites another: " + key)
			identities[identity] = true
			player.play(key)
			player.advance(0)
			sprite.update_skeleton(0.25)
			var entry: SpineTrackEntry = sprite.get_animation_state().get_track(0)
			_check(entry != null and entry.get_animation().get_name() == original and entry.get_loop() == looped,
				"safe Godot key plays the original Spine animation: " + key)
	_check(identities.size() == 48, "every original/loop identity is playable on " + sprite.name)
	if player.has_animation("move%2Frun_looped"):
		player.play("move%2Frun_looped")
		player.advance(0)
		sprite.update_skeleton(0.25)
		sprite.update_skeleton(2)
		var entry: SpineTrackEntry = sprite.get_animation_state().get_track(0)
		_check(is_equal_approx(entry.get_animation_time(), 0.25), "folder-qualified clip loops in native state")
		_check(abs(sprite.get_skeleton().find_bone("root").get_applied_pose().get_rotation() - 11.25) < 0.001,
			"folder-qualified clip produces its animated native pose")
	player.play("RESET")
	player.advance(0)
	sprite.update_skeleton(0)
	_check(sprite.get_node("Track").animation_name.is_empty(), "RESET remains the generated reset rather than the Spine RESET clip")
	_check(player.get_animation("-- Empty --") == player.get_animation("RESET"), "Empty retains reset behavior")

func _run():
	var directory := "user://spine-animation-names-%d" % OS.get_process_id()
	var animations := {}
	for name: String in NAMES:
		if name not in ["walk", "run"]:
			animations[name] = {"bones": {"root": {"rotate": [{"value": 0}, {"time": 2, "value": 90}]}}}
	var data := Fixture.create_data(directory, animations)
	var scene := Node.new()
	root.add_child(scene)
	for three_d: bool in [false, true]:
		var sprite := Fixture.create_sprite(scene, data, three_d)
		var track: SpineAnimationTrack = sprite.get_node("Track")
		track.mix_duration = 0
		var player: AnimationPlayer = track.get_child(0)
		player.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
		_check_clips(sprite, player)
		if not player.has_animation("move%2Frun_looped") or not player.has_animation("%52ESET"):
			sprite.free()
			continue
		var before := player.get_animation_list()
		var notifier := Notifier.new()
		notifier.name = "Notifier"
		scene.add_child(notifier)
		var master := AnimationPlayer.new()
		scene.add_child(master)
		master.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
		master.callback_mode_method = AnimationMixer.ANIMATION_CALLBACK_MODE_METHOD_IMMEDIATE
		var timeline := Animation.new()
		timeline.length = 2
		timeline.loop_mode = Animation.LOOP_LINEAR
		var playback := timeline.add_track(Animation.TYPE_ANIMATION)
		timeline.track_set_path(playback, scene.get_path_to(player))
		timeline.animation_track_insert_key(playback, 0, "move%2Frun_looped")
		timeline.animation_track_insert_key(playback, 1, "%52ESET")
		var events := timeline.add_track(Animation.TYPE_METHOD)
		timeline.track_set_path(events, scene.get_path_to(notifier))
		timeline.track_insert_key(events, 0.5, {"method": &"notify", "args": ["step"]})
		timeline.track_insert_key(events, 1.5, {"method": &"notify", "args": ["finish"]})
		var library := AnimationLibrary.new()
		library.add_animation("cutscene", timeline)
		master.add_animation_library("", library)
		master.play("cutscene")
		master.advance(0)
		player.advance(0)
		sprite.update_skeleton(0.2)
		_check(sprite.get_animation_state().get_track(0).get_animation().get_name() == "move/run", "outer playback uses safe key and original folder clip")
		master.advance(0.75)
		_check(notifier.calls == ["step"], "Call Method event fires at its authored time with payload")
		master.advance(0.5)
		player.advance(0)
		sprite.update_skeleton(0.1)
		_check(sprite.get_animation_state().get_track(0).get_animation().get_name() == "RESET", "outer playback distinguishes Spine RESET from special reset")
		master.advance(0.4)
		_check(notifier.calls == ["step", "finish"], "second authored event fires once")
		master.advance(0.6)
		master.advance(0.3)
		_check(notifier.calls == ["step", "finish", "step"], "authored event repeats on the next outer timeline loop")
		master.stop()
		sprite.skeleton_data_res = null
		sprite.skeleton_data_res = data
		sprite.update_skeleton(0)
		_check(player.get_animation_list() == before, "resource regeneration preserves safe keys and completeness")
		_check(master.get_animation("cutscene") == timeline and timeline.track_get_key_value(playback, 0) == "move%2Frun_looped",
			"generated library rebuilding preserves project-owned playback/event tracks")
		notifier.calls.clear()
		master.play("cutscene")
		master.advance(0.75)
		player.advance(0)
		sprite.update_skeleton(0.1)
		_check(notifier.calls == ["step"] and sprite.get_animation_state().get_track(0).get_animation().get_name() == "move/run",
			"playback and project events work after regeneration")
		master.free()
		notifier.free()
		sprite.free()
	scene.free()
	Fixture.cleanup(directory)
	print("Spine AnimationPlayer naming regression ", "FAILED" if failed else "passed", " (", checks, " checks).")
	quit(1 if failed else 0)
