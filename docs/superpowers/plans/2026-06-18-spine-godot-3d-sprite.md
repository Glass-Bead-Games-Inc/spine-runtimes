# SpineSprite3D Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native `SpineSprite3D` node that renders Spine skeletons in Godot 4.x 3D scenes (Sprite3D-style), reusing the existing spine-godot data/animation layer.

**Architecture:** A `SpineSprite3D : GeometryInstance3D` reuses the entire data/animation layer (`SpineSkeleton`, `SpineAnimationState`, `SpineSkeletonDataResource`, `SkeletonClipping`) and replaces only the rendering layer. The skeleton is rendered as one self-managed mesh (one instance), split into surfaces by *(texture page, blend mode, custom material)* batch. Draw order is preserved by surface/triangle submission order, with an optional `z_spacing` for depth separation. Materials come from a small code-generated spatial-shader library (blend × shading × premultiplied-alpha) with in-shader billboard.

**Tech Stack:** C++ (Godot module + GDExtension via godot-cpp), Godot 4.x `RenderingServer` 3D mesh API, Godot spatial shaders, spine-cpp.

## Global Constraints

- Godot **4.x only**; no Godot 3.5 path. (Dev branch target: `4.3-stable`.)
- Must compile in **both** the engine-module build and the **GDExtension** build. Every `RenderingServer`/`RS` and Godot type usage goes through the existing `#ifdef SPINE_GODOT_EXTENSION` shims used elsewhere in `spine_godot/`.
- **License header**: every new `.h`/`.cpp` starts with the exact Spine Runtimes License Agreement header block copied verbatim from `spine-godot/spine_godot/SpineSprite.h` lines 1-28.
- New `.cpp` files compile automatically via the `*.cpp` glob in `spine-godot/spine_godot/SCsub`; **no SCsub edit needed**. Wiring = add the file + a `GDREGISTER_CLASS` line in `register_types.cpp`.
- Reuse, do not duplicate: skeleton/animation/atlas/clipping classes are shared with `SpineSprite` unchanged.
- Unsupported (same as 2D, do not implement): two-color tinting, screen blend mode.
- Naming mirrors 2D: `SpineSprite3D`, `SpineSlotNode3D`, `SpineBoneNode3D`.

## Build / Verify Commands (used by every task)

These run from `spine-godot/build/` in Git Bash. Setup is one-time; build+run repeats per task.

- One-time module setup (if `spine-godot/godot/` does not exist):
  `./setup.sh 4.3-stable true`
- Build the module editor binary:
  `./build-v4.sh`
  Expected: scons finishes with `scons: done building targets.` and produces `spine-godot/godot/bin/godot.windows.editor.dev.x86_64.exe`.
- Run the example project for visual checks:
  `../godot/bin/godot.windows.editor.dev.x86_64.exe --path ../example`
- One-time GDExtension compile check (run once at Task 1 and again at Task 12; confirms both build configs compile):
  `./setup-extension.sh 4.3-stable true` then
  `cd ../godot-cpp && scons target=editor` is **not** needed; instead build the extension lib with the project's existing flow — the only requirement this plan enforces is that the code uses the `#ifdef SPINE_GODOT_EXTENSION` shims so the GDExtension translation unit compiles. A fast proxy check is to grep that no new raw `RenderingServer::get_singleton()` call is left outside an `#ifdef` (see Task 1 Step).

> Verification model: spine-godot has **no unit tests**; it is validated by building and observing the example project. Each task below ends with a concrete build + visual check + commit. "Expected" describes exactly what to see.

## File Structure

| File | Responsibility |
|---|---|
| `spine-godot/spine_godot/SpineSprite3D.h` | `SpineSprite3D` declaration: data/animation members, mesh RID, properties (pixel_size, z_spacing, billboard, shaded, flips, materials, debug, preview), method decls. |
| `spine-godot/spine_godot/SpineSprite3D.cpp` | Implementation: update loop, batch builder, mesh/AABB management, statics (shader library), property bindings, preview, debug. |
| `spine-godot/spine_godot/SpineSlotNode3D.h` / `.cpp` | `SpineSlotNode3D : Node3D`: follows a slot in 3D; per-slot material overrides. |
| `spine-godot/spine_godot/SpineBoneNode3D.h` / `.cpp` | `SpineBoneNode3D : Node3D`: follows a bone in 3D. |
| `spine-godot/spine_godot/register_types.cpp` | Add `GDREGISTER_CLASS` lines (modify). |
| `spine-godot/example/spine_sprite_3d.tscn` | 3D demo scene for validation (created in Task 12). |
| `spine-godot/example/SpineSprite3DDemo.gd` | Demo script wiring animations (Task 12). |

---

### Task 1: Scaffold `SpineSprite3D` (compiles, registers, animates, renders nothing)

Establish the node with the full data/animation layer ported from `SpineSprite`, extending `GeometryInstance3D`, but with **no geometry output yet**. Deliverable: you can drop a `SpineSprite3D` into a 3D scene, assign skeleton data, see animation state advance (signals fire, no crash), and nothing is drawn.

**Files:**
- Create: `spine-godot/spine_godot/SpineSprite3D.h`
- Create: `spine-godot/spine_godot/SpineSprite3D.cpp`
- Modify: `spine-godot/spine_godot/register_types.cpp`

**Interfaces:**
- Consumes: `SpineSkeletonDataResource`, `SpineSkeleton`, `SpineAnimationState`, `SpineConstant::UpdateMode`, `spine::AnimationStateListenerObject` — all existing.
- Produces (later tasks rely on these exact signatures):
  - `class SpineSprite3D : public GeometryInstance3D, public spine::AnimationStateListenerObject`
  - `void update_skeleton(float delta);`
  - `void on_skeleton_data_changed();`
  - `Ref<SpineSkeleton> get_skeleton();`
  - `Ref<SpineAnimationState> get_animation_state();`
  - `void build_meshes();` (declared now, empty body; Task 2 fills it)
  - Members: `Ref<SpineSkeletonDataResource> skeleton_data_res; Ref<SpineSkeleton> skeleton; Ref<SpineAnimationState> animation_state; SpineConstant::UpdateMode update_mode; float time_scale; spine::SkeletonClipping *skeleton_clipper; bool modified_bones;`

