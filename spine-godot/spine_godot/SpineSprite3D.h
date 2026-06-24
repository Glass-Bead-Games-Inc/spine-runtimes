/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated April 5, 2025. Replaces all prior versions.
 *
 * Copyright (c) 2013-2025, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#pragma once

#include "SpineCommon.h"

// This 3D node is intentionally Godot-4.x-only: it relies on GeometryInstance3D
// and other scene/3d APIs that do not exist in Godot 3.x. SpineCommon.h provides
// VERSION_MAJOR (module: core/version.h; extension: GODOT_VERSION_MAJOR), so the
// guard below excludes all content from the Godot 3.x module build entirely.
//
// Fix #15: additionally, when a Godot 4.x module is built with disable_3d=yes the
// engine defines _3D_DISABLED and GeometryInstance3D / 3D rendering APIs are
// unavailable, so this content is skipped in that configuration too. In the
// GDExtension build VERSION_MAJOR==4 and _3D_DISABLED is never defined, so the 3D
// code still compiles there, which is correct.
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

#include "SpineSkeleton.h"
#include "SpineAnimationState.h"
#include "SpineConstant.h"
#include "SpineSpriteOwner.h"
#ifdef SPINE_GODOT_EXTENSION
#include "SpineCommon.h"
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#else
#include "scene/3d/visual_instance_3d.h"// declares GeometryInstance3D
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "scene/resources/material.h"// declares Material + ShaderMaterial (no separate shader_material.h in 4.x)
// Fix #10: SurfaceCache below references SPINE_RS_ENUM::ARRAY_MAX and the region-update layout.
#if (VERSION_MAJOR >= 4 && VERSION_MINOR >= 6)
#include "servers/rendering/rendering_server.h"
#else
#include "servers/rendering_server.h"
#endif
#endif

#include <spine/SkeletonClipping.h>

class SpineSlotNode3D;

class SpineSprite3D : public GeometryInstance3D, public spine::AnimationStateListenerObject, public SpineSpriteOwner {
	GDCLASS(SpineSprite3D, GeometryInstance3D)

public:
	enum BillboardMode {
		BILLBOARD_DISABLED = 0,
		BILLBOARD_ENABLED = 1,
		BILLBOARD_Y = 2,
	};

	// Texture sampler filter for the auto-generated 3D shaders. Mipmap filters can
	// corrupt packed atlases (sampling bleeds across atlas regions at lower mips), so
	// linear (no mipmap) is the default.
	enum TextureFilter {
		TEXTURE_FILTER_NEAREST = 0,
		TEXTURE_FILTER_LINEAR = 1,
		TEXTURE_FILTER_NEAREST_MIPMAP = 2,
		TEXTURE_FILTER_LINEAR_MIPMAP = 3,
	};

protected:
	Ref<SpineSkeletonDataResource> skeleton_data_res;
	Ref<SpineSkeleton> skeleton;
	Ref<SpineAnimationState> animation_state;
	SpineConstant::UpdateMode update_mode;
	float time_scale;
	spine::SkeletonClipping *skeleton_clipper;
	bool modified_bones;

	RID mesh;// owned RS mesh, created in Task 2

	// Task 2: rendering parameters (exposed as properties in Task 3)
	float pixel_size;
	float z_spacing;

	// Task 3: flip controls
	bool flip_h;
	bool flip_v;

	// Task 5: billboard mode
	BillboardMode billboard;

	// Texture sampler filter (drives the shader sampler hint for albedo/normal/specular).
	TextureFilter texture_filter;

	// Task 6: shaded mode
	bool shaded;

	// Task 8: per-blend-mode custom material overrides
	Ref<Material> normal_material;
	Ref<Material> additive_material;
	Ref<Material> multiply_material;
	Ref<Material> screen_material;

	// Task 10: editor preview members
	String preview_skin;
	String preview_animation;
	bool preview_frame;
	float preview_time;

	// Task 11: debug overlay fields (same names/defaults as SpineSprite 2D)
	bool debug_root;
	Color debug_root_color;
	bool debug_bones;
	Color debug_bones_color;
	float
		debug_bones_thickness;// Controls kite width for the bones debug overlay (bones are rendered as filled TRIANGLES; other debug categories remain 1px LINES)
	bool debug_regions;
	Color debug_regions_color;
	bool debug_meshes;
	Color debug_meshes_color;
	bool debug_bounding_boxes;
	Color debug_bounding_boxes_color;
	bool debug_paths;
	Color debug_paths_color;
	bool debug_clipping;
	Color debug_clipping_color;

