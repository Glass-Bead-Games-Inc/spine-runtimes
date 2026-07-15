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
#include "SpineAnimationPlayer.h"
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

#include "SpineEvent.h"
#include "SpineEventData.h"
#include "SpineAnimationState.h"
#include "SpineTrackEntry.h"
#include "SpineAnimation.h"
#include "SpineNotify.h"

void SpineAnimationPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_before_apply", "spine_sprite"), &SpineAnimationPlayer::on_before_apply);
	ClassDB::bind_method(D_METHOD("_on_spine_event", "spine_sprite", "animation_state", "track_entry", "event"), &SpineAnimationPlayer::on_spine_event);
	ClassDB::bind_method(D_METHOD("set_notify_track", "v"), &SpineAnimationPlayer::set_notify_track);
	ClassDB::bind_method(D_METHOD("get_notify_track"), &SpineAnimationPlayer::get_notify_track);
	ClassDB::bind_method(D_METHOD("set_forward_spine_events", "v"), &SpineAnimationPlayer::set_forward_spine_events);
	ClassDB::bind_method(D_METHOD("get_forward_spine_events"), &SpineAnimationPlayer::get_forward_spine_events);
	ClassDB::bind_method(D_METHOD("play", "animation_name", "loop", "track"), &SpineAnimationPlayer::play, DEFVAL(true), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("seek", "time", "track"), &SpineAnimationPlayer::seek, DEFVAL(0));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "notify_track", PROPERTY_HINT_RESOURCE_TYPE, "SpineNotifyTrack"), "set_notify_track", "get_notify_track");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "forward_spine_events"), "set_forward_spine_events", "get_forward_spine_events");

	ADD_SIGNAL(MethodInfo("notified",
						  PropertyInfo(Variant::STRING, "name"),
						  PropertyInfo(VARIANT_FLOAT, "time"),
						  PropertyInfo(Variant::DICTIONARY, "payload")));
}

void SpineAnimationPlayer::_notification(int what) {
	switch (what) {
		case NOTIFICATION_PARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
				sprite->connect(SNAME("before_animation_state_apply"), callable_mp(this, &SpineAnimationPlayer::on_before_apply));
				sprite->connect(SNAME("animation_event"), callable_mp(this, &SpineAnimationPlayer::on_spine_event));
				discontinuity_pending = true;
			} else {
				WARN_PRINT("SpineAnimationPlayer parent is not a SpineSprite3D.");
			}
			break;
		}
		case NOTIFICATION_UNPARENTED: {
			SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
			if (sprite) {
				sprite->disconnect(SNAME("before_animation_state_apply"), callable_mp(this, &SpineAnimationPlayer::on_before_apply));
				sprite->disconnect(SNAME("animation_event"), callable_mp(this, &SpineAnimationPlayer::on_spine_event));
			}
			break;
		}
		default:
			break;
	}
}

void SpineAnimationPlayer::emit_forward(const String &anim, float lo, float hi) {
	if (notify_track.is_null()) return;
	Array notifies = notify_track->get_notifies();
	for (int i = 0; i < notifies.size(); i++) {
		Ref<SpineNotify> n = notifies[i];
		if (n.is_null() || n->get_animation_name() != anim) continue;
		float t = n->get_time();
		if (t > lo && t <= hi) {
			Dictionary payload = n->get_payload().duplicate();
			payload["source"] = "notify";
			emit_signal(SNAME("notified"), n->get_notify_name(), t, payload);
		}
	}
}

void SpineAnimationPlayer::on_before_apply(const Variant &_sprite) {
	SpineSprite3D *sprite = Object::cast_to<SpineSprite3D>(get_parent());
	if (!sprite) return;
	Ref<SpineAnimationState> state = sprite->get_animation_state();
	if (state.is_null() || !state->get_spine_object()) return;

	int n = state->get_num_tracks();
	if (n > MAX_TRACKS) n = MAX_TRACKS;
	for (int i = 0; i < n; i++) {
		Ref<SpineTrackEntry> entry = state->get_track(i);
		if (entry.is_null() || !entry->get_spine_object()) { prev_anim[i] = String(); continue; }
		Ref<SpineAnimation> anim = entry->get_animation();
		if (anim.is_null()) { prev_anim[i] = String(); continue; }
		String anim_name = anim->get_name();
		float cur = entry->get_animation_time();
		float dur = anim->get_duration();

		// Discontinuity: first-seen, animation changed, or a global reset (seek/play). Fire nothing.
		if (discontinuity_pending || prev_anim[i] != anim_name) {
			prev_anim[i] = anim_name;
			prev_time[i] = cur;
			continue;
		}
		float prev = prev_time[i];
		if (cur >= prev) {
			emit_forward(anim_name, prev, cur);// normal forward
		} else if (entry->get_loop()) {
			emit_forward(anim_name, prev, dur);// wrapped: tail of the cycle...
			emit_forward(anim_name, -1.0f, cur);// ...then head of the next
		}
		prev_time[i] = cur;
	}
	discontinuity_pending = false;
}
void SpineAnimationPlayer::on_spine_event(const Variant &_sprite, const Variant &_state, const Variant &_entry, const Variant &_event) {
	if (!forward_spine_events) return;
	Ref<SpineEvent> event = _event;
	if (event.is_null()) return;
	Ref<SpineEventData> data = event->get_data();
	if (data.is_null()) return;
	Dictionary payload;
	payload["source"] = "spine_event";
	payload["int"] = event->get_int_value();
	payload["float"] = event->get_float_value();
	payload["string"] = event->get_string_value();
	payload["volume"] = event->get_volume();
	payload["balance"] = event->get_balance();
	emit_signal(SNAME("notified"), data->get_event_name(), event->get_time(), payload);
}
// Filled in Task 5.
void SpineAnimationPlayer::play(const String &animation_name, bool loop, int track) {}
void SpineAnimationPlayer::seek(float time, int track) {}
#endif