- [ ] **Step 1: Create `SpineSprite3D.h`**

Header guard `#pragma once`, license header (copy lines 1-28 from `SpineSprite.h`). Include block mirroring `SpineSprite.h` but using the 3D base class:

```cpp
#pragma once

#include "SpineSkeleton.h"
#include "SpineAnimationState.h"
#include "SpineConstant.h"
#ifdef SPINE_GODOT_EXTENSION
#include "SpineCommon.h"
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/templates/vector.hpp>
#else
#include "scene/3d/visual_instance_3d.h" // declares GeometryInstance3D
#endif

#include <spine/SkeletonClipping.h>

class SpineSlotNode3D;

class SpineSprite3D : public GeometryInstance3D, public spine::AnimationStateListenerObject {
	GDCLASS(SpineSprite3D, GeometryInstance3D)

protected:
	Ref<SpineSkeletonDataResource> skeleton_data_res;
	Ref<SpineSkeleton> skeleton;
	Ref<SpineAnimationState> animation_state;
	SpineConstant::UpdateMode update_mode;
	float time_scale;
	spine::SkeletonClipping *skeleton_clipper;
	bool modified_bones;

	RID mesh; // owned RS mesh, created in Task 2

	static void _bind_methods();
	void _notification(int what);

	void callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event) override;

	void build_meshes(); // Task 2 implements; empty for now

public:
	SpineSprite3D();
	~SpineSprite3D();

	void set_skeleton_data_res(const Ref<SpineSkeletonDataResource> &res);
	Ref<SpineSkeletonDataResource> get_skeleton_data_res();
	Ref<SpineSkeleton> get_skeleton();
	Ref<SpineAnimationState> get_animation_state();
	void on_skeleton_data_changed();
	void update_skeleton(float delta);

	SpineConstant::UpdateMode get_update_mode();
	void set_update_mode(SpineConstant::UpdateMode v);
	float get_time_scale();
	void set_time_scale(float v);

	static void clear_statics();
};
```

- [ ] **Step 2: Create `SpineSprite3D.cpp` (scaffold)**

License header, then port the **non-rendering** logic from `SpineSprite.cpp`. Concretely:
- Copy `callback(...)` verbatim from `SpineSprite.cpp` (the signal-emitting `AnimationStateListenerObject::callback`).
- Implement `on_skeleton_data_changed()` by copying `SpineSprite::on_skeleton_data_changed` (lines ~568-605) but **delete** the `generate_meshes_for_slots(skeleton)` call and the `remove_meshes()` call (no per-slot nodes here). Keep skeleton/animation_state creation, the `skeleton_data_changed` connect, and `update(0)/apply/update_world_transform`.
- Implement `update_skeleton(float delta)` by copying `SpineSprite::update_skeleton` (lines ~822-845) but **replace** the trailing `sort_slot_nodes(); update_meshes(skeleton); queue_redraw();` with a single call to `build_meshes();`.
- `_notification`: copy `SpineSprite::_notification` (lines ~669-691) but **remove** the `NOTIFICATION_DRAW` case (3D has no canvas draw). Keep `READY`, `INTERNAL_PROCESS`, `INTERNAL_PHYSICS_PROCESS`.
- Constructor: init `update_mode(SpineConstant::UpdateMode_Process), time_scale(1.0), skeleton_clipper(new spine::SkeletonClipping()), modified_bones(false)`.
- Destructor: `delete skeleton_clipper;` and `if (mesh.is_valid()) RS::get_singleton()->free_rid(mesh);` (use `free_rid` under `SPINE_GODOT_EXTENSION`, `free` otherwise — match the `#ifdef` pattern in `SpineMesh2D::~SpineMesh2D`, `SpineSprite.h` lines 102-118).
- `build_meshes()`: empty body `{}` for now.
- Getters/setters for `skeleton_data_res`, `update_mode`, `time_scale`: copy from `SpineSprite`.
- `_bind_methods()`: bind `set/get_skeleton_data_res`, `get_skeleton`, `get_animation_state`, `on_skeleton_data_changed`, `set/get_update_mode`, `set/get_time_scale`, `update_skeleton`. Add the same 6 animation signals + `before_*`/`world_transforms_changed`/`_internal_spine_objects_invalidated` signals as `SpineSprite::_bind_methods` (copy the `ADD_SIGNAL` block, lines ~466-489). Add properties:

```cpp
ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skeleton_data_res", PROPERTY_HINT_RESOURCE_TYPE, "SpineSkeletonDataResource"),
             "set_skeleton_data_res", "get_skeleton_data_res");
ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "time_scale"), "set_time_scale", "get_time_scale");
```

- `clear_statics()`: empty body for now (Task 2 adds the statics singleton teardown).

- [ ] **Step 3: Register the class**

In `register_types.cpp`, after `GDREGISTER_CLASS(SpineSprite);` (line ~148) add:

```cpp
GDREGISTER_CLASS(SpineSprite3D);
```

And add `#include "SpineSprite3D.h"` near the other includes (after `#include "SpineSprite.h"`, line ~39). In the uninitialize paths where `SpineSprite::clear_statics();` is called (lines ~235, ~248), add `SpineSprite3D::clear_statics();` next to it.

- [ ] **Step 4: Build**

Run: `cd spine-godot/build && ./build-v4.sh`
Expected: `scons: done building targets.`, no errors referencing `SpineSprite3D`.

- [ ] **Step 5: GDExtension shim sanity check**

Run: `grep -rn "RenderingServer::get_singleton\|RS::get_singleton" spine-godot/spine_godot/SpineSprite3D.cpp`
Expected: every match sits inside an `#ifdef SPINE_GODOT_EXTENSION` / `#else` branch (no raw 4.x-module-only call leaks into the shared path). At this task there should be only the destructor's `free_rid`/`free` pair, already guarded.

- [ ] **Step 6: Visual check**

