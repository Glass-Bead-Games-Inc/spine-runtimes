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

#include "SpineCommon.h"
#include "SpineBoneNode3D.h"

#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/world3d.hpp>
#else
#include "scene/resources/mesh.h"
#include "scene/resources/shader.h"
#if (VERSION_MAJOR >= 4 && VERSION_MINOR >= 6)
#include "servers/rendering/rendering_server.h"
#else
#include "servers/rendering_server.h"
#endif
#endif

void SpineBoneNode3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_before_world_transforms_change", "spine_sprite"), &SpineBoneNode3D::on_before_world_transforms_change);
	ClassDB::bind_method(D_METHOD("_on_world_transforms_changed", "spine_sprite"), &SpineBoneNode3D::on_world_transforms_changed);
	ClassDB::bind_method(D_METHOD("set_bone_name", "bone_name"), &SpineBoneNode3D::set_bone_name);
	ClassDB::bind_method(D_METHOD("get_bone_name"), &SpineBoneNode3D::get_bone_name);
	ClassDB::bind_method(D_METHOD("set_bone_mode", "bone_mode"), &SpineBoneNode3D::set_bone_mode);
	ClassDB::bind_method(D_METHOD("get_bone_mode"), &SpineBoneNode3D::get_bone_mode);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &SpineBoneNode3D::set_enabled);
	ClassDB::bind_method(D_METHOD("get_enabled"), &SpineBoneNode3D::get_enabled);
	ClassDB::bind_method(D_METHOD("get_bone_index"), &SpineBoneNode3D::get_bone_index);
	ClassDB::bind_method(D_METHOD("set_debug_bone", "v"), &SpineBoneNode3D::set_debug_bone);
	ClassDB::bind_method(D_METHOD("get_debug_bone"), &SpineBoneNode3D::get_debug_bone);
	ClassDB::bind_method(D_METHOD("set_debug_thickness", "v"), &SpineBoneNode3D::set_debug_thickness);
	ClassDB::bind_method(D_METHOD("get_debug_thickness"), &SpineBoneNode3D::get_debug_thickness);
	ClassDB::bind_method(D_METHOD("set_debug_color", "v"), &SpineBoneNode3D::set_debug_color);
	ClassDB::bind_method(D_METHOD("get_debug_color"), &SpineBoneNode3D::get_debug_color);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "bone_mode", PROPERTY_HINT_ENUM, "Follow,Drive"), "set_bone_mode", "get_bone_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_bone"), "set_debug_bone", "get_debug_bone");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "debug_thickness"), "set_debug_thickness", "get_debug_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "debug_color"), "set_debug_color", "get_debug_color");
}

void SpineBoneNode3D::_notification(int what) {
	switch (what) {
		case NOTIFICATION_PARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
#if VERSION_MAJOR > 3
				sprite->connect(SNAME("before_world_transforms_change"), callable_mp(this, &SpineBoneNode3D::on_before_world_transforms_change));
				sprite->connect(SNAME("world_transforms_changed"), callable_mp(this, &SpineBoneNode3D::on_world_transforms_changed));
#else
				sprite->connect(SNAME("before_world_transforms_change"), this, SNAME("_on_before_world_transforms_change"));
				sprite->connect(SNAME("world_transforms_changed"), this, SNAME("_on_world_transforms_changed"));
#endif
				// Follow + billboard needs a per-frame re-solve: the billboard basis depends on the active
				// CAMERA, which can move independently of the skeleton (world_transforms_changed only fires
				// on skeleton updates). Internal process handles that; it no-ops when billboard is off.
				set_process_internal(true);
				update_transform(sprite);
			} else {
				WARN_PRINT("SpineBoneNode3D parent is not a SpineSprite3D.");
			}
			NOTIFY_PROPERTY_LIST_CHANGED();
			break;
		}
		case NOTIFICATION_INTERNAL_PROCESS: {
			// Re-solve every frame when either: Follow + the parent is billboarded (track the camera,
			// which moves independently of the skeleton), OR the debug overlay is on (keep the kite on the
			// bone even if the skeleton is static/Manual). Otherwise stays signal-driven.
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (!sprite) break;
			bool billboard_follow = bone_mode == SpineConstant::BoneMode_Follow && sprite->get_billboard() != SpineSprite3D::BILLBOARD_DISABLED;
			if (billboard_follow || debug_bone) update_transform(sprite);
			break;
		}
		case NOTIFICATION_UNPARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
