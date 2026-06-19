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

#include "SpineSkeleton.h"
#include "SpineAnimationState.h"
#include "SpineConstant.h"
#include "SpineSpriteOwner.h"
#ifdef SPINE_GODOT_EXTENSION
#include "SpineCommon.h"
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#else
#include "scene/3d/visual_instance_3d.h" // declares GeometryInstance3D
#include "core/templates/hash_map.h"
#include "scene/resources/shader_material.h"
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

protected:
	Ref<SpineSkeletonDataResource> skeleton_data_res;
	Ref<SpineSkeleton> skeleton;
	Ref<SpineAnimationState> animation_state;
	SpineConstant::UpdateMode update_mode;
	float time_scale;
	spine::SkeletonClipping *skeleton_clipper;
	bool modified_bones;

	RID mesh; // owned RS mesh, created in Task 2

	// Task 2: rendering parameters (exposed as properties in Task 3)
	float pixel_size;
	float z_spacing;

	// Task 3: flip controls
	bool flip_h;
	bool flip_v;

	// Task 5: billboard mode
	BillboardMode billboard;

	// Task 6: shaded mode
	bool shaded;

	// Task 8: per-blend-mode custom material overrides
	Ref<Material> normal_material;
	Ref<Material> additive_material;
	Ref<Material> multiply_material;
	Ref<Material> screen_material;

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

	// Task 4: per-instance material cache keyed by (blend<<3 | shaded<<2 | pma<<1 | 0) in high byte + texture RID id.
	// Encoding: key = ((uint64_t)(blend * 4 + shaded * 2 + pma) << 56) | texture_rid_id
	HashMap<uint64_t, Ref<ShaderMaterial>> material_cache;

	static void _bind_methods();
	void _notification(int what);

	void callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event) override;

	void build_meshes();

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
	Ref<SpineAnimationState> get_animation_state();
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

	static void clear_statics();
};

VARIANT_ENUM_CAST(SpineSprite3D::BillboardMode)