Run: `../godot/bin/godot.windows.editor.dev.x86_64.exe --path ../example`
In the editor: create a new 3D scene, add a `SpineSprite3D` node (it appears in the Create Node dialog), assign an existing `.spine` skeleton data resource from `example/assets/` to `Skeleton Data Res`, set an animation via the (not-yet-present preview — instead) a temporary script `get_animation_state().set_animation("walk", true, 0)` in `_ready`.
Expected: scene runs, no crash, no errors in Output; nothing is drawn (no geometry yet). Confirm via a `print(get_skeleton())` that the skeleton is non-null.

- [ ] **Step 7: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp spine-godot/spine_godot/register_types.cpp
git commit -m "[spine-godot] Scaffold SpineSprite3D node (data/animation layer, no rendering)"
```

---

### Task 2: Combined mesh + minimal unshaded rendering

Build the batch builder and the statics shader library (one variant: unshaded, normal blend, straight alpha). Render `RegionAttachment` and `MeshAttachment` as one mesh, Y-flipped into the XY plane, with per-vertex tint and submission-order draw ordering. No clipping, no blend modes, no billboard yet.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h`
- Modify: `spine-godot/spine_godot/SpineSprite3D.cpp`

**Interfaces:**
- Consumes: `build_meshes()` (empty from Task 1), `mesh` RID, `skeleton`, `skeleton_clipper`.
- Produces:
  - A `SpineSprite3DStatics` singleton exposing `Ref<ShaderMaterial> get_material(spine::BlendMode blend, bool shaded, bool pma);` (Task 2 only fills `{Normal,false,false}`; later tasks fill the rest).
  - Member `float pixel_size;` default `0.01` (exposed in Task 3; field added now).
  - Member `float z_spacing;` default `0.0` (exposed in Task 3; field added now).
  - Scratch buffers `PackedVector3Array scratch_positions; PackedVector2Array scratch_uvs; PackedColorArray scratch_colors; PackedInt32Array scratch_indices;` (use the `Vector<...>` variants under non-extension via the existing `#ifdef SPINE_GODOT_EXTENSION` pattern from `SpineMesh2D`).

- [ ] **Step 1: Add the statics singleton**

In `SpineSprite3D.cpp`, above the class methods, add `SpineSprite3DStatics` modeled on `SpineSpriteStatics` (`SpineSprite.cpp` lines 96-153): a singleton with `instance()`, `clear()`, holding the quad index array and a material cache. For Task 2 it builds exactly one material via a helper `make_material(spine::BlendMode, bool shaded, bool pma)` that creates a `Ref<Shader>` from generated source and a `Ref<ShaderMaterial>` referencing it. Implement the generator `String build_shader_source(spine::BlendMode blend, bool shaded, bool pma)` returning, for `{Normal,false,false}`:

```glsl
shader_type spatial;
render_mode blend_mix, cull_disabled, unshaded, depth_draw_opaque, shadows_disabled;

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap;

void vertex() {
    // billboard inserted in Task 5; identity for now
}

void fragment() {
    vec4 tex = texture(albedo_tex, UV);
    ALBEDO = tex.rgb * COLOR.rgb;
    ALPHA = tex.a * COLOR.a;
}
```

Cache key: `blend * 4 + shaded * 2 + pma`. `make_material` sets `render_priority` later; for now leave default. Store materials in `Ref<ShaderMaterial> materials[ ... ]`. `clear()` unrefs them. Wire `SpineSprite3D::clear_statics()` to call `SpineSprite3DStatics::clear()`.

- [ ] **Step 2: Implement `build_meshes()` — batch builder**

Port the per-attachment vertex computation from `SpineSprite::update_meshes` (`SpineSprite.cpp` lines 847-960) but accumulate into the shared scratch buffers and emit `Vector3`. Pseudocode of the real implementation:

```cpp
void SpineSprite3D::build_meshes() {
	if (mesh.is_valid()) { RS::get_singleton()->free_rid(mesh); mesh = RID(); } // simple full rebuild for Task 2
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;
	spine::Skeleton *sk = skeleton->get_spine_object();
	auto statics = SpineSprite3DStatics::instance();

	mesh = RS::get_singleton()->mesh_create();
	AABB aabb;
	bool aabb_init = false;
	int surface_index = 0;

	// One surface per contiguous run sharing the same texture page.
	SpineRendererObject *current_ro = nullptr;
	// scratch buffers reset
	auto flush = [&]() {
		if (scratch_indices.size() == 0) return;
		Array arrays; arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = scratch_positions;
		arrays[Mesh::ARRAY_TEX_UV] = scratch_uvs;
		arrays[Mesh::ARRAY_COLOR]  = scratch_colors;
		arrays[Mesh::ARRAY_INDEX]  = scratch_indices;
		RS::get_singleton()->mesh_add_surface_from_arrays(mesh, RS::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
			RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE);
		Ref<ShaderMaterial> mat = statics.get_material(spine::BlendMode_Normal, false, false);
		mat->set_shader_parameter("albedo_tex", current_ro->texture);
		RS::get_singleton()->mesh_surface_set_material(mesh, surface_index, mat->get_rid());
		surface_index++;
		// reset scratch
		scratch_positions.clear(); scratch_uvs.clear(); scratch_colors.clear(); scratch_indices.clear();
	};

	for (int i = 0, n = (int) sk->getSlots().size(); i < n; i++) {
		spine::Slot *slot = sk->getDrawOrder().getAppliedPose()[i];
		spine::Attachment *att = slot->getAppliedPose().getAttachment();
		if (!att || !slot->getBone().isActive()) continue;

		// compute world verts/uvs/indices + renderer_object exactly as SpineSprite::update_meshes,
		// for RegionAttachment and MeshAttachment. tint = skeleton.color * slot.color * attachment.color.
		// (Copy that block; it produces: float* worldVerts (xy pairs), uvs, unsigned short* indices, SpineRendererObject* ro, tint)

		if (current_ro && ro != current_ro) flush();
		current_ro = ro;

		int base = scratch_positions.size();
		float z = -((float) i) * z_spacing; // per-slot depth; 0 in Task 2
		for (int v = 0; v < numVerts; v++) {
			float x = worldVerts[v*2] * pixel_size;
			float y = -worldVerts[v*2+1] * pixel_size; // Y-flip: spine Y-down -> Godot Y-up
			scratch_positions.push_back(Vector3(x, y, z));
			scratch_uvs.push_back(Vector2(uvs[v*2], uvs[v*2+1]));
			scratch_colors.push_back(Color(tint.r, tint.g, tint.b, tint.a));
			if (!aabb_init) { aabb.position = Vector3(x,y,z); aabb.size = Vector3(); aabb_init = true; }
			else aabb.expand_to(Vector3(x,y,z));
		}
		for (int t = 0; t < numIndices; t++) scratch_indices.push_back(base + indices[t]);
	}
	flush();

	RS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);
	set_base(mesh);
}
```

