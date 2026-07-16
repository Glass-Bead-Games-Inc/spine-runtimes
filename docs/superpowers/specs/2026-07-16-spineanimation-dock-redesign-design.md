# SpineAnimation dock — redesign (AnimationPlayer-style timeline)

Date: 2026-07-16
Status: design, approved (mock signed off), pending implementation
Scope: the **GDScript EditorPlugin addon** `spine_notify_editor` only. No C++, no DLL rebuild.

Reworks the dock UI to mirror Godot's built-in **AnimationPlayer** (transport + track sizing + left track headers)
and Unreal's **Animation Montage** (labeled markers + hover). Approved mock:
`https://claude.ai/code/artifact/21473bf0-1838-4e52-8f2a-d109778bb679`.

## 1. Goals (the seven requests)

1. **Taller tracks, bigger type** — 36px rows, 13px labels (was 20px / 10px).
2. **Icon transport, full set** — replace the "Play" text button with AnimationPlayer's icon transport
   (play-backwards-from-current, play-backwards-from-end, stop, play-from-start, play-from-current) plus the
   Loop and Snap toggles. Playback supports both directions + start/end/current entry points.
3. **No channel editor on a notify** — the notify's channel is the lane it lives in; the detail strip shows it
   as a **read-only chip** and drops the channel `LineEdit`.
4. **Events drawn like notifies** — spine events use the same flag marker as notifies, in **teal** (vs amber),
   on a **read-only Events track** (a real track row, not a thin lane).
5. **Names on every marker + hover tooltip** — each marker shows its name inline (ellipsized if long); hovering
   shows a tooltip with name, time, frame, and (notifies) a payload summary.
6. **Add & name tracks** — a **"+ New track"** control at the top of the track-header column prompts for a name
   on creation; **double-clicking a track name renames** it, re-tagging that lane's notifies (current animation).

## 2. Layout restructure

Today `timeline_view.gd` draws everything (including a channel-name gutter) in one `_draw`. The redesign splits
the timeline into a **left header column of real Controls** + a **right canvas** so track names can be
buttons/inline-edited, matching AnimationPlayer:

```
timeline_dock (VBoxContainer)
├─ _toolbar (HBoxContainer)          — animation dropdown + icon transport + Loop/Snap + readout
├─ _timeline_row (HBoxContainer)
│  ├─ _headers (VBoxContainer, ~176px, fixed)
│  │   ├─ header cell 0  (ruler height) → "+ New track" button
│  │   ├─ Events header  (read-only, teal, lock icon)
│  │   └─ one header per channel: swatch + name (dbl-click to rename) + pencil-on-hover
│  └─ _view (timeline_view.gd, Control, expand-fill)  — ruler + track rows + markers + tooltip + playhead
└─ _strip (VBoxContainer)            — selected-notify details (name + read-only channel chip + payload)
```

**Row model (canvas):** `_view` draws a ruler row (`RULER_H = 28`) then N track rows (`LANE_H = 36`). Track 0 is
always the read-only **Events** row; tracks 1..N are the channels from `channels()`. Row `i` occupies
`y ∈ [RULER_H + i*LANE_H, RULER_H + (i+1)*LANE_H)`. The canvas no longer draws a name gutter, so `_track_left()`
returns `PAD` again and markers use the full canvas width. The `_headers` VBox uses the **same** cell heights
(28 then 36×) so its rows line up with the canvas rows by construction.

**Tracks list** (`_view.tracks() -> Array`): `[{kind:"events"}]` + `channels().map(ch -> {kind:"notify", channel:ch})`.
`channels()` is unchanged from today (current-animation notify channels ∪ `_extra_channels`, `""`→`"default"`,
sorted with `"default"` first).

## 3. Markers (notifies + events), labels, tooltips

