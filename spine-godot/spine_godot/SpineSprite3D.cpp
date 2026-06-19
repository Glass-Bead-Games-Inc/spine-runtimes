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

#include "SpineSprite3D.h"
#include "SpineEvent.h"
#include "SpineTrackEntry.h"
#include "SpineSkeleton.h"
#include "SpineRendererObject.h"

#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/variant.hpp>
#else
#include "scene/resources/shader.h"
#include "scene/resources/shader_material.h"
#include "scene/resources/mesh.h"
#if (VERSION_MAJOR >= 4 && VERSION_MINOR >= 6)
#include "servers/rendering/rendering_server.h"
#else
#include "servers/rendering_server.h"
#endif
#endif

#include <spine/RegionAttachment.h>
#include <spine/MeshAttachment.h>

// ---------------------------------------------------------------------------
// SpineSprite3DStatics — shader/material singleton for 3D spine rendering.
// Modeled on SpineSpriteStatics in SpineSprite.cpp.
// ---------------------------------------------------------------------------
struct SpineSprite3DStatics {
private:
	static SpineSprite3DStatics *_instance;

	// Build GLSL source for the requested variant.
	// Generates all {Normal, Additive, Multiply} blend x {straight, pma} unshaded variants.
	// Shaded variants (shaded == true) are stubbed for Task 6.
	static String build_shader_source(spine::BlendMode blend, bool shaded, bool pma) {
		// Blend mode -> render_mode token
		String rm;
		switch (blend) {
			case spine::BlendMode_Additive: rm = "blend_add"; break;
			case spine::BlendMode_Multiply: rm = "blend_mul"; break;
			default: rm = "blend_mix"; break; // Normal (Screen unsupported -> treat as normal)
		}

		// Shading mode token (Task 6 will remove "unshaded" for shaded variants)
		String shading = shaded ? "" : "unshaded, ";

		// Fragment: both straight and PMA use same formula; render_mode drives blending
		// PMA atlases already store rgb*a so we output as-is; blend_add/mul + PMA is correct per 2D behavior
		String frag = pma
				? "vec4 tex = texture(albedo_tex, UV); vec3 c = tex.rgb * COLOR.rgb; ALBEDO = c; ALPHA = tex.a * COLOR.a;"
				: "vec4 tex = texture(albedo_tex, UV); ALBEDO = tex.rgb * COLOR.rgb; ALPHA = tex.a * COLOR.a;";

		return String("shader_type spatial;\n") +
			   "render_mode " + rm + ", cull_disabled, " + shading + "depth_draw_opaque, shadows_disabled;\n"
			   "\n"
			   "uniform sampler2D albedo_tex : source_color, filter_linear_mipmap;\n"
			   "\n"
			   "void vertex() {\n"
			   "    // billboard inserted in Task 5; identity for now\n"
			   "}\n"
			   "\n"
			   "void fragment() {\n"
			   "    " +
			   frag +
			   "\n"
			   "}\n";
	}

	static Ref<ShaderMaterial> make_material(spine::BlendMode blend, bool shaded, bool pma) {
		Ref<Shader> shader;
		shader.instantiate();
		shader->set_code(build_shader_source(blend, shaded, pma));

		Ref<ShaderMaterial> mat;
		mat.instantiate();
		mat->set_shader(shader);
		return mat;
	}

public:
	// Cache key: blend * 4 + shaded * 2 + pma  (max index = 3*4+2+1 = 15)
	Ref<ShaderMaterial> materials[16];
	int sprite_count;

	SpineSprite3DStatics() : sprite_count(0) {
		// Variants are built lazily on first get_material() call.
	}

	Ref<ShaderMaterial> get_material(spine::BlendMode blend, bool shaded, bool pma) {
		int key = (int) blend * 4 + (shaded ? 2 : 0) + (pma ? 1 : 0);
		if (!materials[key].is_valid()) {
			materials[key] = make_material(blend, shaded, pma);
		}
		return materials[key];
	}

	static SpineSprite3DStatics &instance() {
		if (!_instance) {
			_instance = new SpineSprite3DStatics();
		}
		return *_instance;
	}

	static void clear() {
		if (_instance) {
			delete _instance;
		}
		_instance = nullptr;
	}
};

SpineSprite3DStatics *SpineSprite3DStatics::_instance = nullptr;

