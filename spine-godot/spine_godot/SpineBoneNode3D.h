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

#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

#include "SpineCommon.h"
#include "SpineSprite3D.h"
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/node3d.hpp>
#else
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#endif

class SpineBoneNode3D : public Node3D {
	GDCLASS(SpineBoneNode3D, Node3D)

protected:
	String bone_name;
	int bone_index;
	SpineConstant::BoneMode bone_mode;
	bool enabled;

	// Optional debug overlay (like the 2D SpineBoneNode): draws a bone kite AT THIS NODE'S transform, so
	// you can overlay it on the SpineSprite3D debug bones and confirm they coincide (i.e. the attachment
	// transform matches the bone). Rendered via a separate RS instance in the node's world scenario.
	bool debug_bone;
	float debug_thickness;
	Color debug_color;
	RID debug_instance;
	RID debug_mesh;
	Ref<Material> debug_material;

	static void _bind_methods();
	void _notification(int what);
	void _get_property_list(List<PropertyInfo> *list) const;
	bool _get(const StringName &property, Variant &value) const;
	bool _set(const StringName &property, const Variant &value);
	void on_before_world_transforms_change(const Variant &_sprite);
	void on_world_transforms_changed(const Variant &_sprite);
	void update_transform(SpineSprite3D *sprite);
	void update_debug(SpineSprite3D *sprite);
	void free_debug();

public:
	SpineBoneNode3D()
		: bone_index(-1), bone_mode(SpineConstant::BoneMode_Follow), enabled(true), debug_bone(false), debug_thickness(8.0f),
		  debug_color(Color(0, 1, 1, 0.6f)) {
	}
	~SpineBoneNode3D();

	void set_bone_name(const String &_bone_name);
	String get_bone_name();

	void set_bone_mode(SpineConstant::BoneMode v);
	SpineConstant::BoneMode get_bone_mode();

	void set_enabled(bool v);
	bool get_enabled();

	void set_debug_bone(bool v);
	bool get_debug_bone();
	void set_debug_thickness(float v);
	float get_debug_thickness();
	void set_debug_color(const Color &v);
	Color get_debug_color();

	int get_bone_index() {
		return bone_index;
	}
};

#endif// _3D_DISABLED
