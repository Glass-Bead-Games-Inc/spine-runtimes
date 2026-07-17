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
#include "SpineSlotNode3D.h"

#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

void SpineSlotNode3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_world_transforms_changed", "spine_sprite"), &SpineSlotNode3D::on_world_transforms_changed);

	ClassDB::bind_method(D_METHOD("set_normal_material", "material"), &SpineSlotNode3D::set_normal_material);
	ClassDB::bind_method(D_METHOD("get_normal_material"), &SpineSlotNode3D::get_normal_material);
	ClassDB::bind_method(D_METHOD("set_additive_material", "material"), &SpineSlotNode3D::set_additive_material);
	ClassDB::bind_method(D_METHOD("get_additive_material"), &SpineSlotNode3D::get_additive_material);
	ClassDB::bind_method(D_METHOD("set_multiply_material", "material"), &SpineSlotNode3D::set_multiply_material);
	ClassDB::bind_method(D_METHOD("get_multiply_material"), &SpineSlotNode3D::get_multiply_material);
	ClassDB::bind_method(D_METHOD("set_screen_material", "material"), &SpineSlotNode3D::set_screen_material);
	ClassDB::bind_method(D_METHOD("get_screen_material"), &SpineSlotNode3D::get_screen_material);

	ClassDB::bind_method(D_METHOD("set_slot_name", "slot_name"), &SpineSlotNode3D::set_slot_name);
	ClassDB::bind_method(D_METHOD("get_slot_name"), &SpineSlotNode3D::get_slot_name);
	ClassDB::bind_method(D_METHOD("get_slot_index"), &SpineSlotNode3D::get_slot_index);

	// Top-level slot picker. Registered as a real property BEFORE the Materials group so it renders at the
	// top of the node (like SpineBoneNode3D's bone_name) instead of being swallowed by the group. Its enum
	// of slot names is filled dynamically in _validate_property(). NOTE: adding it from _get_property_list
	// instead put it AFTER the bound material properties (the GDExtension appends dynamic props last), which
	// is exactly what pushed it inside "Materials".
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "slot_name", PROPERTY_HINT_ENUM, ""), "set_slot_name", "get_slot_name");

	ADD_GROUP("Materials", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_normal_material",
				 "get_normal_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "additive_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_additive_material",
				 "get_additive_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiply_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_multiply_material",
				 "get_multiply_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "screen_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_screen_material",
				 "get_screen_material");
}

void SpineSlotNode3D::_notification(int what) {
	switch (what) {
		case NOTIFICATION_PARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
#if VERSION_MAJOR > 3
				sprite->connect(SNAME("world_transforms_changed"), callable_mp(this, &SpineSlotNode3D::on_world_transforms_changed));
#else
				sprite->connect(SNAME("world_transforms_changed"), this, SNAME("_on_world_transforms_changed"));
#endif
				// Billboard needs a per-frame re-solve: the billboard basis depends on the active CAMERA,
				// which moves independently of the skeleton (world_transforms_changed only fires on skeleton
				// updates). Internal process handles that; it no-ops when billboard is off. Mirrors SpineBoneNode3D.
				set_process_internal(true);
				update_transform(sprite);
			} else {
				WARN_PRINT("SpineSlotNode3D parent is not a SpineSprite3D.");
			}
			NOTIFY_PROPERTY_LIST_CHANGED();
			break;
		}
		case NOTIFICATION_INTERNAL_PROCESS: {
			// Re-solve every frame while the parent is billboarded so an attached scene tracks the camera
			// (which moves independently of the skeleton). Stays signal-driven otherwise.
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (!sprite) break;
			if (sprite->get_billboard() != SpineSprite3D::BILLBOARD_DISABLED) update_transform(sprite);
			break;
		}
		case NOTIFICATION_UNPARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
#if VERSION_MAJOR > 3
				sprite->disconnect(SNAME("world_transforms_changed"), callable_mp(this, &SpineSlotNode3D::on_world_transforms_changed));
#else
				sprite->disconnect(SNAME("world_transforms_changed"), this, SNAME("_on_world_transforms_changed"));
#endif
			}
			break;
		}
		default:
			break;
	}
}

// Fill slot_name's enum with the parent skeleton's slot names. _validate_property modifies the already
// registered property in place, so it keeps its top-of-node position (unlike a _get_property_list insert).
void SpineSlotNode3D::_validate_property(PropertyInfo &property) const {
	if (property.name != StringName("slot_name")) return;
#ifdef SPINE_GODOT_EXTENSION
	PackedStringArray slot_names;
#else
	Vector<String> slot_names;
#endif
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (sprite && sprite->get_skeleton_data_res().is_valid())
		sprite->get_skeleton_data_res()->get_slot_names(slot_names);
	else
		slot_names.push_back(slot_name);// keep the current value visible until parented to a SpineSprite3D
	property.hint = PROPERTY_HINT_ENUM;
	property.hint_string = String(",").join(slot_names);
}