void SpineSprite3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_skeleton_data_res", "skeleton_data_res"), &SpineSprite3D::set_skeleton_data_res);
	ClassDB::bind_method(D_METHOD("get_skeleton_data_res"), &SpineSprite3D::get_skeleton_data_res);
	ClassDB::bind_method(D_METHOD("get_skeleton"), &SpineSprite3D::get_skeleton);
	ClassDB::bind_method(D_METHOD("get_animation_state"), &SpineSprite3D::get_animation_state);
	ClassDB::bind_method(D_METHOD("on_skeleton_data_changed"), &SpineSprite3D::on_skeleton_data_changed);
	ClassDB::bind_method(D_METHOD("set_update_mode", "v"), &SpineSprite3D::set_update_mode);
	ClassDB::bind_method(D_METHOD("get_update_mode"), &SpineSprite3D::get_update_mode);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &SpineSprite3D::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_time_scale", "v"), &SpineSprite3D::set_time_scale);
	ClassDB::bind_method(D_METHOD("update_skeleton", "delta"), &SpineSprite3D::update_skeleton);
	ClassDB::bind_method(D_METHOD("set_pixel_size", "v"), &SpineSprite3D::set_pixel_size);
	ClassDB::bind_method(D_METHOD("get_pixel_size"), &SpineSprite3D::get_pixel_size);
	ClassDB::bind_method(D_METHOD("set_z_spacing", "v"), &SpineSprite3D::set_z_spacing);
	ClassDB::bind_method(D_METHOD("get_z_spacing"), &SpineSprite3D::get_z_spacing);
	ClassDB::bind_method(D_METHOD("set_flip_h", "v"), &SpineSprite3D::set_flip_h);
	ClassDB::bind_method(D_METHOD("get_flip_h"), &SpineSprite3D::get_flip_h);
	ClassDB::bind_method(D_METHOD("set_flip_v", "v"), &SpineSprite3D::set_flip_v);
	ClassDB::bind_method(D_METHOD("get_flip_v"), &SpineSprite3D::get_flip_v);

	ADD_SIGNAL(MethodInfo("animation_started", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_interrupted", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_ended", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_completed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_disposed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_event", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D"),
						  PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"),
						  PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry"),
						  PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_TYPE_STRING, "SpineEvent")));
	ADD_SIGNAL(MethodInfo("before_animation_state_update", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("before_animation_state_apply", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("before_world_transforms_change", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("world_transforms_changed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("_internal_spine_objects_invalidated"));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skeleton_data_res", PropertyHint::PROPERTY_HINT_RESOURCE_TYPE, "SpineSkeletonDataResource"),
				 "set_skeleton_data_res", "get_skeleton_data_res");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "time_scale"), "set_time_scale", "get_time_scale");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "pixel_size", PROPERTY_HINT_RANGE, "0.0001,1,0.0001"), "set_pixel_size", "get_pixel_size");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "z_spacing", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_z_spacing", "get_z_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_h"), "set_flip_h", "get_flip_h");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_v"), "set_flip_v", "get_flip_v");
}

SpineSprite3D::SpineSprite3D()
	: update_mode(SpineConstant::UpdateMode_Process), time_scale(1.0), skeleton_clipper(new spine::SkeletonClipping()), modified_bones(false),
	  pixel_size(0.01f), z_spacing(0.0f), flip_h(false), flip_v(false) {
	scratch_world_verts.ensureCapacity(1200);
}

SpineSprite3D::~SpineSprite3D() {
	delete skeleton_clipper;
	if (mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->free_rid(mesh);
#else
		RS::get_singleton()->free(mesh);
#endif
	}
}

void SpineSprite3D::set_skeleton_data_res(const Ref<SpineSkeletonDataResource> &_skeleton_data) {
	skeleton_data_res = _skeleton_data;
	on_skeleton_data_changed();
}

Ref<SpineSkeletonDataResource> SpineSprite3D::get_skeleton_data_res() {
	return skeleton_data_res;
}

