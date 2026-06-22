# SpineSprite3D — Spine skeletons in Godot 3D scenes

**Date:** 2026-06-18
**Status:** Approved design, pending implementation plan
**Area:** `spine-godot/spine_godot/`

## Summary

The current spine-godot runtime renders Spine skeletons only as a `Node2D`
(`SpineSprite`) through the `RenderingServer` *canvas item* API. This design adds
a sibling node, **`SpineSprite3D`**, that renders the same skeletons natively in
3D scenes — usable like Godot's own `Sprite3D` (configurable billboard, optional
lighting) while still interacting with the 3D depth buffer.

The entire data and animation layer is reused unchanged. Only the rendering and
transform layers are new.

## Goals

- A native 3D node, `SpineSprite3D`, that draws Spine skeletons in a 3D world.
- Configurable orientation: free 3D orientation by default, with billboard modes
  (`Disabled` / `Enabled` / `Y-Billboard`) like `Sprite3D`.
- **Unshaded by default** (matches the 2D look), with an opt-in **shaded** mode
  plus normal/specular map support.
- Feature parity with `SpineSprite`: clipping, all supported blend modes,
  custom materials (per-sprite and per-slot), slot/bone child nodes, editor
  preview, and debug rendering.
- Godot 4.x, compiled in **both** the engine-module and the GDExtension builds.

## Non-goals

- Godot 3.5 support (the 3D rendering APIs differ too much; legacy target).
- Two-color tinting and screen blend mode (already unsupported in 2D; same here).
- Physically correct lighting of billboarded flat cards (documented as
  inherently approximate).
- The render-to-`SubViewport`-on-a-quad approach (rejected: it is a flat textured
  card, not a true 3D runtime).

## Background: how the 2D runtime works today

- `SpineSprite : Node2D, AnimationStateListenerObject` owns
  `SpineSkeletonDataResource`, `SpineSkeleton`, `SpineAnimationState`, the
  `SkeletonClipping`, update mode, time scale, signals, and editor preview.
- Each slot gets a child `SpineMesh2D : Node2D` that submits geometry through the
  canvas-item API (`canvas_item_add_mesh` / `canvas_item_add_triangle_array`),
  with `Vector2` vertices.
- Draw order is implicit in canvas child ordering (`move_child`,
  `set_draw_behind_parent`).
- Materials are `CanvasItemMaterial` with 2D blend modes, cached in a statics
  singleton (`SpineSpriteStatics::default_materials[4]`).
- Per-frame: `update_skeleton` → `animation_state->update/apply` →
  `skeleton->update_world_transform` → `update_meshes` builds vertex/uv/color/
  index arrays per slot → submits to canvas items.
- The build globs `*.cpp` (`spine_godot/SCsub`), so new files compile in both the
  module and GDExtension builds with no extra wiring. `RenderingServer`/`RS` and
  type usages go through `#ifdef SPINE_GODOT_EXTENSION` shims.

The data/animation half is rendering-agnostic and fully reusable.

## Chosen architecture

**Approach B — a single combined mesh, rebuilt per frame, with Z-spacing.**

The skeleton is rendered as **one visual instance** holding one mesh, split into
surfaces by batch. This was chosen over (A) per-slot child mesh nodes and (C)
render-to-SubViewport because it gives the fewest draw calls, the most robust
ordering (we own it rather than relying on per-object depth sorting), and matches
the proven Unity `SkeletonRenderer` approach, while reusing the full data layer.

## Components

### New classes (`spine-godot/spine_godot/`)

| Class | Extends | Role |
|---|---|---|
| `SpineSprite3D` | `GeometryInstance3D` | 3D analog of `SpineSprite`: owns skeleton/animation state, builds & owns the combined mesh, drives updates, holds signals + materials + debug + preview. |
| `SpineSlotNode3D` | `Node3D` | Follows a slot transform in 3D; carries per-slot custom material overrides. |
| `SpineBoneNode3D` | `Node3D` | Follows a bone transform in 3D. |

A shared **statics singleton** (mirroring `SpineSpriteStatics`) caches the
generated shader/material library and scratch buffers, torn down via
`clear_statics()` on module shutdown.

### Why `GeometryInstance3D`

It is the base Godot's own `Sprite3D` builds on. It provides the instance RID,
frustum culling, `material_override`/`material_overlay`, `cast_shadow`, GI and
visibility settings, and layer masks for free. `SpineSprite3D` calls
`set_base(mesh_rid)` with a self-built mesh and assigns per-surface materials via
`RenderingServer` on `get_instance()`.