void SpineSlotNode3D::on_world_transforms_changed(const Variant &_sprite) {
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(_sprite.operator Object *());
	update_transform(sprite);
}

void SpineSlotNode3D::update_transform(SpineSprite3D *sprite) {
	if (!is_visible_in_tree()) return;
	if (!sprite) return;
	if (!sprite->get_skeleton().is_valid() || !sprite->get_skeleton()->get_spine_object()) return;
	auto slot_ref = sprite->get_skeleton()->find_slot(slot_name);
	if (!slot_ref.is_valid()) {
		slot_index = -1;
		return;
	}
	slot_index = slot_ref->get_data()->get_index();
	auto bone_ref = slot_ref->get_bone();
	if (!bone_ref.is_valid()) return;
	spine::Bone *bone = bone_ref->get_spine_object();
	if (!bone) return;

	// Depth must match how SpineSprite3D::build_meshes() lays out each slot's attachment quad. The display
	// shader does TWO things per part (see build_shader_source): (1) VERTEX.z *= z_spacing — a local +Z
	// layer offset of -(pos)*z_spacing; and (2) a VIEW-SPACE push `_vpos.z -= _draw_index * depth_offset`
	// (= +pos*depth_offset toward the camera). The NET depth toward the camera is pos*(depth_offset - z_spacing)
	// — and because depth_offset usually EXCEEDS z_spacing, that term dominates and is what actually orders the
	// parts. So the node z must include BOTH: z = pos*(depth_offset - z_spacing). (Using only -pos*z_spacing —
	// as before — puts the child on the far side of EVERY part, so an attached scene renders behind the whole
	// character.) We approximate the view-space depth_offset push as local +Z, which is exact for a front-facing
	// or billboarded view; the setup-pose data index differs from the draw-order position once a DrawOrderTimeline
	// reorders slots, so derive pos from the slot's current position in the applied draw order.
	float z = 0.0f;
	float z_spacing = sprite->get_z_spacing();
	float depth_offset = sprite->get_depth_offset();
	if (z_spacing != 0.0f || depth_offset != 0.0f) {
		spine::Skeleton *spine_skeleton = sprite->get_skeleton()->get_spine_object();
		if (spine_skeleton) {
			spine::Array<spine::Slot *> &draw_order = spine_skeleton->getDrawOrder().getAppliedPose();
			for (int pos = 0, n = (int) draw_order.size(); pos < n; pos++) {
				spine::Slot *slot = draw_order[pos];
				if (slot && slot->getData().getIndex() == slot_index) {
					z = ((float) pos) * (depth_offset - z_spacing);
					break;
				}
			}
		}
	}

	// Place this node at the slot's bone position + slot depth. When the body is billboarded, the display
	// shader renders the card at the sprite ORIGIN with a camera-facing basis (the sprite's own rotation/
	// scale discarded), so put this node in that same billboarded frame — otherwise an attached scene stays
	// in the flat local card plane instead of on the visible slot. Mirrors SpineBoneNode3D.
	Transform3D local = sprite->bone_to_transform3d(bone, z);
	Basis bb;
	if (sprite->get_billboard_basis(bb)) {
		set_global_transform(Transform3D(bb, sprite->get_global_transform().origin) * local);
	} else {
		set_transform(local);
	}
}

void SpineSlotNode3D::set_slot_name(const String &_slot_name) {
	slot_name = _slot_name;
	update_transform(Object::cast_to<SpineSprite3D>(get_parent()));
}

String SpineSlotNode3D::get_slot_name() {
	return slot_name;
}

Ref<Material> SpineSlotNode3D::get_normal_material() {
	return normal_material;
}

void SpineSlotNode3D::set_normal_material(Ref<Material> material) {
	normal_material = material;
}

Ref<Material> SpineSlotNode3D::get_additive_material() {
	return additive_material;
}

void SpineSlotNode3D::set_additive_material(Ref<Material> material) {
	additive_material = material;
}

Ref<Material> SpineSlotNode3D::get_multiply_material() {
	return multiply_material;
}

void SpineSlotNode3D::set_multiply_material(Ref<Material> material) {
	multiply_material = material;
}

Ref<Material> SpineSlotNode3D::get_screen_material() {
	return screen_material;
}

void SpineSlotNode3D::set_screen_material(Ref<Material> material) {
	screen_material = material;
}

#endif// _3D_DISABLED
