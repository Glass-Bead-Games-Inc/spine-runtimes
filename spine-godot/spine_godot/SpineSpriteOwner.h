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

// SpineSpriteOwner.h deliberately does NOT include SpineCommon.h to avoid a
// circular dependency: SpineCommon.h -> SpineSpriteOwner.h -> SpineCommon.h.
// It includes the minimal Godot headers required for Ref<> and Node* directly.
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/node.hpp>
using namespace godot;
#else
#include "core/version.h"
#if VERSION_MAJOR > 3
#include "core/object/ref_counted.h"
#else
#include "core/reference.h"
#endif
#include "scene/main/node.h"
#endif

class SpineSkeleton;
class SpineSkeletonDataResource;
class SpineAnimationState;

// Non-GDCLASS abstract interface implemented by both SpineSprite (Node2D) and
// SpineSprite3D (GeometryInstance3D) so the data layer (SpineSkeleton,
// SpineAnimationState, all wrapper objects) can own either node uniformly.
//
// Multiple-inheritance note: implementers inherit both a Godot node base
// (-> Object) and this interface. A SpineSpriteOwner* and the Object* are
// DIFFERENT addresses. Never C-cast between them. owner_as_node() returns
// `this` correctly adjusted to Node* via the virtual call.
class SpineSpriteOwner {
public:
	virtual ~SpineSpriteOwner() {
	}
	virtual Ref<SpineSkeleton> get_skeleton() = 0;
	virtual Ref<SpineSkeletonDataResource> get_skeleton_data_res() = 0;
	virtual Ref<SpineAnimationState> get_animation_state() = 0;
	virtual void set_modified_bones() = 0;
	virtual Node *owner_as_node() = 0;// returns `this` adjusted to Node*; used for signal connect and 2D casts
};