- **Flag marker** (both kinds): drawn at `x = x_from_time(time)`, centered in its row. A rounded body with a
  small left pointer (diamond nub) at the exact time, the **name** drawn inside. Notify = amber
  (`#e0993e`, selected `#ffca4a`), event = teal (`#49b7b0`). Events are read-only (not draggable/selectable for
  editing; they still show tooltips).
- **Label** = the marker name. Clipped to `LABEL_MAX = 120px`; if the name's measured width exceeds it, truncate
  and append `…`. Full name always available via the tooltip.
- **Hover tooltip:** `_gui_input` mouse-motion hit-tests the marker under the cursor (`marker_at_pos(x,y)` →
  `{kind, ref, row}`); on change, store `_hovered` + `queue_redraw()`; mouse-exit clears it. `_draw` renders a
  tooltip box near the hovered marker: title = name + a kind/channel chip, then `time` (`0.200 s · f6`) and, for
  notifies, a payload summary (`N keys`, or the first key/value). The box is clamped to stay within the canvas.
- **Hit-testing** is 2-D (within `HIT_PX` in x AND inside the marker's row in y). Notify hit → select/drag as
  today; event hit → hover-only (no select/drag/add). Clicking an empty spot in a channel row adds a notify to
  that channel; clicking the Events row or the ruler scrubs.

## 4. Transport + playback (AnimationPlayer-style)

Replace the `Play` text button with an icon `Button` group that uses **the editor's own AnimationPlayer icons**
(this is a `@tool` plugin, so the editor theme is available). Buttons, with the exact `EditorIcons` name and
tooltip AnimationPlayer uses:

1. `PlayBackwards` — "Play selected animation backwards from current pos." → dir −1, from current, playing.
2. `PlayStartBackwards` — "Play selected animation backwards from end." → playhead = duration, dir −1, playing.
3. `Stop` — "Pause/stop animation playback." → playing = false (pause in place). Second press when already
   stopped → reset playhead to 0.
4. `PlayStart` — "Play selected animation from start." → playhead = 0, dir +1, playing.
5. `Play` — "Play selected animation from current pos." → dir +1, from current, playing.

Plus **Loop** (`Loop`) and **Snap** (`SnapGrid`) toggles. Playback state: `playing: bool`, `play_dir: int`
(+1/−1). `_process(delta)`: when `playing`, `t = playhead + play_dir*delta`; if `t` passes an end (`>= dur`
forward, `<= 0` back): if Loop wrap to the other end, else clamp and `playing = false`. Each step
`_view.set_playhead(t)` (poses via `pose_at`). The active play button shows a pressed state; Stop clears it.
Readout: `t s · frame f / fps · dur s`.

**Icon resolution + fallback:** set each button's `icon` from `get_theme_icon(&"PlayStart", &"EditorIcons")`
etc. via a helper `_editor_icon(name) -> Texture2D` that returns the theme icon when it resolves and `null`
otherwise. In-editor these are the real AnimationPlayer glyphs. In a **headless test or a standalone
`--write-movie` capture** the `EditorIcons` theme type isn't loaded, so the helper returns `null` and the button
falls back to a short text label (`"|>"`, `"<|"`, `"[]"`, `">|"`, `"<"`) so nothing breaks and the capture still
reads. No external assets, no hand-drawn glyphs.

## 5. Track headers — add & rename

- **"+ New track"** (header cell 0): clicking reveals an inline `LineEdit` ("channel name…"); Enter/commit with a
  non-blank, non-duplicate name appends to `_extra_channels` + `refresh()`. (Escape/blank cancels.)
- **Rename:** each channel header's name is a `Button` (flat) or `Label`; **double-click** swaps it for an inline
  `LineEdit` seeded with the current name. Commit (Enter/focus-out) with a changed, non-blank, non-duplicate name:
  - re-tag every current-animation notify whose `channel == old` to `new` (one undoable action, null-`undo_redo`
    fallback = direct set + `emit_changed`);
  - if `old` was in `_extra_channels`, replace it with `new`;
  - `refresh()`. The **Events** and **default** rows are **not** renameable (Events is read-only; `default` is the
    reserved base channel). Rename scope is the current animation's notifies (channels are per-notify tags; other
    animations keep theirs).

