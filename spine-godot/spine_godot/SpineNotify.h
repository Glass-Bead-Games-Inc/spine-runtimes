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
#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/classes/resource.hpp>
#else
#include "core/io/resource.h"
#endif

// One "notify": a named trigger placed at a time on a specific animation.
class SpineNotify : public Resource {
	GDCLASS(SpineNotify, Resource)

protected:
	String animation_name;
	float time = 0.0f;
	String notify_name;
	String channel = "default";
	Dictionary payload;
	static void _bind_methods();

public:
	void set_animation_name(const String &v) { animation_name = v; }
	String get_animation_name() const { return animation_name; }
	void set_time(float v) { time = v; }
	float get_time() const { return time; }
	void set_notify_name(const String &v) { notify_name = v; }
	String get_notify_name() const { return notify_name; }
	void set_channel(const String &v) { channel = v; }
	String get_channel() const { return channel; }
	void set_payload(const Dictionary &v) { payload = v; }
	Dictionary get_payload() const { return payload; }
};
#endif