#if VERSION_MAJOR > 3
				sprite->disconnect(SNAME("before_world_transforms_change"), callable_mp(this, &SpineBoneNode3D::on_before_world_transforms_change));
				sprite->disconnect(SNAME("world_transforms_changed"), callable_mp(this, &SpineBoneNode3D::on_world_transforms_changed));
#else
				sprite->disconnect(SNAME("before_world_transforms_change"), this, SNAME("_on_before_world_transforms_change"));
				sprite->disconnect(SNAME("world_transforms_changed"), this, SNAME("_on_world_transforms_changed"));
#endif
			}
			break;
		}
		default:
			break;
	}
}

void SpineBoneNode3D::_get_property_list(List<PropertyInfo> *list) const {
#ifdef SPINE_GODOT_EXTENSION
	PackedStringArray bone_names;
#else
	Vector<String> bone_names;
#endif
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (sprite && sprite->get_skeleton_data_res().is_valid())
		sprite->get_skeleton_data_res()->get_bone_names(bone_names);
	else
		bone_names.push_back(bone_name);

	auto element = list->front();
	while (element) {
		auto property_info = element->get();
		if (property_info.name == StringName("SpineBoneNode3D")) break;
		element = element->next();
	}
	PropertyInfo bone_name_property;
	bone_name_property.name = "bone_name";
	bone_name_property.type = Variant::STRING;
	bone_name_property.hint_string = String(",").join(bone_names);
	bone_name_property.hint = PROPERTY_HINT_ENUM;
	bone_name_property.usage = PROPERTY_USAGE_DEFAULT;
	list->insert_after(element, bone_name_property);
}

bool SpineBoneNode3D::_get(const StringName &property, Variant &value) const {
	if (property == StringName("bone_name")) {
		value = bone_name;
		return true;
	}
	return false;
}

bool SpineBoneNode3D::_set(const StringName &property, const Variant &value) {
	if (property == StringName("bone_name")) {
		bone_name = value;
		SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
		update_transform(sprite);
		return true;
	}
	return false;
}

void SpineBoneNode3D::on_before_world_transforms_change(const Variant &_sprite) {
	if (bone_mode != SpineConstant::BoneMode_Drive) return;
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(_sprite.operator Object *());
	update_transform(sprite);
}

void SpineBoneNode3D::on_world_transforms_changed(const Variant &_sprite) {
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(_sprite.operator Object *());
	update_transform(sprite);
}

void SpineBoneNode3D::update_transform(SpineSprite3D *sprite) {
	if (!enabled) return;
	if (!sprite) return;
	if (!sprite->get_skeleton().is_valid() || !sprite->get_skeleton()->get_spine_object()) return;
	auto bone_ref = sprite->get_skeleton()->find_bone(bone_name);
	if (!bone_ref.is_valid()) {
		bone_index = -1;
		return;
	}
	spine::Bone *bone = bone_ref->get_spine_object();
	if (!bone) return;
	bone_index = bone->getData().getIndex();
	if (bone_mode == SpineConstant::BoneMode_Drive) {
		// DRIVE: write this node's transform back into the bone (node -> bone).
		sprite->set_global_bone_transform_3d(bone_name, get_global_transform());
	} else {
		// FOLLOW: position this node at the bone (bone -> node). Skip positioning if hidden.
		if (is_visible_in_tree()) {
			Transform3D local = sprite->bone_to_transform3d(bone, 0.0f);
			Basis bb;
			if (sprite->get_billboard_basis(bb)) {
				// Billboarded body: the display shader renders the card at the sprite ORIGIN with a
				// camera-facing basis (the sprite's own rotation/scale discarded). Place this node in that
				// same billboarded frame so a Follow attachment stays glued to the visible body instead of
				// the flat local card plane. (DRIVE mode is unchanged; billboard is a display-only rotation.)
				Transform3D card(bb, sprite->get_global_transform().origin);
				set_global_transform(card * local);
			} else {
				set_transform(local);
			}
		}
	}
	if (debug_bone) update_debug(sprite);
}

void SpineBoneNode3D::set_bone_name(const String &_bone_name) {
	bone_name = _bone_name;
}

String SpineBoneNode3D::get_bone_name() {
	return bone_name;
}

void SpineBoneNode3D::set_bone_mode(SpineConstant::BoneMode v) {
	if (bone_mode != v) {
		bone_mode = v;
		update_transform(Object::cast_to<SpineSprite3D>(get_parent()));
	}
}

SpineConstant::BoneMode SpineBoneNode3D::get_bone_mode() {
	return bone_mode;
}

