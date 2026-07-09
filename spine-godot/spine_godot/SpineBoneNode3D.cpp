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

	ADD_PROPERTY(PropertyInfo(Variant::INT, "bone_mode", PROPERTY_HINT_ENUM, "Follow,Drive"), "set_bone_mode", "get_bone_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");
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
				update_transform(sprite);
			} else {
				WARN_PRINT("SpineBoneNode3D parent is not a SpineSprite3D.");
			}
			NOTIFY_PROPERTY_LIST_CHANGED();
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
		// FOLLOW: position this node at the bone (bone -> node). Skip if hidden.
		if (!is_visible_in_tree()) return;
		set_transform(sprite->bone_to_transform3d(bone, 0.0f));
	}
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

#endif// _3D_DISABLED