## 6. Detail strip (point 3)

Drop `_channel_edit`. The strip row 1 becomes: `notify:` + name `LineEdit` + a **read-only channel chip**
(amber-tinted, shows `_selected_notify.channel`, not editable) + `Edit in Inspector`. Below: the typed payload
editor (unchanged from the shipped version — key / type / value rows + `+ Add key`). `_apply_strip` writes only
`notify_name` now (not channel); the undo action becomes "Rename Notify". The `edit_notify` populate path sets the
chip text instead of a channel field. Everything else (payload rows, undo/redo, the `_process` resync) unchanged.

## 7. Preserved behavior

- Frame snapping + frame-magnet (from the shipped version) unchanged; Ctrl still frees.
- Undo/redo through `EditorUndoRedoManager` with null fallback on every mutation; `_extra_channels`
  add/rename of an *empty session lane* is not a resource mutation.
- Selection/strip/playhead reset on bind/animation-change; `_prune_selection`; the payload/name resync on
  undo/redo (`_sync_strip_if_stale`) — all preserved (channel is no longer part of the strip signature).
- `_sync` and per-frame redraw stay gated on `is_visible_in_tree()`.

## 8. Error handling / edge cases

- Empty label / duplicate on add or rename → ignored (no-op).
- Renaming to a name that collides with an existing channel → treated as a merge (notifies just join that lane);
  or rejected as duplicate — **rejected as duplicate** for MVP (simplest, predictable).
- Events row with no events → empty read-only row (still shown, so the layout is stable).
- Very long names → ellipsized in the flag; full text in the tooltip.
- Zero-duration animation → x↔time guards from the shipped version hold; playback clamps.
- Many tracks exceeding the panel height → no scroll this round (documented out-of-scope, §10).

## 9. Testing

Per-task headless logic tests (extension build), plus a `--write-movie` capture at the end:
- **Layout/markers:** `tracks()` returns Events + channels in order; `marker_at_pos` hits notifies in channel
  rows and events in the events row; add-to-channel still tags correctly; Events row is not addable/selectable.
- **Tooltip/labels:** hovering a marker sets `_hovered` to the right ref; a name wider than `LABEL_MAX` reports
  truncated (helper returns the ellipsized string).
- **Transport:** play-from-start sets `playhead=0, play_dir=+1, playing=true`; play-bw-from-end sets
  `playhead=dur, play_dir=-1`; a forward `_process` step past `dur` wraps to 0 when Loop else clamps+stops; a
  backward step past 0 wraps to `dur` when Loop else clamps+stops; Stop clears `playing`.
- **Add/rename tracks:** "+ New track" with a name adds a session lane (appears in `channels()`); rename re-tags
  every current-animation notify in the old lane to the new channel and updates `_extra_channels`; duplicate/blank
  rejected; Events/default not renameable.
- **Strip:** editing name writes `notify_name` (undoable); the channel chip reflects `notify.channel`; the strip
  has no channel editor; the payload editor still round-trips.
- **Capture:** render the reworked dock (taller labelled tracks, teal event flags + amber notify flags with
  names, a hovered tooltip, the icon transport, a header mid-rename, the strip with the read-only chip + payload).

## 10. Out of scope (still deferred)

- Vertical scroll when tracks exceed the panel height; drag-to-reorder tracks; per-track color/mute.
- Vertical-drag to move a notify between lanes (channel change stays via rename / add-in-lane).
- 2D `SpineSprite` parity; nested/array payload values (Inspector); onion skinning / blend-time editing / the
  AnimationPlayer "Animation" (new/load/save) menu — intentionally omitted (authoring animations is the
  skeleton's job).