void SpineBoneNode3D::set_enabled(bool v) {
	enabled = v;
	update_transform(Object::cast_to<SpineSprite3D>(get_parent()));
}

bool SpineBoneNode3D::get_enabled() {
	return enabled;
}

SpineBoneNode3D::~SpineBoneNode3D() {
	free_debug();
}

void SpineBoneNode3D::free_debug() {
	RS *rs = RS::get_singleton();
	if (debug_instance.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		rs->free_rid(debug_instance);
#else
		rs->free(debug_instance);
#endif
		debug_instance = RID();
	}
	if (debug_mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		rs->free_rid(debug_mesh);
#else
		rs->free(debug_mesh);
#endif
		debug_mesh = RID();
	}
}

// Draws a bone kite AT THIS NODE'S transform (like the 2D SpineBoneNode debug), scaled by the sprite's
// pixel_size so it matches the SpineSprite3D debug bones in world size. If this node's transform matches
// the bone, this kite overlays the sprite's kite exactly — a direct alignment check.
void SpineBoneNode3D::update_debug(SpineSprite3D *sprite) {
	if (!debug_bone || !enabled || !is_inside_tree()) {
		free_debug();
		return;
	}
	if (!sprite || !sprite->get_skeleton().is_valid() || !sprite->get_skeleton()->get_spine_object()) return;
	auto bone_ref = sprite->get_skeleton()->find_bone(bone_name);
	if (!bone_ref.is_valid()) return;
	spine::Bone *bone = bone_ref->get_spine_object();
	if (!bone) return;

	RS *rs = RS::get_singleton();
	float ps = sprite->get_pixel_size();
	float t = debug_thickness * ps;
	float len = bone->getData().getLength();
	if (len == 0.0f) len = debug_thickness * 2.0f;
	len *= ps;

	PackedVector3Array verts;
	PackedColorArray colors;
	PackedInt32Array indices;
	const float ze = 0.003f;// tiny +Z so the kite sits just in front of the sprite's overlay
	verts.push_back(Vector3(-t, 0, ze));
	colors.push_back(debug_color);
	verts.push_back(Vector3(0, t, ze));
	colors.push_back(debug_color);
	verts.push_back(Vector3(len, 0, ze));
	colors.push_back(debug_color);
	verts.push_back(Vector3(0, -t, ze));
	colors.push_back(debug_color);
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(2);
	indices.push_back(0);
	indices.push_back(2);
	indices.push_back(3);

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;

	if (!debug_mesh.is_valid())
		debug_mesh = rs->mesh_create();
	else
		rs->mesh_clear(debug_mesh);
	rs->mesh_add_surface_from_arrays(debug_mesh, SPINE_RS_ENUM::PRIMITIVE_TRIANGLES, arrays);

	if (!debug_material.is_valid()) {
		Ref<Shader> shader;
		shader.instantiate();
		shader->set_code("shader_type spatial;\n"
						 "render_mode unshaded, cull_disabled, depth_test_disabled, shadows_disabled, blend_mix;\n"
						 "void fragment() { ALBEDO = COLOR.rgb; ALPHA = COLOR.a; }\n");
		Ref<ShaderMaterial> mat;
		mat.instantiate();
		mat->set_shader(shader);
		mat->set_render_priority(127);
		debug_material = mat;
	}
	rs->mesh_surface_set_material(debug_mesh, 0, debug_material->get_rid());

	if (!debug_instance.is_valid()) debug_instance = rs->instance_create();
	RID scenario;
	Ref<World3D> world = get_world_3d();
	if (world.is_valid()) scenario = world->get_scenario();
	rs->instance_set_base(debug_instance, debug_mesh);
	rs->instance_set_scenario(debug_instance, scenario);
	rs->instance_set_transform(debug_instance, get_global_transform());
}

void SpineBoneNode3D::set_debug_bone(bool v) {
	debug_bone = v;
	if (!v) free_debug();
	update_transform(Object::cast_to<SpineSprite3D>(get_parent()));
}

bool SpineBoneNode3D::get_debug_bone() {
	return debug_bone;
}

void SpineBoneNode3D::set_debug_thickness(float v) {
	debug_thickness = v;
	update_transform(Object::cast_to<SpineSprite3D>(get_parent()));
}

float SpineBoneNode3D::get_debug_thickness() {
	return debug_thickness;
}

void SpineBoneNode3D::set_debug_color(const Color &v) {
	debug_color = v;
	update_transform(Object::cast_to<SpineSprite3D>(get_parent()));
}

Color SpineBoneNode3D::get_debug_color() {
	return debug_color;
}

#endif// _3D_DISABLED
