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

#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/variant.hpp>
#endif

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
}

SpineSprite3D::SpineSprite3D()
	: update_mode(SpineConstant::UpdateMode_Process), time_scale(1.0), skeleton_clipper(new spine::SkeletonClipping()), modified_bones(false) {
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
	// Task 2 will implement mesh building.
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

void SpineSprite3D::clear_statics() {
	// Task 2 will add statics teardown.
}