void SpineSprite3D::on_skeleton_data_changed() {
	skeleton.unref();
	animation_state.unref();
	// Task 4: clear per-instance material cache; textures change with skeleton data
	material_cache.clear();
	emit_signal(SNAME("_internal_spine_objects_invalidated"));

	if (skeleton_data_res.is_valid()) {
#if VERSION_MAJOR > 3
		if (!skeleton_data_res->is_connected(SNAME("skeleton_data_changed"), callable_mp(this, &SpineSprite3D::on_skeleton_data_changed)))
			skeleton_data_res->connect(SNAME("skeleton_data_changed"), callable_mp(this, &SpineSprite3D::on_skeleton_data_changed));
#else
		if (!skeleton_data_res->is_connected(SNAME("skeleton_data_changed"), this, SNAME("on_skeleton_data_changed")))
			skeleton_data_res->connect(SNAME("skeleton_data_changed"), this, SNAME("on_skeleton_data_changed"));
#endif
	}

	if (skeleton_data_res.is_valid() && skeleton_data_res->is_skeleton_data_loaded()) {
		skeleton = Ref<SpineSkeleton>(memnew(SpineSkeleton));
		skeleton->set_spine_sprite(this);

		animation_state = Ref<SpineAnimationState>(memnew(SpineAnimationState));
		animation_state->set_spine_sprite(this);
		animation_state->get_spine_object()->setListener(this);

		animation_state->update(0);
		animation_state->apply(skeleton);
		skeleton->update_world_transform(SpineConstant::Physics_Update);

		if (update_mode == SpineConstant::UpdateMode_Process) {
			_notification(NOTIFICATION_INTERNAL_PROCESS);
		} else if (update_mode == SpineConstant::UpdateMode_Physics) {
			_notification(NOTIFICATION_INTERNAL_PHYSICS_PROCESS);
		}
	}

	NOTIFY_PROPERTY_LIST_CHANGED();
}

Ref<SpineSkeleton> SpineSprite3D::get_skeleton() {
	return skeleton;
}

Ref<SpineAnimationState> SpineSprite3D::get_animation_state() {
	return animation_state;
}

void SpineSprite3D::_notification(int what) {
	switch (what) {
		case NOTIFICATION_READY: {
			set_process_internal(update_mode == SpineConstant::UpdateMode_Process);
			set_physics_process_internal(update_mode == SpineConstant::UpdateMode_Physics);
			break;
		}
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (update_mode == SpineConstant::UpdateMode_Process) update_skeleton(get_process_delta_time());
			break;
		}
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (update_mode == SpineConstant::UpdateMode_Physics) update_skeleton(get_physics_process_delta_time());
			break;
		}
		default:
			break;
	}
}

void SpineSprite3D::update_skeleton(float delta) {
	if (!skeleton_data_res.is_valid() || !skeleton_data_res->is_skeleton_data_loaded() || !skeleton.is_valid() || !skeleton->get_spine_object() ||
		!animation_state.is_valid() || !animation_state->get_spine_object())
		return;

	emit_signal(SNAME("before_animation_state_update"), this);
	animation_state->update(delta * time_scale);
	if (!is_visible_in_tree()) return;
	emit_signal(SNAME("before_animation_state_apply"), this);
	animation_state->apply(skeleton);
	emit_signal(SNAME("before_world_transforms_change"), this);
	skeleton->update(delta * time_scale);
	skeleton->update_world_transform(SpineConstant::Physics_Update);
	modified_bones = false;
	emit_signal(SNAME("world_transforms_changed"), this);
	if (modified_bones) skeleton->update_world_transform(SpineConstant::Physics_Update);
	build_meshes();
}

