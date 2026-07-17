# SpineAnimation dock — full-timeline follow-up (channel lanes, frame-magnet snap, typed payload)

Date: 2026-07-16
Status: design, pending implementation
Scope: the **GDScript EditorPlugin addon** `spine_notify_editor` only. No C++, no DLL rebuild.

Follows the shipped MVP (`docs/superpowers/specs/2026-07-16-spine-notify-editor-design.md`). Delivers three of
the four items from that spec's §9 "deferred / full timeline" list. (2D `SpineSprite` parity is **not** in
this round.) The bottom-panel tab is now **"SpineAnimation"**.

## 1. Goal

Grow the single notify lane into a channel-organized timeline, make snapping magnetize to existing
event/notify times (while staying frame-exact), and edit a notify's `Dictionary payload` inline with typed
values — without leaving the dock.

## 2. Components (all in the addon)

- `timeline_view.gd` — multi-lane layout, per-lane draw + hit-test, add-to-channel, magnet `snap()`.
- `timeline_dock.gd` — "+ Channel" control, the typed payload editor in the marker strip.

No changes to the C++ runtime, `plugin.gd`, or `plugin.cfg`. `SpineNotify` already carries
`channel: String` (default `"default"`) and `payload: Dictionary`; nothing new is persisted on the resources.

## 3. Channel lanes (auto-derived + "+ Channel")

The area below the read-only events lane becomes **N stacked channel lanes**, one per channel.

- **Lane set** (`channels() -> PackedStringArray`): the distinct `channel` values across the current
  animation's notifies, **unioned** with the dock's session-local `_extra_channels`, sorted alphabetically
  with `"default"` forced first. Always contains at least `"default"`.
- **Layout constants** (view): `LANE_H = 20.0`, `LANE_LABEL_W = 72.0`. Lane `i` occupies
  `y ∈ [lanes_top + i*LANE_H, lanes_top + (i+1)*LANE_H)`, where `lanes_top = EVENTS_Y + 12`. The channel
  name is drawn left-aligned in the `LANE_LABEL_W` gutter; the lane's track (where diamonds sit) starts at
  `_track_left()` as today. A faint separator line divides lanes.
- **View min height** grows with lane count: in `refresh()`, set
  `custom_minimum_size.y = lanes_top + channels().size() * LANE_H + PAD` so every lane is visible in the
  bottom panel (overflow/scroll for very many channels is out of scope).
- **Diamond position**: a notify draws at `x = x_from_time(n.time)`, `y = lane_center(index_of(n.channel))`.
  Selected diamond highlighted as today.
- **"+ Channel"**: a small `LineEdit` ("new channel…") + `Button` ("+ Channel") in the toolbar. Submitting a
  non-empty, non-duplicate name appends it to `_extra_channels` and refreshes → an empty lane appears,
  clickable to add the first notify. Empty channels are **not** persisted; a lane becomes data-backed once a
  notify with that channel exists.

### Interaction
- **Add**: left-click an empty spot inside a lane → `add_notify_at(t, channel)` where `channel` is that
  lane's channel; the new `SpineNotify` gets `channel =` it. Time from x, frame-snapped unless Ctrl.
- **Select / drag / delete**: unchanged, but hit-testing is now 2-D — a click hits a notify only if it is
  within `HIT_PX` in x **and** inside that notify's lane in y. Horizontal drag changes `time` only (drag/undo
  stays as the MVP). Delete/select as before.
- **Move between channels**: edit the strip's `channel` field → on redraw the marker appears in the matching
  lane (creating a session lane if needed). *Vertical-drag-to-move-channel is a deliberate non-goal this
  round.*

## 4. Frame-magnet snap (folded into the existing Snap toggle)

No new toggle. Everything placed stays **frame-exact**; the magnet just gives event/notify times a generous
capture radius so you can drag "near the footstep" and land on it. `snap(t)`:

- Snap **off** → return `t` (continuous). (`Ctrl` at the call site also bypasses to free.)
- Snap **on**:
  1. Frame time `f = round(t * fps) / fps` (fps from `get_fps()`, fallback 30).
  2. **Magnet**: among all event times (`_events`) and current-animation notify times
     (`notifies_for_current()`, **excluding** `_drag_notify`), find the target whose on-screen x is within
     `MAGNET_PX` of `x_from_time(t)` and closest. If one is found, return **its frame-aligned time**
     `round(target * fps) / fps` — so even an off-grid event resolves to an exact frame.
  3. Otherwise return `f`.

`MAGNET_PX = 10.0` (a fixed on-screen radius). Why a pixel radius rather than "nearest of {frame, event,
notify}": with frame-aligned targets the nearest frame to an event *is* that event's frame, so a plain
nearest-of would never differ from frame snap. A fixed pixel radius instead gives a **consistent visual
stickiness** that adapts to frame density: for a long/dense animation (frames only a few px apart) 10px spans
one-plus frames, so dragging near a footstep snaps onto it even though the geometrically-nearest frame is a
different one; for a short/sparse animation (frames far apart) 10px is under half a frame, so the magnet
rarely overrides and plain frame snapping — which already lands you on the event's frame — does the work.
Either way the result is frame-aligned (`round(target*fps)/fps`), so frame-exactness always holds. Applies
everywhere mouse times originate (scrub, add, drag) since those route through `snap()`. A dragged notify
excludes itself so it can't magnet to its own old position.

