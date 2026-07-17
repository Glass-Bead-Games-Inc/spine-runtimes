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
#include "SpineNotify.h"
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#else
#include "core/io/resource.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#endif

// A reusable per-character list of notifies (assign to a SpineAnimationPlayer).
class SpineNotifyTrack : public Resource {
	GDCLASS(SpineNotifyTrack, Resource)

protected:
	Array notifies;// of SpineNotify
	// Runtime lookup index: animation name (StringName, interned -> O(1) key compare) -> its
	// notifies. Lets the player fetch one animation's notifies in O(1) instead of scanning
	// every animation's notifies each frame. Not serialized; rebuilt lazily after `notifies`
	// changes, and shared by every SpineAnimationPlayer that references this track.
	HashMap<StringName, Vector<Ref<SpineNotify>>> index;
	bool index_dirty = true;
	void rebuild_index();
	static void _bind_methods();

public:
	void set_notifies(const Array &v) {
		notifies = v;
		index_dirty = true;
	}
	Array get_notifies() const {
		return notifies;
	}
	// O(1) lookup of the notifies belonging to one animation (index rebuilt lazily).
	const Vector<Ref<SpineNotify>> &get_notifies_for_animation(const StringName &animation_name);
	// Force the index to rebuild on next lookup (call if a notify's animation_name changed in place).
	void invalidate_index() {
		index_dirty = true;
	}
};
#endif
