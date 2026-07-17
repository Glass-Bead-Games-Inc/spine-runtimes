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
#include "SpineNotify.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

void SpineNotify::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animation_name", "v"), &SpineNotify::set_animation_name);
	ClassDB::bind_method(D_METHOD("get_animation_name"), &SpineNotify::get_animation_name);
	ClassDB::bind_method(D_METHOD("set_time", "v"), &SpineNotify::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &SpineNotify::get_time);
	ClassDB::bind_method(D_METHOD("set_notify_name", "v"), &SpineNotify::set_notify_name);
	ClassDB::bind_method(D_METHOD("get_notify_name"), &SpineNotify::get_notify_name);
	ClassDB::bind_method(D_METHOD("set_channel", "v"), &SpineNotify::set_channel);
	ClassDB::bind_method(D_METHOD("get_channel"), &SpineNotify::get_channel);
	ClassDB::bind_method(D_METHOD("set_payload", "v"), &SpineNotify::set_payload);
	ClassDB::bind_method(D_METHOD("get_payload"), &SpineNotify::get_payload);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "animation_name"), "set_animation_name", "get_animation_name");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "time"), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "notify_name"), "set_notify_name", "get_notify_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "channel"), "set_channel", "get_channel");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "payload"), "set_payload", "get_payload");
}
#endif
