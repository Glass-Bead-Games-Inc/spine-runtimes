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

#include "SpineSlotNode3D.h"

#ifndef _3D_DISABLED

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
				update_transform(sprite);
			} else {
				WARN_PRINT("SpineSlotNode3D parent is not a SpineSprite3D.");
			}
			NOTIFY_PROPERTY_LIST_CHANGED();
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

void SpineSlotNode3D::_get_property_list(List<PropertyInfo> *list) const {
#ifdef SPINE_GODOT_EXTENSION
	PackedStringArray slot_names;
#else
	Vector<String> slot_names;
#endif
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (sprite && sprite->get_skeleton_data_res().is_valid())
		sprite->get_skeleton_data_res()->get_slot_names(slot_names);
	else
		slot_names.push_back(slot_name);

	auto element = list->front();
	while (element) {
		auto property_info = element->get();
		if (property_info.name == StringName("SpineSlotNode3D")) break;
		element = element->next();
	}
	PropertyInfo slot_name_property;
	slot_name_property.name = "slot_name";
	slot_name_property.type = Variant::STRING;
	slot_name_property.hint_string = String(",").join(slot_names);
	slot_name_property.hint = PROPERTY_HINT_ENUM;
	slot_name_property.usage = PROPERTY_USAGE_DEFAULT;
	list->insert_after(element, slot_name_property);
}

bool SpineSlotNode3D::_get(const StringName &property, Variant &value) const {
	if (property == StringName("slot_name")) {
		value = slot_name;
		return true;
	}
	return false;
}

bool SpineSlotNode3D::_set(const StringName &property, const Variant &value) {
	if (property == StringName("slot_name")) {
		slot_name = value;
		SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
		update_transform(sprite);
		return true;
	}
	return false;
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

	// Depth must match SpineSprite3D::build_meshes(), which places each slot's
	// attachment quad at z = -(draw_order_position) * z_spacing. The setup-pose data
	// index differs from the draw-order position once a DrawOrderTimeline reorders
	// slots, so derive z from the slot's current position in the applied draw order.
	float z = 0.0f;
	float z_spacing = sprite->get_z_spacing();
	if (z_spacing != 0.0f) {
		spine::Skeleton *spine_skeleton = sprite->get_skeleton()->get_spine_object();
		if (spine_skeleton) {
			spine::Array<spine::Slot *> &draw_order = spine_skeleton->getDrawOrder().getAppliedPose();
			for (int pos = 0, n = (int) draw_order.size(); pos < n; pos++) {
				spine::Slot *slot = draw_order[pos];
				if (slot && slot->getData().getIndex() == slot_index) {
					z = -((float) pos) * z_spacing;
					break;
				}
			}
		}
	}

	// Place this node in sprite-local space at the slot's bone position, at slot depth
	set_transform(sprite->bone_to_transform3d(bone, z));
}

void SpineSlotNode3D::set_slot_name(const String &_slot_name) {
	slot_name = _slot_name;
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