## 5. Typed payload editor (inline, in the marker strip)

When a notify is selected, the strip becomes a small vertical panel:

```
notify: [name]   channel:[default]   [Edit in Inspector]
payload:  [key ........] [String ▾] [value .......] [−]
          [key ........] [float  ▾] [value .......] [−]
          [ + Add key ]
```

- One row per payload entry: a key `LineEdit`, a type `OptionButton` (`String`, `int`, `float`, `bool`), a
  value `LineEdit`, and a `−` remove `Button`. A `+ Add key` button appends an empty row.
- **Populate**: `edit_notify(n)` rebuilds the rows from `n.payload` — inferring each existing value's type
  from its `Variant` type (`TYPE_INT`→int, `TYPE_FLOAT`→float, `TYPE_BOOL`→bool, else String) and filling the
  value text via `str(value)`.
- **Value parsing** by selected type: `String`→text as-is; `int`→`text.to_int()`; `float`→`text.to_float()`;
  `bool`→`text.strip_edges().to_lower() in ["true","1","on","yes"]`.
- **Commit**: any row change (value/key `focus_exited` or `text_submitted`, type `item_selected`, add, remove)
  calls `_apply_payload()`, which rebuilds the whole `Dictionary` from the rows (skipping blank keys; last
  wins on duplicate keys) and writes it to `_selected_notify.payload` through `EditorUndoRedoManager`
  (`add_do_property`/`add_undo_property` on `"payload"`, capturing the current dict as undo), with the
  null-`undo_redo` direct-assignment fallback. No-op if the rebuilt dict equals the current one (avoids empty
  undo actions). Then `track.emit_changed()` + `_view.queue_redraw()`.
- The existing **"Edit in Inspector"** button stays (full/advanced payload editing, nested values, etc.).

## 6. Undo/redo + persistence

Same model as the MVP: every mutation targets the `SpineNotify`/`SpineNotifyTrack` resource through
`EditorUndoRedoManager` (so Ctrl+S persists), with a direct-assignment fallback when `undo_redo` is null
(headless). Adding/removing a session channel lane is **not** a resource mutation (no undo entry); it only
affects the dock's local `_extra_channels`.

## 7. Error handling / edge cases

- No notifies and no extra channels → still show the single `"default"` lane.
- `+ Channel` with a blank or duplicate name → ignored (no lane added).
- A notify whose `channel` is `""` → treated as `"default"` for lane placement.
- Payload row with a blank key → skipped when building the dict (not written).
- `int`/`float` parse of non-numeric text → `to_int()`/`to_float()` yield `0`/`0.0` (Godot semantics); no
  error, no crash.
- Selecting a different animation clears the strip + payload rows (existing selection-reset behavior).
- All existing guards (`is_instance_valid`, zero-duration divide protection, freed-node safety) preserved.

## 8. Testing

**Headless logic (GDScript, extension build):**
- **Channels:** notifies with channels `{default, vfx, sfx}` → `channels()` returns `["default","sfx","vfx"]`
  (default first, rest sorted); `add_notify_at(t, "vfx")` yields a notify with `channel == "vfx"`; adding
  `"gameplay"` to `_extra_channels` makes it appear in `channels()` even with no notifies.
- **Magnet snap:** use a **narrow** view (`200×260`, track width ≈ 116 → ≈ 3.9 px/frame, so the 10px magnet
  spans ≈ 2.6 frames and genuinely overrides plain frame snap), Snap on, fps 30, event at 0.5, notify added at
  0.30. `snap(0.47)` (≈3.5px from the 0.5 event) `== 0.5` while plain frame would be `14/30 ≈ 0.4667` (magnet
  overrides → RED against old snap); `snap(0.32)` (≈2.3px from the 0.30 notify) `== 0.30` vs plain `10/30 ≈
  0.3333`; `snap(0.80)` (far from every target) `== round(0.80*30)/30 == 0.80` (plain frame). With
  `_drag_notify` = the 0.30 notify, `snap(0.32)` no longer magnets to 0.30 (self excluded) → its frame
  `round(0.32*30)/30 == 10/30 ≈ 0.3333`.
- **Payload:** set rows `{speed: float 2.5, loop: bool true, tag: String hi}` → `_apply_payload` →
  `notify.payload == {"speed": 2.5, "loop": true, "tag": "hi"}` with correct Variant types; re-`edit_notify`
  round-trips the rows (types inferred); blank-key row is dropped.

**Capture (`--write-movie`) self-validation:** render the dock bound to spineboy `walk` with notifies across
`default` / `vfx` / `sfx` lanes, one selected showing 2–3 typed payload rows; read frames and verify the
stacked labelled lanes, per-lane diamonds, the events lane, and the payload panel. (Interactive mouse/undo is
the human smoke test.)

**Then** copy the updated addon to the Shanhai project (DLL unchanged) for the user's smoke test.

## 9. Out of scope (still deferred)

- 2D `SpineSprite` parity.
- Vertical-drag to move a notify between lanes (channel changes go through the strip field this round).
- Channel lane reordering / colors / per-channel mute, and scrolling when channels exceed the panel height.
- A managed/persisted channel list on `SpineNotifyTrack` (channels remain derived + session-local).
- Nested/array payload values in the inline editor (use "Edit in Inspector" for those).