### Reused unchanged

`SpineSkeletonDataResource`, `SpineSkeleton`, `SpineAnimationState`,
`SpineAtlasResource`, `SpineRendererObject` (already carries
`texture`/`normal_map`/`specular_map`), `SkeletonClipping`, all constraints, the
`AnimationStateListenerObject` callback and the full signal set. The
animation/update half of `SpineSprite` is copied; only rendering is replaced.

## Rendering pipeline

1. **Update** (mirrors `SpineSprite::update_skeleton`): emit
   `before_animation_state_update` → `animation_state->update/apply` →
   `before_world_transforms_change` → `skeleton->update_world_transform` →
   `world_transforms_changed` → build geometry. `update_mode`
   (`Process`/`Physics`/`Manual`) and `time_scale` carry over identically, as
   does the `!is_visible_in_tree()` early-out.
2. **Batch builder** walks `getDrawOrder()`, computing `RegionAttachment` /
   `MeshAttachment` world vertices and running `SkeletonClipping` exactly as the
   2D path does (that code ports directly, emitting `Vector3` instead of
   `Vector2`). A **batch flushes into a surface** when the next slot changes the
   *(texture page, blend mode, or custom material)*, or when clipping starts/ends.
3. **Draw order → depth.** Because the skeleton is one instance, surfaces render
   in the order added and triangles in index order, so painter's order is
   preserved **by submission order** — no z-fighting even at `z = 0`. An optional
   **`z_spacing`** property (per-slot incremental local-Z, à la Unity "Z Spacing",
   default small/zero) additionally separates slots in the depth buffer for:
   occlusion with 3D world geometry, depth-prepass, and steep billboard tilt.
   Occlusion *by opaque 3D geometry* works regardless, via normal depth testing.
4. **Buffer strategy.** Full rebuild of CPU scratch buffers each frame pushed to
   the RS mesh, using the same dynamic-update region path
   (`mesh_surface_update_vertex_region`) as `SpineMesh2D` for the same-topology
   case, recreating surfaces when topology changes.
5. **AABB / culling.** Compute the mesh AABB from the frame's vertex bounds
   (`mesh_set_custom_aabb`); when billboard is enabled, expand to a
   symmetric/spherical AABB so the rotating instance is not wrongly culled.

## Coordinate mapping, scale & billboard

- **Units.** `pixel_size` property (default `0.01`, like `Sprite3D`):
  3D vertex = `(x, y) * pixel_size`.
- **Axis handling.** The module globally sets `spine::Bone::setYDown(true)`
  (shared with 2D, cannot change per node), so the builder **negates Y**:
  `pos = Vector3(x * pixel_size, -y * pixel_size, z)`. The skeleton stands upright
  in the XY plane, front-facing **+Z**, with `z` from `z_spacing`.
- **Billboard.** Three modes (`Disabled` / `Enabled` / `Y-Billboard`) implemented
  **in the shader** vertex stage (rewriting `MODELVIEW_MATRIX` from
  `INV_VIEW_MATRIX`), the same technique `BaseMaterial3D` uses. The node's
  `Transform3D` stays authoritative for position; no per-frame CPU node rotation.
- **Flip.** `flip_h` / `flip_v` convenience properties (negate axis scale).

## Materials & shading

Blend mode and shading are Godot shader **`render_mode`s** (compile-time, not
uniforms), so a small **generated spatial-shader library** is built once and
cached in the statics singleton — the same pattern as the existing
`CanvasItemMaterial` defaults, just spatial shaders.

- **Variant matrix:** `{Normal, Additive, Multiply}` blend × `{unshaded, shaded}`
  shading × `{straight, premultiplied-alpha}` → ~6–12 small `ShaderMaterial`s.
  Screen blend stays unsupported. Each surface picks its variant from the slot's
  blend mode + the sprite's `shaded` flag + the atlas page's PMA flag.
  Shaders are **code-generated in C++** (matching the existing statics pattern).
- **Shader behavior:** sample page texture → multiply by interpolated vertex
  `COLOR` (the CPU-computed `skeleton·slot·attachment` tint) → apply billboard in
  vertex stage → set `ALPHA`. The **unshaded** variant uses
  `render_mode unshaded` (flat — the default, == 2D look). The **shaded** variant
  runs lighting and samples `normal_map`/`specular_map` from
  `SpineRendererObject` when present.