void SpineSprite3D::build_meshes() {
	// Full rebuild every frame (simple approach from Task 2).
	if (mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->free_rid(mesh);
#else
		RS::get_singleton()->free(mesh);
#endif
		mesh = RID();
	}

	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;
	spine::Skeleton *sk = skeleton->get_spine_object();
	auto &statics = SpineSprite3DStatics::instance();

	mesh = RS::get_singleton()->mesh_create();
	AABB aabb;
	bool aabb_init = false;
	int surface_index = 0;

	SpineRendererObject *current_ro = nullptr;
	// Task 4: track current blend + pma for batch breaking
	spine::BlendMode current_blend = spine::BlendMode_Normal;
	bool current_pma = false;

	// Reset scratch buffers.
	scratch_positions.clear();
	scratch_uvs.clear();
	scratch_colors.clear();
	scratch_indices.clear();

	// Task 4: Flush the accumulated scratch buffers as one surface.
	// Per-instance material cache: look up or create a ShaderMaterial for (variant, texture).
	// Key: high byte = blend*4 + shaded*2 + pma; lower 56 bits = texture RID id.
	// This ensures each surface gets its own correctly-textured material independent of other
	// SpineSprite3D instances — fixing the Task 2 shared-material / last-write-wins bug.
	auto flush = [&]() {
		if (scratch_indices.size() == 0) return;

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = scratch_positions;
		arrays[Mesh::ARRAY_TEX_UV] = scratch_uvs;
		arrays[Mesh::ARRAY_COLOR] = scratch_colors;
		arrays[Mesh::ARRAY_INDEX] = scratch_indices;

		RS::get_singleton()->mesh_add_surface_from_arrays(mesh, RS::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
				RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE);

		if (current_ro && current_ro->texture.is_valid()) {
			// Build cache key: variant bits in top byte, texture RID in lower 56 bits
			uint64_t variant_bits = (uint64_t)((int)current_blend * 4 + 0 * 2 + (current_pma ? 1 : 0));
			uint64_t tex_id = (uint64_t)current_ro->texture->get_rid().get_id();
			uint64_t cache_key = (variant_bits << 56) | (tex_id & 0x00FFFFFFFFFFFFFFull);

			Ref<ShaderMaterial> mat;
			if (material_cache.has(cache_key)) {
				mat = material_cache[cache_key];
			} else {
				// Clone the shared shader variant into a fresh per-(variant,texture) material
				Ref<ShaderMaterial> variant_mat = statics.get_material(current_blend, false, current_pma);
				mat.instantiate();
				mat->set_shader(variant_mat->get_shader());
				mat->set_shader_parameter("albedo_tex", current_ro->texture);
				material_cache[cache_key] = mat;
			}
			RS::get_singleton()->mesh_surface_set_material(mesh, surface_index, mat->get_rid());
		}
		surface_index++;

		// Reset scratch for next surface.
		scratch_positions.clear();
		scratch_uvs.clear();
		scratch_colors.clear();
		scratch_indices.clear();
	};

	for (int i = 0, n = (int) sk->getSlots().size(); i < n; i++) {
		spine::Slot *slot = sk->getDrawOrder().getAppliedPose()[i];
		spine::Attachment *attachment = slot->getAppliedPose().getAttachment();

		if (!attachment || !slot->getBone().isActive()) {
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		spine::Color sk_color = sk->getColor();
		spine::Color slot_color = slot->getAppliedPose().getColor();
		spine::Color tint(sk_color.r * slot_color.r, sk_color.g * slot_color.g,
				sk_color.b * slot_color.b, sk_color.a * slot_color.a);

		// Task 4: get blend mode from slot data
		spine::BlendMode slot_blend = slot->getData().getBlendMode();

		SpineRendererObject *ro = nullptr;
		spine::Array<float> *world_verts = &scratch_world_verts;
		spine::Array<float> *uvs = nullptr;
		spine::Array<unsigned short> *indices = nullptr;
		spine::AtlasPage *atlas_page = nullptr;

		if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
			auto region = (spine::RegionAttachment *) attachment;
			auto &sequence = region->getSequence();
			int seq_index = sequence.resolveIndex(slot->getAppliedPose());

			world_verts->setSize(8, 0);
			region->computeWorldVertices(*slot, sequence.getOffsets(seq_index).buffer(), world_verts->buffer(), 0);
			auto *atlas_region = (spine::AtlasRegion *) sequence.getRegion(seq_index);
			atlas_page = atlas_region->getPage();
			ro = (SpineRendererObject *) atlas_page->texture;
			uvs = &sequence.getUVs(seq_index);

			// Build quad indices for region attachments.
			static spine::Array<unsigned short> quad_idx;
			if (quad_idx.size() == 0) {
				quad_idx.setSize(6, 0);
				quad_idx[0] = 0; quad_idx[1] = 1; quad_idx[2] = 2;
				quad_idx[3] = 2; quad_idx[4] = 3; quad_idx[5] = 0;
			}
			indices = &quad_idx;

			auto &att_color = region->getColor();
			tint.r *= att_color.r;
			tint.g *= att_color.g;
			tint.b *= att_color.b;
			tint.a *= att_color.a;
		} else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
			auto mesh_att = (spine::MeshAttachment *) attachment;
			auto &sequence = mesh_att->getSequence();
			int seq_index = sequence.resolveIndex(slot->getAppliedPose());

			world_verts->setSize(mesh_att->getWorldVerticesLength(), 0);
			mesh_att->computeWorldVertices(*sk, *slot, 0, mesh_att->getWorldVerticesLength(), world_verts->buffer(), 0, 2);
			auto *atlas_region = (spine::AtlasRegion *) sequence.getRegion(seq_index);
			atlas_page = atlas_region->getPage();
			ro = (SpineRendererObject *) atlas_page->texture;
			uvs = &sequence.getUVs(seq_index);
			indices = &mesh_att->getTriangles();

			auto &att_color = mesh_att->getColor();
			tint.r *= att_color.r;
			tint.g *= att_color.g;
			tint.b *= att_color.b;
			tint.a *= att_color.a;
		} else {
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		if (!ro || !uvs || !indices || indices->size() == 0) {
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		// Task 4: read PMA from atlas page
		bool slot_pma = atlas_page ? atlas_page->pma : false;

		// Task 4: Flush if we switch texture page, blend mode, or PMA flag
		if (current_ro && (ro != current_ro || slot_blend != current_blend || slot_pma != current_pma)) {
			flush();
		}
		current_ro = ro;
		current_blend = slot_blend;
		current_pma = slot_pma;

		int base = (int) scratch_positions.size();
		int num_verts = (int) world_verts->size() / 2;
		float z = -((float) i) * z_spacing;

		float sx = flip_h ? -pixel_size : pixel_size;
		float sy = flip_v ? pixel_size : -pixel_size; // base is -pixel_size (Y-flip); flip_v cancels it

		for (int v = 0; v < num_verts; v++) {
			float x = world_verts->buffer()[v * 2] * sx;
			float y = world_verts->buffer()[v * 2 + 1] * sy;
			Vector3 pos(x, y, z);
			scratch_positions.push_back(pos);
			scratch_uvs.push_back(Vector2(uvs->buffer()[v * 2], uvs->buffer()[v * 2 + 1]));
			scratch_colors.push_back(Color(tint.r, tint.g, tint.b, tint.a));

			if (!aabb_init) {
				aabb.position = pos;
				aabb.size = Vector3();
				aabb_init = true;
			} else {
				aabb.expand_to(pos);
			}
		}

		for (int t = 0; t < (int) indices->size(); t++) {
			scratch_indices.push_back(base + (int) indices->buffer()[t]);
		}

		skeleton_clipper->clipEnd(*slot);
	}
	skeleton_clipper->clipEnd();

	flush(); // flush final surface

	if (aabb_init) {
		RS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);
	}
	set_base(mesh);
}

