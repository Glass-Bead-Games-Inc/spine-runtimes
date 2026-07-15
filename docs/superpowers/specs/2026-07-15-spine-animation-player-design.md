# SpineAnimationPlayer — frame-accurate notify system (runtime MVP)

Date: 2026-07-15
Status: design, pending implementation
Scope: spine-godot **GDExtension** runtime (no engine module)

Terminology: **notify** = a named trigger placed at a time on an animation (as in Unreal's "Anim
Notifies"). A **notify track** is the reusable resource that holds a character's notifies.

## 1. Goal

Let a game trigger arbitrary Godot-side effects (particle systems, mesh VFX, sounds, gameplay
callbacks) at precise frames of a spine animation, **authored in Godot** — without round-tripping to
the Spine editor to add event keys, and without a custom engine build.

This spec covers **step 1: the runtime**. The in-editor timeline UI is a later, separate step (see §9).

## 2. What it is / isn't

- **Is:** a passive observer of the animation the sprite is already playing, that fires a single
  `notified` signal — for both Godot-authored notifies **and** spine-native events — plus thin
  convenience `play()`/`seek()` wrappers.
- **Isn't:** a second animation driver. It never runs its own `AnimationState` or skeleton; it reads the
  sprite's existing track state. It does not itself spawn VFX — the game decides what a notify does.

## 3. Components

### `SpineAnimationPlayer` (Node, added to the GDExtension)
A node placed as a **child of a `SpineSprite3D`** (same attach/auto-connect pattern as
`SpineBoneNode3D`/`SpineSlotNode3D`: on `NOTIFICATION_PARENTED` it finds the parent sprite and connects;
warns if the parent isn't a `SpineSprite3D`).

Exports:
- `notify_track: SpineNotifyTrack` — the reusable per-character notify data (optional; null = only spine
  events fire, if enabled).
- `forward_spine_events: bool = true` — mirror the sprite's native `animation_event`s into `notified`.

Signal:
- `notified(name: String, time: float, payload: Dictionary)`

Convenience control (thin forwarders to the sprite's `SpineAnimationState`; optional to call):
- `play(animation_name: String, loop := true, track := 0)`
- `seek(time: float, track := 0)` — sets the track entry's track time (a "scrub"); marks the crossing
  state discontinuous so skipped notifies do **not** retro-fire.

### `SpineNotifyTrack` (Resource, reusable, one per character)
- `notifies: Array[SpineNotify]` — flat list; authored once, assigned to every instance of that
  character.

### `SpineNotify` (Resource)
- `animation_name: String` — which animation this notify belongs to.
- `time: float` — seconds into that animation.
- `name: String` — the notify name the game switches on (e.g. `"muzzle_flash"`).
- `channel: String = "default"` — the lane/group (for the future timeline editor; ignored at runtime).
- `payload: Dictionary` — free-form data passed through (e.g. `{ "bone": "gun-tip", "scale": 1.5 }`).

A typed `Array[SpineNotify]` gives a clean inspector for the MVP (author notifies directly in the
inspector) and maps 1:1 onto lanes in the later timeline editor via `channel`.

## 4. Data flow

```
game / AnimationState / SpineAnimationTrack  -- drives -->  SpineSprite3D.animation_state
                                                                   │
   per frame (update_skeleton):                                    │
     animation_state.update(delta)                                 │
     emit before_animation_state_apply  ───────────────────► SpineAnimationPlayer._on_apply()
     animation_state.apply(skeleton)  ── fires spine events ─► animation_event ─► forward (if enabled)
                                                                   │
   SpineAnimationPlayer:                                           ▼
     for each active track entry:                             notified(name, time, payload)
       anim  = entry.get_animation().get_name()                    │
       t     = entry.get_animation_time()  (wrapped)               ▼
       fire notifies for `anim` crossed in (prev_t, t]        game VFX code (spawn/emit/call)
```

- **Godot notifies** fire from the `before_animation_state_apply` hook (fires every frame, in the
  sprite's own update — so it respects the sprite's `update_mode` Process/Physics/Manual, `time_scale`,
  and pause, with no separate `_process` timer).
- **Spine events** fire from the existing `animation_event` signal, normalized into the same `notified`:
  `name = event.get_data().get_event_name()`, `time = event.get_time()`, and
  `payload = { source: "spine_event", int: …, float: …, string: …, volume: …, balance: … }`. Godot
  notifies use `payload.source = "notify"`.

## 5. Crossing detection

Per active track entry, keep `prev_animation_time`. Each frame, fire notifies whose `time` lies in the
half-open interval advanced this frame — this is the same algorithm spine uses for its own events, so
Godot notifies stay consistent with spine events:

- **Forward play:** fire notifies with `prev < time <= cur`.
- **Loop wrap** (`cur < prev` because it wrapped past the end): fire `(prev, duration]` then `(0, cur]`.
- **Reverse** (`time_scale < 0`, `cur < prev` without a wrap): fire `[cur, prev)`.
- **Discontinuity** — reset `prev = cur` and fire nothing (avoids VFX spam on a scrub/teleport). A
  discontinuity is any of: the entry's animation changed since last frame; a `seek()` happened; or the
  observed time change disagrees with the expected advance (`delta * time_scale`) beyond a small epsilon
  (the animation didn't advance by roughly one frame). New/first-seen track entries also start here.
- A per-entry cursor over notifies pre-sorted by time keeps this O(notifies fired), not O(all notifies).

**All active track entries are watched** (not just track 0), each resolving its own animation name via
`entry.get_animation().get_name()` — so an additive "shoot" overlaid on a "run" fires its notifies too.
Entries are keyed by track index so per-entry `prev` state stays separate.

## 6. Performance

Passive observer; per frame per sprite it adds one already-emitted signal callback, a few getters, and a
handful of float comparisons — negligible next to `build_meshes()`/`update_world_transform()`, which run
regardless. No duplicated skeleton/animation. Still fires while the sprite is culled (the runtime updates
the animation when hidden and only skips the GPU mesh build), so notifies aren't missed off-screen.

## 7. Error handling / edge cases

- No `notify_track` and `forward_spine_events = false` → inert (no-op).
- Parent isn't a `SpineSprite3D` → `WARN_PRINT`, stay inert.
- Notify references an animation the skeleton doesn't have → skipped (never matches an active entry).
- Skeleton/animation-state not yet valid (pre-`_ready`) → guarded, no crash.
- Duplicate notifies at the same time → all fire, in array order.
- `seek()`/animation switch → discontinuity reset, no retro-fire.

## 8. Testing

Headless (`--headless`, extension build), driving a spineboy sprite from a script:
- **Timing:** play `walk`, place notifies at known times, assert `notified` fires once each as
  `get_animation_time()` crosses them; assert none fire when scrubbing via `seek()`.
- **Loop:** loop `walk`, assert notifies re-fire every cycle (wrap handled).
- **time_scale / reverse:** assert notifies still fire correctly at 0.5× and negative scale.
- **Spine events:** on an animation with a native event, assert it arrives via `notified` with
  `payload.source == "spine_event"` and the correct name/int/float/string.
- **Inertness:** no track + events off → zero signals.

## 9. Out of scope (explicit — future steps)

- **`pose_at(anim, time)`** transient scrub helper on `SpineSprite3D` (poses without dirtying saved
  `preview_time`). Needed to unblock the editor timeline's scrubbing; separate small runtime change.
- **Timeline dock** (`EditorPlugin`): visual authoring/scrubbing, a read-only lane of the skeleton's
  spine events pulled from `SkeletonData`, and editable lanes per `channel`. Built on top of this runtime.
- Multi-page/2D `SpineSprite` parity for the player (this MVP targets `SpineSprite3D`; the observer logic
  is node-type-agnostic and can extend to 2D `SpineSprite` later).

## 10. Naming

Decided: `SpineAnimationPlayer` (node), `SpineNotifyTrack` (resource), `SpineNotify` (item), `notified`
(signal) — "notify" throughout, mirroring Unreal's Anim Notify terminology.