- **PMA:** respect the atlas page's premultiplied-alpha flag by selecting the
  matching variant — identical semantics to the 2D renderer.
- **Transparency & depth:** alpha-blend transparent, `depth_test` on (opaque 3D
  geometry occludes the skeleton), `depth_draw` off by default with a property to
  enable it; `render_priority` and `alpha_scissor_threshold` knobs exposed for
  sorting control.
- **Custom material overrides (parity with 2D):** per-blend-mode overrides on the
  node (`normal_material` / `additive_material` / `multiply_material` /
  `screen_material`) and per-slot overrides via `SpineSlotNode3D`; a slot with a
  custom material forces a batch break into its own surface.
  `GeometryInstance3D::material_override` also works globally.

**Documented caveat:** shaded + billboard is approximate (a flat camera-facing
card has no true surface normals); best paired with `Disabled`/`Y` billboard or
with normal maps.

## Feature parity pieces

- **`SpineBoneNode3D` / `SpineSlotNode3D`.** Child `Node3D`s following a bone/slot
  each frame, driven from the `world_transforms_changed` signal. The skeleton's
  2D affine bone transform `(a, b, c, d, worldX, worldY)` is lifted into a
  `Transform3D` in the XY plane: translation `(x·pixel_size, -y·pixel_size,
  slot_z)`, basis from the 2D rotation/scale columns with Y negated, Z as plane
  normal. The sprite exposes `get_global_bone_transform_3d(name)` /
  `set_global_bone_transform_3d(name, xform)` (3D analog of the existing
  `Transform2D` accessors). `SpineSlotNode3D` also carries per-slot material
  overrides and uses the slot's draw-order `z`.
- **Editor preview.** `preview_skin` / `preview_animation` / `preview_frame` /
  `preview_time` from `SpineSprite` (`_get_property_list`/`_get`/`_set`/
  `update_preview_animation`) is editor-only and rendering-agnostic — ports
  verbatim, scrubbing animations in the 3D viewport.
- **Debug rendering.** Same toggle set as 2D (root / bones / regions / meshes /
  bounding boxes / paths / clipping + colors + bone thickness), built as a
  separate unshaded, no-depth-test **line-list mesh surface** drawn over the
  skeleton, generated from the same debug geometry the 2D `draw()` produces,
  projected into the XY plane.

## Error handling & edge cases

Handled as the 2D path already does:

- No / not-loaded `skeleton_data_res` → render nothing, no crash.
- Slot with no attachment or inactive bone → skipped, `clipEnd`.
- Empty clip result → skip.
- `!is_visible_in_tree()` → skip apply.
- Skeleton data hot-swap → `on_skeleton_data_changed` rebuilds the mesh and
  reconnects.
- Node teardown frees the mesh RID; shader/material statics released via the
  `clear_statics()` refcount, matching `SpineSprite`'s destructor.
- All `RenderingServer`/`RS`/type usages go through the existing
  `#ifdef SPINE_GODOT_EXTENSION` shims so both builds compile.

## Testing & validation

spine-godot is validated through the example project, not unit tests.

- Add a **3D demo scene** to `spine-godot/example/`: a `SpineSprite3D` in a 3D
  world with a floor and pillar (proving depth occlusion), one billboarded and
  one free-oriented instance, and a shaded instance with a light.
- Manual checklist: draw-order correctness, each blend mode, clipping, the three
  billboard modes, slot/bone node attachment, editor preview scrubbing, PMA vs
  straight atlases, and a perf sanity check vs `SpineSprite` on the same skeleton.
- Visual parity check against the equivalent 2D scene.

## Build & registration

- New `.cpp`/`.h` files in `spine_godot/` compile automatically (the `*.cpp`
  glob in `SCsub`) for module and GDExtension.
- Add `GDREGISTER_CLASS(SpineSprite3D)`, `GDREGISTER_CLASS(SpineSlotNode3D)`,
  `GDREGISTER_CLASS(SpineBoneNode3D)` in `register_types.cpp` alongside the
  existing registrations; release statics in the module's uninitialize path.
- Godot 4.x only; no 3.5 branch.

## Open implementation details (to resolve during planning)

- Exact surface/material assignment call on `GeometryInstance3D`
  (`instance_set_surface_override_material` vs embedding in the mesh surface).
- Whether the generated shaders are emitted as strings in C++ (default) or shipped
  as `.gdshader` resources.
- Precise `z_spacing` default and whether it scales with `pixel_size`.
- Billboard AABB expansion factor.
