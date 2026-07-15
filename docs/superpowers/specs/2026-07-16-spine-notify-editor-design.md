# Spine Notify Editor — timeline dock (editor MVP)

Date: 2026-07-16
Status: design, pending implementation
Scope: spine-godot **GDExtension** runtime (2 small C++ helpers) + a **GDScript EditorPlugin addon**

Step 2 + 3 of the notify system (step 1, the runtime `SpineAnimationPlayer`, is committed:
`docs/superpowers/specs/2026-07-15-spine-animation-player-design.md`). This makes notifies
**authorable on a timeline** instead of by hand-editing arrays in the inspector.

## 1. Goal

An in-editor timeline dock to (a) **preview** a spine animation by scrubbing/playing a playhead, and
(b) **author** `SpineNotify` markers on that timeline at precise frames, with the skeleton's native
spine events shown as read-only reference. Works with the shipped GDExtension — no engine module.

## 2. Components

### C++ (extension runtime) — two small helpers
1. **`SpineSprite3D::pose_at(animation_name: String, time: float)`** — poses the skeleton at `time` of
   `animation_name` **transiently**: guard skeleton/data valid → find the animation (no-op if missing) →
   `set_to_setup_pose` → apply the animation at the absolute `time` → `update_world_transform` → rebuild
   the display + debug mesh so the viewport refreshes. It does **not** touch the stored
   `preview_animation`/`preview_time`/`preview_frame` properties, so scrubbing never marks the scene
   dirty. (The step-1 spike proved editor scrubbing works via `preview_time`; this is the clean,
   non-persisting version.)
2. **`SpineSkeletonDataResource::get_animation_events(animation_name: String) -> Array`** — returns
   `[{ "time": float, "name": String }, …]` for the animation's spine events: find the animation, iterate
   `get_timelines()`, find the `EventTimeline` (RTTI `isExactly(spine::EventTimeline::rtti)`), read its
   frames (times) + events (`Event->getData().getName()`). Empty array if the animation has no events.
   Feeds the read-only events lane. (The generic `SpineTimeline` wrapper exposes frame times but not the
   per-frame event, so this dedicated accessor is required.)

Both get headless GDScript tests (like the step-1 runtime).

### GDScript addon `spine_notify_editor`
Lives at `spine-godot/example-v4-extension/addons/spine_notify_editor/` (the user copies it next to their
`spine_godot` addon and enables it in Project Settings → Plugins). Files:
- `plugin.cfg` — addon manifest (`script="plugin.gd"`).
- `plugin.gd` (`@tool extends EditorPlugin`) — lifecycle + selection binding + owns the `EditorUndoRedoManager`.
- `timeline_dock.gd` (`@tool extends VBoxContainer`) — the dock UI.
- `timeline_view.gd` (`@tool extends Control`) — the timeline canvas (custom `_draw` + `_gui_input`).

## 3. Plugin lifecycle + binding

`plugin.gd`:
- `_enter_tree()`: instance the dock, `add_control_to_bottom_panel(dock, "Spine Notifies")`; pass the dock
  `get_undo_redo()` (the `EditorUndoRedoManager`) and `get_editor_interface()`; connect
  `get_editor_interface().get_selection().selection_changed`.
- `_exit_tree()`: `remove_control_from_bottom_panel(dock)`, free the dock.
- On `selection_changed`: pick the first selected `SpineAnimationPlayer` (or a `SpineSprite3D` with a
  child player) and `dock.bind(player)`; if none, `dock.bind(null)` → placeholder.

The dock binds to: the `SpineAnimationPlayer`, its parent `SpineSprite3D` (preview target), and the
player's `notify_track` (a `SpineNotifyTrack`). If `notify_track` is null, the dock shows a **Create
Notify Track** button that creates one and assigns it to the player (via undo/redo).

## 4. Layout

```
┌ toolbar ─────────────────────────────────────────────────┐
│ [animation ▾] [▶/⏸] [loop ☑] [snap ☑ 30fps]  0.42 / 1.00 │
├ timeline_view (Control) ──────────────────────────────────┤
│ ruler   |....|....|....|....|....|....|....|....|          │
│         0.0      0.25     0.5      0.75     1.0            │
│ events  │   ❘         ❘                     (read-only)    │
│ notify  │     ◆         ◆      ◆            (editable)     │
│                    ▲ playhead                              │
├ marker strip (shown when a notify is selected) ───────────┤
│ name:[muzzle_flash]  channel:[vfx]  [Edit in Inspector]   │
└───────────────────────────────────────────────────────────┘
```
- **Animation dropdown**: the skeleton's animation names (`skeleton_data_res.get_animation_names`). Changing
  it re-reads notifies (filtered `animation_name == selected`) and `get_animation_events(selected)`.
- **time label**: `playhead_time / duration` (duration from `find_animation(sel).get_duration()`).
- **snap toggle + fps**: the frame grid is `skeleton_data_res.get_fps()` (fallback 30 if 0); the fps value
  is shown next to the toggle. When on, the ruler draws frame ticks at the grid.

