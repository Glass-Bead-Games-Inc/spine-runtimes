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
#include "SpineSprite3D.h"
#include "SpineNotifyTrack.h"
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/node.hpp>
#else
#include "scene/main/node.h"
#endif

class SpineAnimationPlayer : public Node {
	GDCLASS(SpineAnimationPlayer, Node)

	static const int MAX_TRACKS = 32;

protected:
	Ref<SpineNotifyTrack> notify_track;
	bool forward_spine_events = true;

	// Per-track crossing state.
	float prev_time[MAX_TRACKS];
	String prev_anim[MAX_TRACKS];
	bool discontinuity_pending = true;// reset all tracks on next observe (set on parent/seek/play)

	static void _bind_methods();
	void _notification(int what);
	void on_before_apply(const Variant &sprite);
	void on_spine_event(const Variant &sprite, const Variant &state, const Variant &entry, const Variant &event);
	void emit_forward(const String &anim, float lo, float hi);// fire notifies with lo < time <= hi

public:
	SpineAnimationPlayer() {
		for (int i = 0; i < MAX_TRACKS; i++) prev_time[i] = 0.0f;
	}

	void set_notify_track(const Ref<SpineNotifyTrack> &v) { notify_track = v; }
	Ref<SpineNotifyTrack> get_notify_track() const { return notify_track; }
	void set_forward_spine_events(bool v) { forward_spine_events = v; }
	bool get_forward_spine_events() const { return forward_spine_events; }

	void play(const String &animation_name, bool loop, int track);
	void seek(float time, int track);
};
#endif
