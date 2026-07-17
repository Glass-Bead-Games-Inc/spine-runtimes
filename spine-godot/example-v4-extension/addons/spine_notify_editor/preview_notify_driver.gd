@tool
extends RefCounted
# Preview-emission driver for the SpineAnimation dock.
#
# During FORWARD transport playback the dock feeds forward playhead advances here; we emit
# the bound SpineAnimationPlayer's REAL `notified` signal for every notify of the currently
# previewed clip crossed in (cursor, to], in ascending time order — exactly as runtime
# playback would, so @tool runtime consumers present the final VFX/hitbox result live.
#
# Any non-forward-transport playhead move (scrub in either direction, backward tick, loop
# wrap, stop, clip switch, unbind, teardown) is a RESET: fan out reset_preview() to the
# FROZEN scene-tree group &"spine_notify_cue_components" and re-anchor the cursor. The dock
# never references the consumer's class or file — the group + has_method guard is the whole
# coupling, and the group being empty (until the runtime side lands) is expected.

const CUE_GROUP := &"spine_notify_cue_components"

var _dock                        # timeline_dock — owns player / track / current-anim / tree
var _cursor: float = 0.0         # time up to which the current forward pass has emitted
var _in_reset: bool = true       # debounce: set true by a reset, cleared by the next emit

func setup(dock) -> void:
	_dock = dock

# Forward transport advance: emit current-clip notifies in (cursor, to] ascending, move the cursor.
func advance_forward(to: float) -> void:
	if not _player_valid() or _dock.track == null:
		_cursor = to
		return
	var anim: String = _dock._current_animation()
	var pending: Array = []
	for n in _dock.track.notifies:
		if n != null and n.animation_name == anim and n.time > _cursor and n.time <= to:
			pending.append(n)
	pending.sort_custom(func(a, b): return a.time < b.time)
	for n in pending:
		_dock.player.emit_signal("notified", n.notify_name, n.time, n.payload if n.payload != null else {})
	_cursor = to
	_in_reset = false

# Reset the preview consumers and re-anchor the cursor to `t`. Debounced so a continuous
# backward/scrub run (repeated calls with no intervening emit) fans out reset_preview() once.
func reset_to(t: float) -> void:
	if not _in_reset:
		_fan_out_reset()
		_in_reset = true
	_cursor = t

func _player_valid() -> bool:
	return _dock != null and _dock.player != null and is_instance_valid(_dock.player)

func _fan_out_reset() -> void:
	if _dock == null:
		return
	var tree = _dock.get_tree()
	if tree == null:
		return
	for node in tree.get_nodes_in_group(CUE_GROUP):
		if node.has_method("reset_preview"):
			node.reset_preview()