Notes: use `pixel_size` field (default `0.01`) and `z_spacing` (default `0`) added as members in this task's header step. Guard `RS::get_singleton()->free_rid` vs `free` with the `#ifdef`. Use the existing scratch `spine::Array<float>` (`SpineSpriteStatics::scratch_vertices`) approach for `computeWorldVertices`, or a local `spine::Array<float>` member.

- [ ] **Step 3: Add fields to header**

In `SpineSprite3D.h` add `float pixel_size; float z_spacing;` and the four scratch buffers (under the `#ifdef SPINE_GODOT_EXTENSION` Packed*/Vector pattern). Initialize `pixel_size(0.01f), z_spacing(0.0f)` in the constructor initializer list.

- [ ] **Step 4: Build**

Run: `cd spine-godot/build && ./build-v4.sh`
Expected: `scons: done building targets.`

- [ ] **Step 5: Visual check**

Run the example (same command as Task 1 Step 6). With the temp `_ready` script setting a walk animation:
Expected: the skeleton renders in the 3D viewport as a flat figure in the XY plane, **upright** (not upside-down — confirms the Y-flip), facing +Z, animating, with correct front-to-back layering of body parts (confirms submission-order draw order) and correct colors/tint. Rotating the camera around it shows a flat plane.

- [ ] **Step 6: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: combined-mesh unshaded rendering"
```

---

### Task 3: `pixel_size`, `z_spacing`, `flip_h`/`flip_v` properties

Expose scaling/depth/flip controls and verify depth interaction with 3D geometry.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h`
- Modify: `spine-godot/spine_godot/SpineSprite3D.cpp`

**Interfaces:**
- Produces: `void set_pixel_size(float); float get_pixel_size(); void set_z_spacing(float); float get_z_spacing(); void set_flip_h(bool); bool get_flip_h(); void set_flip_v(bool); bool get_flip_v();` and members `bool flip_h; bool flip_v;`.

- [ ] **Step 1: Add fields + accessors**

Header: add `bool flip_h; bool flip_v;` (init `false`). Add the 8 getters/setters. Each setter stores then calls `build_meshes()` if `skeleton.is_valid()` (so editor updates live).

- [ ] **Step 2: Apply flips in the builder**

In `build_meshes()`, replace the x/y mapping:

```cpp
float sx = flip_h ? -pixel_size : pixel_size;
float sy = flip_v ? pixel_size : -pixel_size; // base is -pixel_size (Y-flip); flip_v cancels it
float x = worldVerts[v*2] * sx;
float y = worldVerts[v*2+1] * sy;
```

- [ ] **Step 3: Bind properties**

In `_bind_methods`, bind the 8 methods and add:

```cpp
ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "pixel_size", PROPERTY_HINT_RANGE, "0.0001,1,0.0001"), "set_pixel_size", "get_pixel_size");
ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "z_spacing", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_z_spacing", "get_z_spacing");
ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_h"), "set_flip_h", "get_flip_h");
ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_v"), "set_flip_v", "get_flip_v");
```

- [ ] **Step 4: Build**

Run: `cd spine-godot/build && ./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 5: Visual check**

Run the example. Add a `BoxMesh` `MeshInstance3D` and a `Camera3D` to the scene. Place the box partly in front of the skeleton (smaller Z than parts of it):
Expected: changing `pixel_size` scales the skeleton; toggling `flip_h` mirrors it; setting `z_spacing` to e.g. `0.001` and enabling the box overlap shows the box correctly occluding the back of the skeleton (depth test works), while the skeleton's own parts keep correct order.

- [ ] **Step 6: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: pixel_size, z_spacing, flip properties"
```

---

### Task 4: Blend modes + premultiplied-alpha + shader library

Generate the full unshaded shader matrix and break batches on *(page, blend mode, PMA)*. Shaded variants come in Task 6.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.cpp`

**Interfaces:**
- Produces: `SpineSprite3DStatics::get_material` now returns valid materials for `blend ∈ {Normal, Additive, Multiply}`, `shaded == false`, `pma ∈ {false, true}`.

- [ ] **Step 1: Extend `build_shader_source`**

Map blend → `render_mode` and PMA → fragment output:

```cpp
String rm; // render_mode blend token
switch (blend) {
  case spine::BlendMode_Additive: rm = "blend_add"; break;
  case spine::BlendMode_Multiply: rm = "blend_mul"; break;
  default: rm = "blend_mix"; break; // Normal (Screen unsupported)
}
String shading = shaded ? "" : "unshaded, ";
String frag = pma
  ? "vec4 tex = texture(albedo_tex, UV); vec3 c = tex.rgb * COLOR.rgb; ALBEDO = c; ALPHA = tex.a * COLOR.a;"
  : "vec4 tex = texture(albedo_tex, UV); ALBEDO = tex.rgb * COLOR.rgb; ALPHA = tex.a * COLOR.a;";
