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