	Ref<ShaderMaterial> debug_lines_material;// per-sprite debug lines material (billboard_mode + priority 127)

	// Task 2: scratch buffers for mesh building
#ifdef SPINE_GODOT_EXTENSION
	PackedVector3Array scratch_positions;
	PackedVector2Array scratch_uvs;
	PackedColorArray scratch_colors;
	PackedInt32Array scratch_indices;
#else
	Vector<Vector3> scratch_positions;
	Vector<Vector2> scratch_uvs;
	Vector<Color> scratch_colors;
	Vector<int> scratch_indices;
#endif

	// Task 2: scratch float buffer for computeWorldVertices
	spine::Array<float> scratch_world_verts;

	// Task 4: per-instance material cache keyed by (blend * 4 + shaded * 2 + pma) in high byte + texture RID id.
	// Encoding: key = ((uint64_t)(blend * 4 + shaded * 2 + pma) << 56) | texture_rid_id
	HashMap<uint64_t, Ref<ShaderMaterial>> material_cache;

	// Fix #2 (GDExtension only path): cache of 3D-safe ImageTexture copies keyed by the
	// original texture's RID id. In the module build get_3d_safe_texture clears the
	// detect-3D callback instead and never populates this. Cleared wherever material_cache is.
	HashMap<uint64_t, Ref<Texture2D>> texture_3d_cache;

	// Fix #10: per-surface cache for the build_meshes() fast path. When the surface
	// topology (count, per-surface vertex/index counts, index contents and chosen
	// material RID) is identical to the previous frame, build_meshes() updates the
	// existing surfaces' vertex/attribute buffers in place via
	// mesh_surface_update_vertex_region / mesh_surface_update_attribute_region rather
	// than freeing + recreating the whole mesh. Mirrors SpineMesh2D::update_mesh.
	struct SurfaceCache {
		int num_vertices = 0;
		int num_indices = 0;
		bool shaded = false;// vertex layout (normal/tangent present) was built shaded
#ifdef SPINE_GODOT_EXTENSION
		PackedInt32Array indices;// last-frame index contents, for topology compare
#else
		Vector<int> indices;
#endif
		RID material;// material RID assigned to the surface (RID() if none)
		// Surface buffer layout for region updates (mirrors SpineMesh2D fields).
		uint32_t surface_offsets[SPINE_RS_ENUM::ARRAY_MAX] = {};
		uint32_t vertex_stride = 0;
		uint32_t normal_tangent_stride = 0;
		uint32_t attribute_stride = 0;
		PackedByteArray vertex_buffer;
		PackedByteArray attribute_buffer;
	};
	Vector<SurfaceCache> surface_cache;// one entry per non-debug surface built last frame
	bool debug_active_last_frame;      // whether the debug overlay produced a surface last frame

	static void _bind_methods();
	void _notification(int what);

	// Task 10: editor preview property overrides
	void _get_property_list(List<PropertyInfo> *list) const;
	bool _get(const StringName &property, Variant &value) const;
	bool _set(const StringName &property, const Variant &value);

	void callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event) override;

	void build_meshes();
	void build_debug_mesh();// Task 11: rebuild PRIMITIVE_LINES debug overlay from skeleton geometry

	// Fix #2: return a texture safe to use in a 3D draw without triggering Godot's
	// "texture used in 3D" auto-reimport (which adds mipmaps + VRAM compression and
	// corrupts packed atlases). Module build clears the RS detect-3D callback; the
	// GDExtension build (which lacks that API) returns a cached ImageTexture copy.
	// Input is a Ref<Texture> (the SpineRendererObject member type); the returned
	// Ref<Texture2D> is what the shader sampler binds to.
	Ref<Texture2D> get_3d_safe_texture(const Ref<Texture> &tex);