## 5. Interaction + preview

- **Scrub**: drag the playhead, or click the ruler/timeline → set `playhead_time` from the mouse x
  (`x / view_width * duration`, clamped) → `sprite.pose_at(sel_anim, playhead_time)` → viewport updates.
- **Play/pause/loop**: `_process(delta)`: if playing, `playhead_time += delta`; wrap to 0 at `duration`
  when loop is on (else stop at the end); each frame `pose_at(sel_anim, playhead_time)`. `@tool` `_process`
  runs in the editor (proven by the spike).
- **Add notify**: click empty space in the notify lane → time from x → new `SpineNotify`
  (`animation_name=sel_anim`, `time`, `notify_name="notify"`, `channel="default"`) appended to
  `notify_track.notifies` → select it.
- **Move notify**: drag a marker horizontally → live-updates its `time` (and scrubs the preview to it);
  the undo action is committed on drag release.
- **Select / edit**: click a marker → highlight + fill the marker strip; `name`/`channel` edited in the
  strip (LineEdits) write back to the `SpineNotify`. The strip also has an **Edit in Inspector** button
  that calls `EditorInterface.edit_resource(notify)` so the full `SpineNotify` (including `payload`) opens
  in the Inspector — that is the MVP's payload editor (no custom payload widget in the dock).
- **Delete**: `Delete` key or right-click a marker → remove from `notify_track.notifies`.
- **Frame snapping**: when the snap toggle is on, every time computed from the mouse — the playhead scrub,
  add-notify, and marker drag — is snapped to the nearest frame: `snapped = round(t * fps) / fps` (fps from
  `get_fps()`, fallback 30). Holding a modifier (Ctrl) temporarily disables snap for a fine adjustment.
  Snap off → free/continuous times.

## 6. Undo/redo + persistence

Every mutation (add/move/delete/edit a notify, create a track, edit a field) goes through the
`EditorUndoRedoManager`: `create_action("…")` → `add_do_method`/`add_undo_method` (or
`add_do_property`/`add_undo_property`) targeting the `SpineNotifyTrack` (and the `SpineNotify`) →
`commit_action()`. Targeting the resource marks it modified so **Ctrl+S** persists it (inline sub-resource
or a saved `.tres`). Live scrub/preview during a drag is transient (`pose_at`) and not part of the undo
action; only the final `time` change is committed.

## 7. Error handling / edge cases

- No `SpineAnimationPlayer` selected → dock shows a placeholder ("Select a SpineAnimationPlayer").
- Player's parent isn't a `SpineSprite3D`, or the sprite has no `skeleton_data_res` → disable preview,
  show a hint; still allow viewing.
- `notify_track` null → **Create Notify Track** button.
- Selected animation has no events → events lane empty (no error).
- `pose_at` with a missing animation name → no-op (guarded in C++).
- Animation duration 0 → guard against divide-by-zero in x↔time mapping.
- The dock rebinds cleanly on selection change and on the bound node being freed (guard `is_instance_valid`).

## 8. Testing

- **C++ helpers (headless GDScript, extension build):**
  - `pose_at`: play nothing; call `sprite.pose_at("walk", 0.5)`; assert a known bone's transform matches
    the walk-at-0.5 pose and differs from `pose_at("walk", 0.0)`; assert stored `preview_time` is
    unchanged (no dirty). (Mirrors the step-1 spike's bone-readback method.)
  - `get_animation_events`: `data.get_animation_events("walk")` returns the `footstep` keys with correct
    times/names; an animation with no events returns `[]`.
- **Dock (manual editor smoke test — a checklist in the plan):** enable the addon, select a
  `SpineAnimationPlayer`, verify: animation dropdown lists animations; dragging the playhead scrubs the
  viewport; play/pause/loop animates; clicking adds a notify; dragging moves it (preview follows); the
  events lane shows spine events; name/channel edits persist and Ctrl+S saves; undo/redo works. (Editor
  GUI isn't headless-testable; the underlying pieces — `pose_at`, `get_animation_events`, the notify
  resources, the runtime firing — are all separately tested.)

## 9. Out of scope (deferred — the "Full timeline" follow-up)

- Multiple editable lanes grouped by `channel` (MVP: one notify lane; channel is a per-marker field).
- Snap-to-event / snap-to-marker (MVP has frame snapping via `get_fps()`, but not magnet-snapping to
  nearby spine-event ticks or other notifies).
- A custom payload-editing widget in the dock (MVP: `name`/`channel` in the strip; `payload` via the
  Inspector through an "Edit in Inspector" button).
- 2D `SpineSprite` parity (this targets `SpineSprite3D`; `pose_at`/`get_animation_events` port cleanly later).

## 10. Naming
Addon `spine_notify_editor`; bottom-panel tab "Spine Notifies"; `SpineSprite3D::pose_at`;
`SpineSkeletonDataResource::get_animation_events`. Adjust in review if preferred.