void SpineSprite3D::callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event) {
	Ref<SpineTrackEntry> entry_ref = Ref<SpineTrackEntry>(memnew(SpineTrackEntry));
	entry_ref->set_spine_object(this, entry);

	Ref<SpineEvent> event_ref(nullptr);
	if (event) {
		event_ref = Ref<SpineEvent>(memnew(SpineEvent));
		event_ref->set_spine_object(this, event);
	}

	switch (type) {
		case spine::EventType_Start:
			emit_signal(SNAME("animation_started"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Interrupt:
			emit_signal(SNAME("animation_interrupted"), this, animation_state, entry_ref);
			break;
		case spine::EventType_End:
			emit_signal(SNAME("animation_ended"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Complete:
			emit_signal(SNAME("animation_completed"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Dispose:
			emit_signal(SNAME("animation_disposed"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Event:
			emit_signal(SNAME("animation_event"), this, animation_state, entry_ref, event_ref);
			break;
	}
}

SpineConstant::UpdateMode SpineSprite3D::get_update_mode() {
	return update_mode;
}

void SpineSprite3D::set_update_mode(SpineConstant::UpdateMode v) {
	update_mode = v;
	set_process_internal(update_mode == SpineConstant::UpdateMode_Process);
	set_physics_process_internal(update_mode == SpineConstant::UpdateMode_Physics);
}

void SpineSprite3D::set_time_scale(float v) {
	time_scale = v;
}

float SpineSprite3D::get_time_scale() {
	return time_scale;
}

void SpineSprite3D::set_pixel_size(float v) {
	pixel_size = v;
	if (skeleton.is_valid()) build_meshes();
}

float SpineSprite3D::get_pixel_size() {
	return pixel_size;
}

void SpineSprite3D::set_z_spacing(float v) {
	z_spacing = v;
	if (skeleton.is_valid()) build_meshes();
}

float SpineSprite3D::get_z_spacing() {
	return z_spacing;
}

void SpineSprite3D::set_flip_h(bool v) {
	flip_h = v;
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_flip_h() {
	return flip_h;
}

void SpineSprite3D::set_flip_v(bool v) {
	flip_v = v;
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_flip_v() {
	return flip_v;
}

void SpineSprite3D::clear_statics() {
	SpineSprite3DStatics::clear();
}