public:
	SpineSprite3D();
	~SpineSprite3D();

	void set_skeleton_data_res(const Ref<SpineSkeletonDataResource> &res);
	Ref<SpineSkeletonDataResource> get_skeleton_data_res() override;
	Ref<SpineSkeleton> get_skeleton() override;
	void set_modified_bones() override {
		modified_bones = true;
	}
	Node *owner_as_node() override {
		return this;
	}
	Ref<SpineAnimationState> get_animation_state() override;// SpineSpriteOwner
	void on_skeleton_data_changed();
	void update_skeleton(float delta);

	SpineConstant::UpdateMode get_update_mode();
	void set_update_mode(SpineConstant::UpdateMode v);
	float get_time_scale();
	void set_time_scale(float v);

	// Task 3: pixel_size / z_spacing / flip_h / flip_v accessors
	void set_pixel_size(float v);
	float get_pixel_size();
	void set_z_spacing(float v);
	float get_z_spacing();
	void set_flip_h(bool v);
	bool get_flip_h();
	void set_flip_v(bool v);
	bool get_flip_v();

	// Task 5: billboard mode
	void set_billboard(BillboardMode v);
	BillboardMode get_billboard();

	// Texture sampler filter
	void set_texture_filter(TextureFilter v);
	TextureFilter get_texture_filter();

	// Task 6: shaded mode
	void set_shaded(bool v);
	bool get_shaded();

	// Task 8: per-blend-mode custom material overrides
	void set_normal_material(Ref<Material> v);
	Ref<Material> get_normal_material();
	void set_additive_material(Ref<Material> v);
	Ref<Material> get_additive_material();
	void set_multiply_material(Ref<Material> v);
	Ref<Material> get_multiply_material();
	void set_screen_material(Ref<Material> v);
	Ref<Material> get_screen_material();

	// Task 9: lifting helper and global bone transform accessors
	Transform3D bone_to_transform3d(spine::Bone *bone, float slot_z) const;
	Transform3D get_global_bone_transform_3d(const String &bone_name);
	void set_global_bone_transform_3d(const String &bone_name, Transform3D xform);

	// Task 11: debug overlay getters/setters (parity with SpineSprite 2D)
	bool get_debug_root() {
		return debug_root;
	}
	void set_debug_root(bool v) {
		debug_root = v;
	}
	Color get_debug_root_color() {
		return debug_root_color;
	}
	void set_debug_root_color(const Color &v) {
		debug_root_color = v;
	}
	bool get_debug_bones() {
		return debug_bones;
	}
	void set_debug_bones(bool v) {
		debug_bones = v;
	}
	Color get_debug_bones_color() {
		return debug_bones_color;
	}
	void set_debug_bones_color(const Color &v) {
		debug_bones_color = v;
	}
	float get_debug_bones_thickness() {
		return debug_bones_thickness;
	}
	void set_debug_bones_thickness(float v) {
		debug_bones_thickness = v;
	}
	bool get_debug_regions() {
		return debug_regions;
	}
	void set_debug_regions(bool v) {
		debug_regions = v;
	}
	Color get_debug_regions_color() {
		return debug_regions_color;
	}
	void set_debug_regions_color(const Color &v) {
		debug_regions_color = v;
	}
	bool get_debug_meshes() {
		return debug_meshes;
	}
	void set_debug_meshes(bool v) {
		debug_meshes = v;
	}
	Color get_debug_meshes_color() {
		return debug_meshes_color;
	}
	void set_debug_meshes_color(const Color &v) {
		debug_meshes_color = v;
	}
	bool get_debug_bounding_boxes() {
		return debug_bounding_boxes;
	}
	void set_debug_bounding_boxes(bool v) {
		debug_bounding_boxes = v;
	}
	Color get_debug_bounding_boxes_color() {
		return debug_bounding_boxes_color;
	}
	void set_debug_bounding_boxes_color(const Color &v) {
		debug_bounding_boxes_color = v;
	}
	bool get_debug_paths() {
		return debug_paths;
	}
	void set_debug_paths(bool v) {
		debug_paths = v;
	}
	Color get_debug_paths_color() {
		return debug_paths_color;
	}
	void set_debug_paths_color(const Color &v) {
		debug_paths_color = v;
	}
	bool get_debug_clipping() {
		return debug_clipping;
	}
	void set_debug_clipping(bool v) {
		debug_clipping = v;
	}
	Color get_debug_clipping_color() {
		return debug_clipping_color;
	}
	void set_debug_clipping_color(const Color &v) {
		debug_clipping_color = v;
	}

	static void clear_statics();
};

VARIANT_ENUM_CAST(SpineSprite3D::BillboardMode)
VARIANT_ENUM_CAST(SpineSprite3D::TextureFilter)

#endif// _3D_DISABLED