```

For PMA + additive/multiply the premultiplied color must not be re-divided; since spine PMA atlases already store `rgb*a`, with `blend_add`/`blend_mul` the above `ALBEDO=tex.rgb*COLOR.rgb, ALPHA=tex.a*COLOR.a` plus the render_mode reproduces the 2D result. Build all `3 blend × 2 pma` unshaded variants lazily on first `get_material` call (cache by key).

- [ ] **Step 2: Batch break on blend mode + PMA + page**

In `build_meshes()`, compute `spine::BlendMode blend = slot->getData().getBlendMode();` and read the page PMA flag from the atlas page (`((spine::AtlasRegion*)region)->getPage()->pma` — confirm the accessor name against `spine::AtlasPage`; it exposes `pma`). Flush the current surface whenever `ro`, `blend`, or `pma` changes. In `flush()`, select `statics.get_material(blend, false, pma)` instead of the hardcoded Normal.

- [ ] **Step 3: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 4: Visual check**

Run the example with a skeleton that uses additive/multiply slots (e.g. `assets/` has skeletons with glow/additive parts such as `raptor` or `owl`):
Expected: additive parts glow correctly against a dark background; multiply parts darken; a PMA-exported atlas renders identically to the same skeleton in a `SpineSprite` 2D scene (compare side by side).

- [ ] **Step 5: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: blend modes + premultiplied-alpha shader matrix"
```

---

### Task 5: Billboard modes (in-shader)

Add `billboard` enum (`Disabled`/`Enabled`/`Y-Billboard`) implemented in the vertex stage, plus AABB expansion so billboarded sprites aren't wrongly culled.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h`
- Modify: `spine-godot/spine_godot/SpineSprite3D.cpp`

**Interfaces:**
- Produces: `enum BillboardMode { BILLBOARD_DISABLED, BILLBOARD_ENABLED, BILLBOARD_Y };` (declared in `SpineSprite3D`), `void set_billboard(BillboardMode); BillboardMode get_billboard();`, member `BillboardMode billboard;`. The billboard mode is a **shader uniform** `billboard_mode` (int) shared across the material library.

- [ ] **Step 1: Add billboard to the shader vertex stage**

Replace the empty `vertex()` with the standard Godot billboard transform driven by a uniform:

```glsl
uniform int billboard_mode = 0; // 0 disabled, 1 enabled, 2 y

void vertex() {
    if (billboard_mode == 1) {
        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(
            INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2],
            MODEL_MATRIX[3]);
        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
    } else if (billboard_mode == 2) {
        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(
            vec4(normalize(cross(vec3(0.0,1.0,0.0), INV_VIEW_MATRIX[2].xyz)), 0.0),
            vec4(0.0,1.0,0.0,0.0),
            vec4(normalize(cross(INV_VIEW_MATRIX[0].xyz, vec3(0.0,1.0,0.0))), 0.0),
            MODEL_MATRIX[3]);
        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
    }
}
```

This is the same construction `BaseMaterial3D` emits for its billboard modes. Add `uniform int billboard_mode` to every generated variant.

- [ ] **Step 2: Push the uniform**

In `flush()` (or once per build), set `mat->set_shader_parameter("billboard_mode", (int) billboard)` on each surface material. Since materials are shared in statics, set it on the shared material each build (cheap) — acceptable because all surfaces of one sprite share the billboard mode; if multiple `SpineSprite3D` instances differ, set it per-instance via `RS::instance_geometry_set_shader_parameter(get_instance(), "billboard_mode", billboard)` instead (per-instance uniform overrides the material). Use the per-instance path to avoid cross-instance interference.

- [ ] **Step 3: AABB expansion for billboard**

After computing `aabb`, if `billboard != BILLBOARD_DISABLED`, replace it with a cube centered on the local origin sized to the max extent so rotation never culls it:

```cpp
if (billboard != BILLBOARD_DISABLED) {
    float r = MAX(aabb.size.x, MAX(aabb.size.y, aabb.size.z));
    Vector3 c = aabb.position + aabb.size * 0.5;
    aabb = AABB(c - Vector3(r,r,r), Vector3(2*r,2*r,2*r));
}
```

- [ ] **Step 4: Bind enum + property**

Header: declare `enum BillboardMode`. In `_bind_methods`, `BIND_ENUM_CONSTANT(BILLBOARD_DISABLED/ENABLED/Y)` and:

```cpp
ADD_PROPERTY(PropertyInfo(Variant::INT, "billboard", PROPERTY_HINT_ENUM, "Disabled,Enabled,Y-Billboard"), "set_billboard", "get_billboard");
```

Setter stores + rebuilds.

- [ ] **Step 5: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 6: Visual check**

Run the example with a `Camera3D` you can orbit (or rotate the skeleton):
Expected: `Disabled` → flat plane keeps its world orientation; `Enabled` → always faces the camera as you orbit; `Y-Billboard` → yaws to face camera horizontally but stays upright when the camera tilts up/down. No popping/culling when facing edge-on.

- [ ] **Step 7: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: in-shader billboard modes"
```

---

### Task 6: Shaded mode + normal/specular maps

Add the `shaded` toggle and shaded shader variants that participate in lighting and sample `normal_map`/`specular_map`.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h`
- Modify: `spine-godot/spine_godot/SpineSprite3D.cpp`

**Interfaces:**
- Produces: `void set_shaded(bool); bool get_shaded();`, member `bool shaded;` (default `false`). `get_material` now serves `shaded == true` variants.

- [ ] **Step 1: Shaded shader source**

When `shaded`, drop `unshaded` from `render_mode`, add normal/specular uniforms and sampling:

```glsl
uniform sampler2D albedo_tex : source_color, filter_linear_mipmap;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap;
uniform sampler2D specular_tex : source_color, filter_linear_mipmap;
uniform bool use_normal_tex = false;
uniform bool use_specular_tex = false;

void fragment() {
    vec4 tex = texture(albedo_tex, UV);
    ALBEDO = tex.rgb * COLOR.rgb;
    ALPHA = tex.a * COLOR.a;
    if (use_normal_tex) NORMAL_MAP = texture(normal_tex, UV).rgb;
    if (use_specular_tex) SPECULAR = texture(specular_tex, UV).r;
}
```

(For PMA shaded, mirror Task 4's ALBEDO/ALPHA handling.) Build the `3 blend × 2 pma` shaded variants on demand.

- [ ] **Step 2: Feed maps in `flush()`**

When `shaded`, set `mat->set_shader_parameter("normal_tex", current_ro->normal_map)` and `use_normal_tex` = `current_ro->normal_map.is_valid()`, same for specular. Select `statics.get_material(blend, shaded, pma)`.

- [ ] **Step 3: Bind property + enable shadows when shaded**

Header field `bool shaded`. The shaded variants must allow shadows — generate them **without** `shadows_disabled` and with `cull_disabled`. Bind:

```cpp
ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shaded"), "set_shaded", "get_shaded");
```

Setter stores + rebuilds.

- [ ] **Step 4: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 5: Visual check**

Run the example with a `DirectionalLight3D` and `shaded = true` on a skeleton with a normal map atlas (or any atlas — albedo still lights):
Expected: with `shaded` off the figure is full-bright (== earlier tasks); with `shaded` on it darkens/brightens as the light direction changes; with a normal-map atlas, surface relief responds to the light. Confirm the documented caveat: under `Enabled` billboard the lighting is flat/approximate (acceptable).

- [ ] **Step 6: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: shaded mode + normal/specular maps"
```

---

### Task 7: Clipping

Port `SkeletonClipping` so `ClippingAttachment` masks geometry, matching 2D.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.cpp`

**Interfaces:** No new public API; uses existing `skeleton_clipper`.

- [ ] **Step 1: Port clip logic into the builder**

In `build_meshes()`, replicate the clip handling from `SpineSprite::update_meshes` (`SpineSprite.cpp` lines 907-926): on `ClippingAttachment` call `skeleton_clipper->clipStart(...)` and `continue`; for region/mesh, if `skeleton_clipper->isClipping()` call `clipTriangles(vertices, indices, uvs, 2)`, swap to clipped vertices/uvs/indices (these are 2-float xy), skip if empty; call `skeleton_clipper->clipEnd(*slot)` for slots that end clipping and `clipEnd2()`... — match the exact call sequence in the 2D code, including `clipEnd(*slot)` on skipped/inactive slots and a final `skeleton_clipper->clipEnd()` after the loop. Because clipped output is xy-interleaved like the unclipped path, the Vector3 mapping (pixel_size, Y-flip, z) is unchanged.

- [ ] **Step 2: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 3: Visual check**

Run the example with a skeleton that uses clipping (e.g. a mask/clip attachment in `assets/`):
Expected: geometry outside the clip polygon is removed exactly as in the equivalent 2D `SpineSprite` scene; no leaked triangles; animating the clip region updates the mask each frame.

- [ ] **Step 4: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: clipping support"
```

---

### Task 8: Sprite-level custom materials

Add per-blend-mode material overrides on the sprite (parity with `SpineSprite`).

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h`
- Modify: `spine-godot/spine_godot/SpineSprite3D.cpp`

**Interfaces:**
- Produces: `void set_normal_material(Ref<Material>); Ref<Material> get_normal_material();` and the same for `additive_`, `multiply_`, `screen_` (screen accepted but inert, matching 2D's stored-but-unused screen path). Members `Ref<Material> normal_material, additive_material, multiply_material, screen_material;`.

- [ ] **Step 1: Add fields, accessors, bindings**

Mirror `SpineSprite`'s material property block (`SpineSprite.cpp` lines 420-427, 494-502): bind 8 methods, add 4 properties under an `ADD_GROUP("Materials", "")`.

- [ ] **Step 2: Use overrides in `flush()`**

When selecting the surface material, prefer the sprite override for the slot's blend mode if set; else the generated library material:

```cpp
Ref<Material> custom;
switch (blend) {
  case spine::BlendMode_Normal: custom = normal_material; break;
  case spine::BlendMode_Additive: custom = additive_material; break;
  case spine::BlendMode_Multiply: custom = multiply_material; break;
  default: break;
}
RID mat_rid = custom.is_valid() ? custom->get_rid() : statics.get_material(blend, shaded, pma)->get_rid();
// when custom is valid, do NOT set albedo_tex on it (user-owned); when library material, set textures as before
```

Note: a custom material won't get `albedo_tex` auto-set (it's the user's material); document that custom 3D materials should expose their own texture binding. This matches 2D behavior where custom materials replace the default.

- [ ] **Step 3: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 4: Visual check**

Run the example; assign a simple `ShaderMaterial`/`StandardMaterial3D` (e.g. tinted) to `normal_material`:
Expected: normal-blend surfaces render with the custom material; additive/multiply surfaces still use the library; clearing the override restores default.

- [ ] **Step 5: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: sprite-level custom materials"
```

---

### Task 9: `SpineSlotNode3D`, `SpineBoneNode3D`, and 3D bone transforms

Add child nodes that follow slots/bones in 3D, per-slot material overrides (with batch-break), and the `get/set_global_bone_transform_3d` accessors.

**Files:**
- Create: `spine-godot/spine_godot/SpineSlotNode3D.h` / `.cpp`
- Create: `spine-godot/spine_godot/SpineBoneNode3D.h` / `.cpp`
- Modify: `spine-godot/spine_godot/SpineSprite3D.h` / `.cpp`
- Modify: `spine-godot/spine_godot/register_types.cpp`

**Interfaces:**
- Produces:
  - `Transform3D SpineSprite3D::bone_to_transform3d(spine::Bone *bone, float slot_z) const;` — the lifting helper.
  - `Transform3D SpineSprite3D::get_global_bone_transform_3d(const String &bone_name);`
  - `void SpineSprite3D::set_global_bone_transform_3d(const String &bone_name, Transform3D xform);`
  - `class SpineSlotNode3D : public Node3D` with `set/get_slot_name`, `get_slot_index`, and `normal/additive/multiply/screen` material getters/setters (same shape as `SpineSlotNode`).
  - `class SpineBoneNode3D : public Node3D` with `set/get_bone_name`, `get_bone_index`.

- [ ] **Step 1: Implement the lifting helper**

In `SpineSprite3D.cpp`:

```cpp
Transform3D SpineSprite3D::bone_to_transform3d(spine::Bone *bone, float slot_z) const {
	// spine 2D affine: [a c worldX; b d worldY]. Y-down -> Godot Y-up: negate the y row.
	float a = bone->getA(), b = bone->getB(), c = bone->getC(), d = bone->getD();
	float wx = bone->getWorldX() * pixel_size;
	float wy = -bone->getWorldY() * pixel_size;
	Basis basis;
	basis.set_column(0, Vector3(a, -b, 0));               // X axis (y negated)
	basis.set_column(1, Vector3(-c, d, 0));               // Y axis (x of column negated for handedness)
	basis.set_column(2, Vector3(0, 0, 1));                // Z normal
	return Transform3D(basis, Vector3(wx, wy, slot_z));
}
```

(Validate the exact sign convention against the visual check in Step 6; adjust column signs if the attached node appears mirrored/rotated. The math mirrors how `SpineBone::get_global_transform` builds a `Transform2D` from `a,b,c,d,worldX,worldY`.)

`get_global_bone_transform_3d`: find bone by name on `skeleton`, return `get_global_transform() * bone_to_transform3d(bone, 0)`. `set_global_bone_transform_3d`: inverse-map translation/columns back into `bone->setA/B/C/D/WorldX/WorldY`, then set `modified_bones = true` (so the update loop re-runs world transform like `SpineSprite::set_global_bone_transform`).

- [ ] **Step 2: Create `SpineBoneNode3D`**

Model on `SpineSlotNode` (`SpineSlotNode.h/.cpp`) but extending `Node3D`, tracking a bone. It connects to the sprite's `world_transforms_changed` signal and on callback sets its own transform to `sprite->bone_to_transform3d(bone, 0)` (local to the sprite; set as local transform since it's a child of the sprite). Provide `set/get_bone_name`, `get_bone_index`. Resolve the parent `SpineSprite3D` via `cast_to<SpineSprite3D>(get_parent())`.

- [ ] **Step 3: Create `SpineSlotNode3D`**

Model on `SpineSlotNode` exactly, extending `Node3D`: `slot_name`, `slot_index`, the 4 material overrides, `_get_property_list`/`_get`/`_set` for the slot-name enum dropdown (copy from `SpineSlotNode.cpp`). On `world_transforms_changed`, set transform from the slot's bone via `bone_to_transform3d(bone, -slot_index * z_spacing)` so attached props sit at the slot's depth.

- [ ] **Step 4: Per-slot material batch-break in `SpineSprite3D::build_meshes()`**

Before building, collect slot→`SpineSlotNode3D` by scanning children (mirror `SpineSprite::sort_slot_nodes` minus the 2D draw-order reordering — in 3D we only need the lookup). In the builder, when a slot has a slot-node with a custom material for its blend mode, flush before and after so it becomes its own surface using that material (same override precedence as Task 8, slot-node first then sprite-level then library).

- [ ] **Step 5: Register classes**

`register_types.cpp`: `#include` both headers; add `GDREGISTER_CLASS(SpineSlotNode3D);` and `GDREGISTER_CLASS(SpineBoneNode3D);` after the 2D `SpineSlotNode`/`SpineBoneNode` registrations (line ~186).

- [ ] **Step 6: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 7: Visual check**

Run the example: add a `SpineBoneNode3D` as a child of the `SpineSprite3D`, set its bone to e.g. a hand bone, and add a small `MeshInstance3D` (cube) under it. Add a `SpineSlotNode3D` with a custom material.
Expected: the cube tracks the hand bone's position/rotation/scale through the animation in 3D (confirms the transform lift; fix column signs if mirrored); the slot-node's custom material affects only that slot.

- [ ] **Step 8: Commit**

```bash
git add spine-godot/spine_godot/SpineSlotNode3D.h spine-godot/spine_godot/SpineSlotNode3D.cpp spine-godot/spine_godot/SpineBoneNode3D.h spine-godot/spine_godot/SpineBoneNode3D.cpp spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp spine-godot/spine_godot/register_types.cpp
git commit -m "[spine-godot] SpineSprite3D: SpineSlotNode3D/SpineBoneNode3D + 3D bone transforms"
```

---

### Task 10: Editor preview

Port the inspector preview (skin/animation/frame/time) so animations can be scrubbed in-editor.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h` / `.cpp`

**Interfaces:**
- Produces: `_get_property_list`, `_get`, `_set` overrides and members `String preview_skin, preview_animation; bool preview_frame; float preview_time;`.

- [ ] **Step 1: Port preview machinery**

Copy `SpineSprite::_get_property_list` (lines 693-744), `_get` (746-767), `_set` (792-820), and the file-local `update_preview_animation` helper (769-790) into `SpineSprite3D`, unchanged except the class name. Add the `ADD_GROUP("Preview", "")` line at the end of `_bind_methods` (the properties are injected by `_get_property_list`). Initialize the four members in the constructor (`preview_skin("Default"), preview_animation("-- Empty --"), preview_frame(false), preview_time(0)`).

- [ ] **Step 2: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 3: Visual check**

Run the editor; select a `SpineSprite3D`, expand the **Preview** group, choose a skin and animation, toggle `preview_frame` and drag `preview_time`:
Expected: the skeleton updates live in the 3D viewport without running the scene; choosing `-- Empty --` resets to setup pose.

- [ ] **Step 4: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: editor animation preview"
```

---

### Task 11: Debug rendering

Draw bones/regions/meshes/bounding-boxes/paths/clipping as a 3D line overlay.

**Files:**
- Modify: `spine-godot/spine_godot/SpineSprite3D.h` / `.cpp`

**Interfaces:**
- Produces: the same debug toggle/color getters+setters as `SpineSprite` (`get/set_debug_bones`, `..._color`, `..._thickness`, regions/meshes/bounding_boxes/paths/clipping/root), backed by a second RS mesh `RID debug_mesh;` drawn as `PRIMITIVE_LINES`.

- [ ] **Step 1: Add debug fields + bindings**

Copy the full debug field set, getters/setters, and the `ADD_GROUP("Debug","")` property block from `SpineSprite.h`/`SpineSprite.cpp` (lines 146-160, 235-353, 432-461, 504-519). Add `RID debug_mesh;` freed in the destructor.

- [ ] **Step 2: Build the debug line mesh**

Add `void build_debug_mesh();` called at the end of `update_skeleton` after `build_meshes()`. It produces a `PRIMITIVE_LINES` surface from line segments, reusing the geometry the 2D `SpineSprite::draw`/`draw_bone` computes (bones as segments between bone world positions; regions/meshes as their edges; bounding boxes; paths; clipping polygons), each emitted as `Vector3(x*pixel_size, -y*pixel_size, z + epsilon)` with the corresponding debug color in `ARRAY_COLOR`. Use a single unshaded, `vertex_color_use_as_albedo`, no-depth-test `ShaderMaterial` from statics (one extra variant: `lines_material`). Bone thickness maps to nothing in 3D lines (lines are 1px); document that `bones_thickness` is ignored in 3D, or draw bones as thin quads if thickness > 1 (keep it as ignored for v1 and note it).

- [ ] **Step 3: Build** — `./build-v4.sh` — Expected: `scons: done building targets.`

- [ ] **Step 4: Visual check**

Run the example; enable `bones`, `regions`, `meshes`, `bounding_boxes`, `clipping` toggles:
Expected: colored overlays appear over the skeleton in the 3D viewport, aligned with the geometry, updating each frame; disabling all toggles removes the overlay.

- [ ] **Step 5: Commit**

```bash
git add spine-godot/spine_godot/SpineSprite3D.h spine-godot/spine_godot/SpineSprite3D.cpp
git commit -m "[spine-godot] SpineSprite3D: 3D debug rendering"
```

---

### Task 12: Example demo scene, validation pass, GDExtension check, docs

Add a demonstrative 3D scene, run the full manual checklist, confirm the GDExtension config compiles, and note the feature in the README.

**Files:**
- Create: `spine-godot/example/spine_sprite_3d.tscn`
- Create: `spine-godot/example/SpineSprite3DDemo.gd`
- Modify: `spine-godot/README.md`

- [ ] **Step 1: Build the demo scene**

In the running editor, create `spine_sprite_3d.tscn`: a `Node3D` root with a `Camera3D`, a `DirectionalLight3D`, a `MeshInstance3D` floor (`PlaneMesh`) and a `MeshInstance3D` pillar (`BoxMesh`) for occlusion, and three `SpineSprite3D` instances using an existing skeleton from `example/assets/`: (a) free-oriented, (b) `billboard = Enabled`, (c) `shaded = true` with `z_spacing > 0` positioned to intersect the pillar. Attach `SpineSprite3DDemo.gd` to set walk/idle animations in `_ready` via `get_animation_state().set_animation(...)`.

- [ ] **Step 2: Run the validation checklist**

Run: `../godot/bin/godot.windows.editor.dev.x86_64.exe --path ../example` and open `spine_sprite_3d.tscn`, press Play. Verify each, fixing regressions in the relevant task's file if any fail:
  - Draw order correct on all three instances.
  - Each blend mode renders correctly.
  - Clipping masks correctly.
  - Billboard `Disabled`/`Enabled`/`Y` behave per Task 5.
  - Shaded instance responds to the light; unshaded ones don't.
  - Pillar occludes the `z_spacing` instance correctly (depth).
  - A `SpineBoneNode3D` child prop tracks its bone.
  - Editor preview scrubs animations.
  - Performance: with the same skeleton, frame time is comparable to a `SpineSprite` 2D scene (no order-of-magnitude regression).

- [ ] **Step 3: GDExtension compile check**

Run: `cd spine-godot/build && ./setup-extension.sh 4.3-stable true` (one-time if `godot-cpp/` absent), then build the extension library per the repo's extension flow (`build-extension.sh` if present, else the documented godot-cpp scons invocation).
Expected: the GDExtension `.dll` builds with the new `SpineSprite3D`/`SpineSlotNode3D`/`SpineBoneNode3D` translation units, confirming the `#ifdef SPINE_GODOT_EXTENSION` shims are correct. Fix any unguarded `RenderingServer`/type usage flagged by the compiler.

- [ ] **Step 4: Document**

In `spine-godot/README.md`, under the feature description, add one line: `spine-godot provides SpineSprite (2D) and SpineSprite3D (3D) nodes; SpineSprite3D renders skeletons in 3D scenes with configurable billboard and optional lighting.`

- [ ] **Step 5: Commit**

```bash
git add spine-godot/example/spine_sprite_3d.tscn spine-godot/example/SpineSprite3DDemo.gd spine-godot/README.md
git commit -m "[spine-godot] Add SpineSprite3D example scene and docs"
```

---

## Self-Review Notes

- **Spec coverage:** node/class structure (T1), combined-mesh + submission-order rendering + AABB (T2), pixel_size/z_spacing/flip + depth interaction (T3), blend modes + PMA + shader library (T4), billboard modes (T5), shaded + normal/specular (T6), clipping (T7), custom materials sprite-level (T8) + per-slot/slot+bone nodes + 3D bone transforms (T9), editor preview (T10), debug rendering (T11), example scene + validation + GDExtension + docs (T12). Build matrix (module + GDExtension) covered by the shim rule + T1 Step 5 + T12 Step 3. Non-goals (3.5, two-color, screen) excluded.
- **Open implementation details from the spec** are resolved here: surface material via `mesh_surface_set_material` + per-instance shader params (T2/T5); shaders code-generated as strings (T2/T4/T6); `z_spacing` default `0.0`, not auto-scaled (T2/T3); billboard AABB expansion = max-extent cube (T5).
- **Known follow-ups to confirm during execution (not blockers):** exact `spine::AtlasPage` PMA accessor name (T4 Step 2), bone-basis column signs (T9 Step 1 — verified visually in T9 Step 7), and whether the repo has a ready `build-extension.sh` vs manual godot-cpp scons (T12 Step 3).
