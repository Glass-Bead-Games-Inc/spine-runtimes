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
#include "SpineSlotNode3D.h"
#include "SpineEvent.h"
#include "SpineTrackEntry.h"
#include "SpineSkeleton.h"
#include "SpineRendererObject.h"

#ifdef SPINE_GODOT_EXTENSION
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/variant.hpp>
#else
#include "scene/resources/shader.h"
#include "scene/resources/shader_material.h"
#include "scene/resources/mesh.h"
#if (VERSION_MAJOR >= 4 && VERSION_MINOR >= 6)
#include "servers/rendering/rendering_server.h"
#else
#include "servers/rendering_server.h"
#endif
#endif

#include <spine/RegionAttachment.h>
#include <spine/MeshAttachment.h>
#include <spine/ClippingAttachment.h>
#include <spine/BoundingBoxAttachment.h>
#include <spine/PathAttachment.h>

// ---------------------------------------------------------------------------
// SpineSprite3DStatics — shader/material singleton for 3D spine rendering.
// Modeled on SpineSpriteStatics in SpineSprite.cpp.
// ---------------------------------------------------------------------------
struct SpineSprite3DStatics {
private:
	static SpineSprite3DStatics *_instance;

	// Build GLSL source for the requested variant.
	// Generates all {Normal, Additive, Multiply} blend x {straight, pma} x {unshaded, shaded} variants.
	static String build_shader_source(spine::BlendMode blend, bool shaded, bool pma) {
		// Blend mode -> render_mode token
		String rm;
		switch (blend) {
			case spine::BlendMode_Additive: rm = "blend_add"; break;
			case spine::BlendMode_Multiply: rm = "blend_mul"; break;
			default: rm = "blend_mix"; break; // Normal (Screen unsupported -> treat as normal)
		}

		// Vertex shader (same for shaded and unshaded)
		String vertex_fn =
			"void vertex() {\n"
			"    if (billboard_mode == 1) {\n"
			"        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(\n"
			"            INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2],\n"
			"            MODEL_MATRIX[3]);\n"
			"        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);\n"
			"    } else if (billboard_mode == 2) {\n"
			"        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(\n"
			"            vec4(normalize(cross(vec3(0.0,1.0,0.0), INV_VIEW_MATRIX[2].xyz)), 0.0),\n"
			"            vec4(0.0,1.0,0.0,0.0),\n"
			"            vec4(normalize(cross(INV_VIEW_MATRIX[0].xyz, vec3(0.0,1.0,0.0))), 0.0),\n"
			"            MODEL_MATRIX[3]);\n"
			"        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);\n"
			"    }\n"
			"}\n";

		if (!shaded) {
			// Unshaded variant: flat rendering, no lighting, no shadow participation.
			String frag = pma
					? "vec4 tex = texture(albedo_tex, UV); vec3 c = tex.rgb * COLOR.rgb; ALBEDO = c; ALPHA = tex.a * COLOR.a;"
					: "vec4 tex = texture(albedo_tex, UV); ALBEDO = tex.rgb * COLOR.rgb; ALPHA = tex.a * COLOR.a;";

			return String("shader_type spatial;\n") +
				   "render_mode " + rm + ", cull_disabled, unshaded, depth_draw_opaque, shadows_disabled;\n"
				   "\n"
				   "uniform sampler2D albedo_tex : source_color, filter_linear_mipmap;\n"
				   "uniform int billboard_mode = 0; // 0 disabled, 1 enabled, 2 y\n"
				   "\n" +
				   vertex_fn +
				   "\n"
				   "void fragment() {\n"
				   "    " + frag + "\n"
				   "}\n";
		} else {
			// Shaded variant: participates in lighting and shadows.
			// PMA handling same as unshaded; normal/specular maps are optional.
			String albedo_alpha = pma
					? "vec4 tex = texture(albedo_tex, UV); vec3 c = tex.rgb * COLOR.rgb; ALBEDO = c; ALPHA = tex.a * COLOR.a;"
					: "vec4 tex = texture(albedo_tex, UV); ALBEDO = tex.rgb * COLOR.rgb; ALPHA = tex.a * COLOR.a;";

			return String("shader_type spatial;\n") +
				   "render_mode " + rm + ", cull_disabled, depth_draw_opaque;\n"
				   "\n"
				   "uniform sampler2D albedo_tex : source_color, filter_linear_mipmap;\n"
				   "uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap;\n"
				   "uniform sampler2D specular_tex : source_color, filter_linear_mipmap;\n"
				   "uniform bool use_normal_tex = false;\n"
				   "uniform bool use_specular_tex = false;\n"
				   "uniform int billboard_mode = 0; // 0 disabled, 1 enabled, 2 y\n"
				   "\n" +
				   vertex_fn +
				   "\n"
				   "void fragment() {\n"
				   "    " + albedo_alpha + "\n"
				   "    if (use_normal_tex) NORMAL_MAP = texture(normal_tex, UV).rgb;\n"
				   "    if (use_specular_tex) SPECULAR = texture(specular_tex, UV).r;\n"
				   "}\n";
		}
	}

	static Ref<ShaderMaterial> make_material(spine::BlendMode blend, bool shaded, bool pma) {
		Ref<Shader> shader;
		shader.instantiate();
		shader->set_code(build_shader_source(blend, shaded, pma));

		Ref<ShaderMaterial> mat;
		mat.instantiate();
		mat->set_shader(shader);
		return mat;
	}

	// Build shader source for the debug lines overlay.
	// Unshaded, vertex_color_use_as_albedo, no depth test so lines always show through.
	// Includes the same billboard vertex() transform as the main render shader so that
	// on billboarded sprites the debug lines track the rendered geometry.
	static String build_lines_shader_source() {
		return String(
			"shader_type spatial;\n"
			"render_mode unshaded, cull_disabled, depth_test_disabled;\n"
			"\n"
			"uniform int billboard_mode = 0; // 0 disabled, 1 enabled, 2 y\n"
			"\n"
			"void vertex() {\n"
			"    if (billboard_mode == 1) {\n"
			"        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(\n"
			"            INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2],\n"
			"            MODEL_MATRIX[3]);\n"
			"        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);\n"
			"    } else if (billboard_mode == 2) {\n"
			"        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(\n"
			"            vec4(normalize(cross(vec3(0.0,1.0,0.0), INV_VIEW_MATRIX[2].xyz)), 0.0),\n"
			"            vec4(0.0,1.0,0.0,0.0),\n"
			"            vec4(normalize(cross(INV_VIEW_MATRIX[0].xyz, vec3(0.0,1.0,0.0))), 0.0),\n"
			"            MODEL_MATRIX[3]);\n"
			"        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);\n"
			"    }\n"
			"}\n"
			"\n"
			"void fragment() {\n"
			"    ALBEDO = COLOR.rgb;\n"
			"    ALPHA = COLOR.a;\n"
			"}\n");
	}

	static Ref<Shader> make_lines_shader() {
		Ref<Shader> shader;
		shader.instantiate();
		shader->set_code(build_lines_shader_source());
		return shader;
	}

public:
	// Cache key: blend * 4 + shaded * 2 + pma  (max index = 3*4+2+1 = 15)
	Ref<ShaderMaterial> materials[16];
	// Task 11: shared lines shader (billboard-aware); each sprite clones its own material.
	Ref<Shader> lines_shader;

	SpineSprite3DStatics() {
		// Variants are built lazily on first get_material() call.
	}

	Ref<Shader> get_lines_shader() {
		if (!lines_shader.is_valid()) {
			lines_shader = make_lines_shader();
		}
		return lines_shader;
	}

	Ref<ShaderMaterial> get_material(spine::BlendMode blend, bool shaded, bool pma) {
		int key = (int) blend * 4 + (shaded ? 2 : 0) + (pma ? 1 : 0);
		if (!materials[key].is_valid()) {
			materials[key] = make_material(blend, shaded, pma);
		}
		return materials[key];
	}

	static SpineSprite3DStatics &instance() {
		if (!_instance) {
			_instance = new SpineSprite3DStatics();
		}
		return *_instance;
	}

	static void clear() {
		if (_instance) {
			delete _instance;
		}
		_instance = nullptr;
	}
};

SpineSprite3DStatics *SpineSprite3DStatics::_instance = nullptr;

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
	ClassDB::bind_method(D_METHOD("set_pixel_size", "v"), &SpineSprite3D::set_pixel_size);
	ClassDB::bind_method(D_METHOD("get_pixel_size"), &SpineSprite3D::get_pixel_size);
	ClassDB::bind_method(D_METHOD("set_z_spacing", "v"), &SpineSprite3D::set_z_spacing);
	ClassDB::bind_method(D_METHOD("get_z_spacing"), &SpineSprite3D::get_z_spacing);
	ClassDB::bind_method(D_METHOD("set_flip_h", "v"), &SpineSprite3D::set_flip_h);
	ClassDB::bind_method(D_METHOD("get_flip_h"), &SpineSprite3D::get_flip_h);
	ClassDB::bind_method(D_METHOD("set_flip_v", "v"), &SpineSprite3D::set_flip_v);
	ClassDB::bind_method(D_METHOD("get_flip_v"), &SpineSprite3D::get_flip_v);
	ClassDB::bind_method(D_METHOD("set_billboard", "v"), &SpineSprite3D::set_billboard);
	ClassDB::bind_method(D_METHOD("get_billboard"), &SpineSprite3D::get_billboard);
	BIND_ENUM_CONSTANT(BILLBOARD_DISABLED);
	BIND_ENUM_CONSTANT(BILLBOARD_ENABLED);
	BIND_ENUM_CONSTANT(BILLBOARD_Y);
	ClassDB::bind_method(D_METHOD("set_shaded", "v"), &SpineSprite3D::set_shaded);
	ClassDB::bind_method(D_METHOD("get_shaded"), &SpineSprite3D::get_shaded);

	ClassDB::bind_method(D_METHOD("set_normal_material", "material"), &SpineSprite3D::set_normal_material);
	ClassDB::bind_method(D_METHOD("get_normal_material"), &SpineSprite3D::get_normal_material);
	ClassDB::bind_method(D_METHOD("set_additive_material", "material"), &SpineSprite3D::set_additive_material);
	ClassDB::bind_method(D_METHOD("get_additive_material"), &SpineSprite3D::get_additive_material);
	ClassDB::bind_method(D_METHOD("set_multiply_material", "material"), &SpineSprite3D::set_multiply_material);
	ClassDB::bind_method(D_METHOD("get_multiply_material"), &SpineSprite3D::get_multiply_material);
	ClassDB::bind_method(D_METHOD("set_screen_material", "material"), &SpineSprite3D::set_screen_material);
	ClassDB::bind_method(D_METHOD("get_screen_material"), &SpineSprite3D::get_screen_material);

	ClassDB::bind_method(D_METHOD("get_global_bone_transform_3d", "bone_name"), &SpineSprite3D::get_global_bone_transform_3d);
	ClassDB::bind_method(D_METHOD("set_global_bone_transform_3d", "bone_name", "xform"), &SpineSprite3D::set_global_bone_transform_3d);

	// Task 11: debug overlay bindings
	ClassDB::bind_method(D_METHOD("set_debug_root", "v"), &SpineSprite3D::set_debug_root);
	ClassDB::bind_method(D_METHOD("get_debug_root"), &SpineSprite3D::get_debug_root);
	ClassDB::bind_method(D_METHOD("set_debug_root_color", "v"), &SpineSprite3D::set_debug_root_color);
	ClassDB::bind_method(D_METHOD("get_debug_root_color"), &SpineSprite3D::get_debug_root_color);
	ClassDB::bind_method(D_METHOD("set_debug_bones", "v"), &SpineSprite3D::set_debug_bones);
	ClassDB::bind_method(D_METHOD("get_debug_bones"), &SpineSprite3D::get_debug_bones);
	ClassDB::bind_method(D_METHOD("set_debug_bones_color", "v"), &SpineSprite3D::set_debug_bones_color);
	ClassDB::bind_method(D_METHOD("get_debug_bones_color"), &SpineSprite3D::get_debug_bones_color);
	ClassDB::bind_method(D_METHOD("set_debug_bones_thickness", "v"), &SpineSprite3D::set_debug_bones_thickness);
	ClassDB::bind_method(D_METHOD("get_debug_bones_thickness"), &SpineSprite3D::get_debug_bones_thickness);
	ClassDB::bind_method(D_METHOD("set_debug_regions", "v"), &SpineSprite3D::set_debug_regions);
	ClassDB::bind_method(D_METHOD("get_debug_regions"), &SpineSprite3D::get_debug_regions);
	ClassDB::bind_method(D_METHOD("set_debug_regions_color", "v"), &SpineSprite3D::set_debug_regions_color);
	ClassDB::bind_method(D_METHOD("get_debug_regions_color"), &SpineSprite3D::get_debug_regions_color);
	ClassDB::bind_method(D_METHOD("set_debug_meshes", "v"), &SpineSprite3D::set_debug_meshes);
	ClassDB::bind_method(D_METHOD("get_debug_meshes"), &SpineSprite3D::get_debug_meshes);
	ClassDB::bind_method(D_METHOD("set_debug_meshes_color", "v"), &SpineSprite3D::set_debug_meshes_color);
	ClassDB::bind_method(D_METHOD("get_debug_meshes_color"), &SpineSprite3D::get_debug_meshes_color);
	ClassDB::bind_method(D_METHOD("set_debug_bounding_boxes", "v"), &SpineSprite3D::set_debug_bounding_boxes);
	ClassDB::bind_method(D_METHOD("get_debug_bounding_boxes"), &SpineSprite3D::get_debug_bounding_boxes);
	ClassDB::bind_method(D_METHOD("set_debug_bounding_boxes_color", "v"), &SpineSprite3D::set_debug_bounding_boxes_color);
	ClassDB::bind_method(D_METHOD("get_debug_bounding_boxes_color"), &SpineSprite3D::get_debug_bounding_boxes_color);
	ClassDB::bind_method(D_METHOD("set_debug_paths", "v"), &SpineSprite3D::set_debug_paths);
	ClassDB::bind_method(D_METHOD("get_debug_paths"), &SpineSprite3D::get_debug_paths);
	ClassDB::bind_method(D_METHOD("set_debug_paths_color", "v"), &SpineSprite3D::set_debug_paths_color);
	ClassDB::bind_method(D_METHOD("get_debug_paths_color"), &SpineSprite3D::get_debug_paths_color);
	ClassDB::bind_method(D_METHOD("set_debug_clipping", "v"), &SpineSprite3D::set_debug_clipping);
	ClassDB::bind_method(D_METHOD("get_debug_clipping"), &SpineSprite3D::get_debug_clipping);
	ClassDB::bind_method(D_METHOD("set_debug_clipping_color", "v"), &SpineSprite3D::set_debug_clipping_color);
	ClassDB::bind_method(D_METHOD("get_debug_clipping_color"), &SpineSprite3D::get_debug_clipping_color);

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
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "pixel_size", PROPERTY_HINT_RANGE, "0.0001,1,0.0001"), "set_pixel_size", "get_pixel_size");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "z_spacing", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_z_spacing", "get_z_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_h"), "set_flip_h", "get_flip_h");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_v"), "set_flip_v", "get_flip_v");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "billboard", PROPERTY_HINT_ENUM, "Disabled,Enabled,Y-Billboard"), "set_billboard", "get_billboard");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shaded"), "set_shaded", "get_shaded");
	ADD_GROUP("Materials", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_normal_material",
				 "get_normal_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "additive_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_additive_material",
				 "get_additive_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiply_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_multiply_material",
				 "get_multiply_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "screen_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_screen_material",
				 "get_screen_material");
	ADD_GROUP("Debug", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "root"), "set_debug_root", "get_debug_root");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "root_color"), "set_debug_root_color", "get_debug_root_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bones"), "set_debug_bones", "get_debug_bones");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bones_color"), "set_debug_bones_color", "get_debug_bones_color");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "bones_thickness"), "set_debug_bones_thickness", "get_debug_bones_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "regions"), "set_debug_regions", "get_debug_regions");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "regions_color"), "set_debug_regions_color", "get_debug_regions_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "meshes"), "set_debug_meshes", "get_debug_meshes");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "meshes_color"), "set_debug_meshes_color", "get_debug_meshes_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounding_boxes"), "set_debug_bounding_boxes", "get_debug_bounding_boxes");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bounding_boxes_color"), "set_debug_bounding_boxes_color", "get_debug_bounding_boxes_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paths"), "set_debug_paths", "get_debug_paths");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "paths_color"), "set_debug_paths_color", "get_debug_paths_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clipping"), "set_debug_clipping", "get_debug_clipping");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "paths_clipping"), "set_debug_clipping_color", "get_debug_clipping_color");
	ADD_GROUP("Preview", "");
}

SpineSprite3D::SpineSprite3D()
	: update_mode(SpineConstant::UpdateMode_Process), time_scale(1.0), skeleton_clipper(new spine::SkeletonClipping()), modified_bones(false),
	  pixel_size(0.01f), z_spacing(0.0f), flip_h(false), flip_v(false), billboard(BILLBOARD_DISABLED), shaded(false),
	  preview_skin("Default"), preview_animation("-- Empty --"), preview_frame(false), preview_time(0),
	  // Task 11: debug overlay defaults (same as SpineSprite 2D)
	  debug_root(false), debug_root_color(Color(1, 1, 1, 0.5f)),
	  debug_bones(false), debug_bones_color(Color(1, 1, 0, 0.5f)), debug_bones_thickness(5.0f),
	  debug_regions(false), debug_regions_color(Color(0, 0, 1, 0.5f)),
	  debug_meshes(false), debug_meshes_color(Color(0, 0, 1, 0.5f)),
	  debug_bounding_boxes(false), debug_bounding_boxes_color(Color(0, 1, 0, 0.5f)),
	  debug_paths(false), debug_paths_color(Color::hex(0xff7f0077)),
	  debug_clipping(false), debug_clipping_color(Color(0.8f, 0, 0, 0.8f)) {
	scratch_world_verts.ensureCapacity(1200);
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
	// Task 4: clear per-instance material cache; textures change with skeleton data
	material_cache.clear();
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
	build_debug_mesh(); // Task 11: rebuild debug line overlay
}

void SpineSprite3D::build_meshes() {
	// Full rebuild every frame (simple approach from Task 2).
	if (mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->free_rid(mesh);
#else
		RS::get_singleton()->free(mesh);
#endif
		mesh = RID();
	}

	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;
	spine::Skeleton *sk = skeleton->get_spine_object();
	auto &statics = SpineSprite3DStatics::instance();

	mesh = RS::get_singleton()->mesh_create();
	AABB aabb;
	bool aabb_init = false;
	int surface_index = 0;

	// Task 9: Build slot_index -> SpineSlotNode3D* lookup for per-slot material batch-break.
	HashMap<int, SpineSlotNode3D *> slot_node_map;
	for (int ci = 0; ci < get_child_count(); ci++) {
		SpineSlotNode3D *sn = Object::cast_to<SpineSlotNode3D>(get_child(ci));
		if (sn && sn->get_slot_index() >= 0) {
			slot_node_map[sn->get_slot_index()] = sn;
		}
	}

	SpineRendererObject *current_ro = nullptr;
	// Task 4: track current blend + pma for batch breaking
	spine::BlendMode current_blend = spine::BlendMode_Normal;
	bool current_pma = false;
	// Task 9: track which slot node (if any) owns the current surface
	SpineSlotNode3D *current_slot_node = nullptr;

	// Reset scratch buffers.
	scratch_positions.clear();
	scratch_uvs.clear();
	scratch_colors.clear();
	scratch_indices.clear();

	// Task 4: Flush the accumulated scratch buffers as one surface.
	// Per-instance material cache: look up or create a ShaderMaterial for (variant, texture).
	// Key: high byte = blend*4 + shaded*2 + pma; lower 56 bits = texture RID id.
	// This ensures each surface gets its own correctly-textured material independent of other
	// SpineSprite3D instances — fixing the Task 2 shared-material / last-write-wins bug.
	auto flush = [&]() {
		if (scratch_indices.size() == 0) return;

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = scratch_positions;
		arrays[Mesh::ARRAY_TEX_UV] = scratch_uvs;
		arrays[Mesh::ARRAY_COLOR] = scratch_colors;
		arrays[Mesh::ARRAY_INDEX] = scratch_indices;

		// Task 6: shaded surfaces need per-vertex normals and tangents so the spatial shader
		// can write NORMAL_MAP without triggering Godot's "mesh missing tangents" warning.
		// The card faces +Z in local space; billboard reorients toward camera at render time.
		// Unshaded surfaces skip these arrays to avoid unnecessary vertex data.
		if (shaded) {
			int vc = (int) scratch_positions.size();
#ifdef SPINE_GODOT_EXTENSION
			PackedVector3Array normals;
			PackedFloat32Array tangents;
			normals.resize(vc);
			tangents.resize(vc * 4);
			for (int ni = 0; ni < vc; ni++) {
				normals[ni] = Vector3(0, 0, 1);
				tangents[ni * 4 + 0] = 1.0f; // tangent X
				tangents[ni * 4 + 1] = 0.0f; // tangent Y
				tangents[ni * 4 + 2] = 0.0f; // tangent Z
				tangents[ni * 4 + 3] = 1.0f; // binormal sign
			}
#else
			Vector<Vector3> normals;
			Vector<float> tangents;
			normals.resize(vc);
			tangents.resize(vc * 4);
			for (int ni = 0; ni < vc; ni++) {
				normals.write[ni] = Vector3(0, 0, 1);
				tangents.write[ni * 4 + 0] = 1.0f;
				tangents.write[ni * 4 + 1] = 0.0f;
				tangents.write[ni * 4 + 2] = 0.0f;
				tangents.write[ni * 4 + 3] = 1.0f;
			}
#endif
			arrays[Mesh::ARRAY_NORMAL] = normals;
			arrays[Mesh::ARRAY_TANGENT] = tangents;
		}

		RS::get_singleton()->mesh_add_surface_from_arrays(mesh, RS::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
				RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE);

		// Task 9: slot-node material override takes precedence over sprite-level (Task 8) and library clone.
		Ref<Material> custom_mat;
		if (current_slot_node) {
			switch (current_blend) {
				case spine::BlendMode_Normal:   custom_mat = current_slot_node->get_normal_material(); break;
				case spine::BlendMode_Additive: custom_mat = current_slot_node->get_additive_material(); break;
				case spine::BlendMode_Multiply: custom_mat = current_slot_node->get_multiply_material(); break;
				default: custom_mat = current_slot_node->get_screen_material(); break;
			}
		}
		// Task 8: fall back to sprite-level per-blend-mode custom material.
		if (!custom_mat.is_valid()) {
			switch (current_blend) {
				case spine::BlendMode_Normal:   custom_mat = normal_material; break;
				case spine::BlendMode_Additive: custom_mat = additive_material; break;
				case spine::BlendMode_Multiply: custom_mat = multiply_material; break;
				default: custom_mat = screen_material; break; // Screen (rare/none in practice)
			}
		}

		if (custom_mat.is_valid()) {
			// User-owned material: use as-is; do NOT set albedo_tex or maps on it.
			RS::get_singleton()->mesh_surface_set_material(mesh, surface_index, custom_mat->get_rid());
		} else if (current_ro && current_ro->texture.is_valid()) {
			// Build cache key: variant bits in top byte, texture RID in lower 56 bits
			uint64_t variant_bits = (uint64_t)((int)current_blend * 4 + (shaded ? 2 : 0) + (current_pma ? 1 : 0));
			uint64_t tex_id = (uint64_t)current_ro->texture->get_rid().get_id();
			uint64_t cache_key = (variant_bits << 56) | (tex_id & 0x00FFFFFFFFFFFFFFull);

			Ref<ShaderMaterial> mat;
			if (material_cache.has(cache_key)) {
				mat = material_cache[cache_key];
			} else {
				// Clone the shared shader variant into a fresh per-(variant,texture) material
				Ref<ShaderMaterial> variant_mat = statics.get_material(current_blend, shaded, current_pma);
				mat.instantiate();
				mat->set_shader(variant_mat->get_shader());
				mat->set_shader_parameter("albedo_tex", current_ro->texture);
				mat->set_shader_parameter("billboard_mode", (int) billboard);
				if (shaded) {
					bool has_normal = current_ro->normal_map.is_valid();
					bool has_specular = current_ro->specular_map.is_valid();
					if (has_normal) mat->set_shader_parameter("normal_tex", current_ro->normal_map);
					mat->set_shader_parameter("use_normal_tex", has_normal);
					if (has_specular) mat->set_shader_parameter("specular_tex", current_ro->specular_map);
					mat->set_shader_parameter("use_specular_tex", has_specular);
				}
				material_cache[cache_key] = mat;
			}
			RS::get_singleton()->mesh_surface_set_material(mesh, surface_index, mat->get_rid());
		}
		surface_index++;

		// Reset scratch for next surface.
		scratch_positions.clear();
		scratch_uvs.clear();
		scratch_colors.clear();
		scratch_indices.clear();
	};

	for (int i = 0, n = (int) sk->getSlots().size(); i < n; i++) {
		spine::Slot *slot = sk->getDrawOrder().getAppliedPose()[i];
		spine::Attachment *attachment = slot->getAppliedPose().getAttachment();

		if (!attachment || !slot->getBone().isActive()) {
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		spine::Color sk_color = sk->getColor();
		spine::Color slot_color = slot->getAppliedPose().getColor();
		spine::Color tint(sk_color.r * slot_color.r, sk_color.g * slot_color.g,
				sk_color.b * slot_color.b, sk_color.a * slot_color.a);

		// Task 4: get blend mode from slot data
		spine::BlendMode slot_blend = slot->getData().getBlendMode();

		SpineRendererObject *ro = nullptr;
		spine::Array<float> *world_verts = &scratch_world_verts;
		spine::Array<float> *uvs = nullptr;
		spine::Array<unsigned short> *indices = nullptr;
		spine::AtlasPage *atlas_page = nullptr;

		if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
			auto region = (spine::RegionAttachment *) attachment;
			auto &sequence = region->getSequence();
			int seq_index = sequence.resolveIndex(slot->getAppliedPose());

			world_verts->setSize(8, 0);
			region->computeWorldVertices(*slot, sequence.getOffsets(seq_index).buffer(), world_verts->buffer(), 0);
			auto *atlas_region = (spine::AtlasRegion *) sequence.getRegion(seq_index);
			atlas_page = atlas_region->getPage();
			ro = (SpineRendererObject *) atlas_page->texture;
			uvs = &sequence.getUVs(seq_index);

			// Build quad indices for region attachments.
			static spine::Array<unsigned short> quad_idx;
			if (quad_idx.size() == 0) {
				quad_idx.setSize(6, 0);
				quad_idx[0] = 0; quad_idx[1] = 1; quad_idx[2] = 2;
				quad_idx[3] = 2; quad_idx[4] = 3; quad_idx[5] = 0;
			}
			indices = &quad_idx;

			auto &att_color = region->getColor();
			tint.r *= att_color.r;
			tint.g *= att_color.g;
			tint.b *= att_color.b;
			tint.a *= att_color.a;
		} else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
			auto mesh_att = (spine::MeshAttachment *) attachment;
			auto &sequence = mesh_att->getSequence();
			int seq_index = sequence.resolveIndex(slot->getAppliedPose());

			world_verts->setSize(mesh_att->getWorldVerticesLength(), 0);
			mesh_att->computeWorldVertices(*sk, *slot, 0, mesh_att->getWorldVerticesLength(), world_verts->buffer(), 0, 2);
			auto *atlas_region = (spine::AtlasRegion *) sequence.getRegion(seq_index);
			atlas_page = atlas_region->getPage();
			ro = (SpineRendererObject *) atlas_page->texture;
			uvs = &sequence.getUVs(seq_index);
			indices = &mesh_att->getTriangles();

			auto &att_color = mesh_att->getColor();
			tint.r *= att_color.r;
			tint.g *= att_color.g;
			tint.b *= att_color.b;
			tint.a *= att_color.a;
		} else if (attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti)) {
			auto clip = (spine::ClippingAttachment *) attachment;
			skeleton_clipper->clipStart(*sk, *slot, clip);
			continue;
		} else {
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		if (!ro || !uvs || !indices || indices->size() == 0) {
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		if (skeleton_clipper->isClipping()) {
			skeleton_clipper->clipTriangles(*world_verts, *indices, *uvs, 2);
			if (skeleton_clipper->getClippedTriangles().size() == 0) {
				skeleton_clipper->clipEnd(*slot);
				continue;
			}
			world_verts = &skeleton_clipper->getClippedVertices();
			uvs = &skeleton_clipper->getClippedUVs();
			indices = &skeleton_clipper->getClippedTriangles();
		}

		// Task 4: read PMA from atlas page
		bool slot_pma = atlas_page ? atlas_page->pma : false;

		// Task 9: look up whether this slot has a slot-node with a custom material.
		// Use the slot's setup-pose DATA index (not draw-order loop index i) so the
		// lookup matches the map key built from get_slot_index() = getData().getIndex().
		int slot_data_idx = slot->getData().getIndex();
		SpineSlotNode3D *this_slot_node = slot_node_map.has(slot_data_idx) ? slot_node_map[slot_data_idx] : nullptr;

		// Task 4: Flush if we switch texture page, blend mode, or PMA flag.
		// Task 9: Also flush if the slot-node material changes (entering or leaving a slot-node surface).
		if (current_ro && (ro != current_ro || slot_blend != current_blend || slot_pma != current_pma || this_slot_node != current_slot_node)) {
			flush();
		}
		current_ro = ro;
		current_blend = slot_blend;
		current_pma = slot_pma;
		current_slot_node = this_slot_node;

		int base = (int) scratch_positions.size();
		int num_verts = (int) world_verts->size() / 2;
		float z = -((float) i) * z_spacing;

		float sx = flip_h ? -pixel_size : pixel_size;
		float sy = flip_v ? pixel_size : -pixel_size; // base is -pixel_size (Y-flip); flip_v cancels it

		for (int v = 0; v < num_verts; v++) {
			float x = world_verts->buffer()[v * 2] * sx;
			float y = world_verts->buffer()[v * 2 + 1] * sy;
			Vector3 pos(x, y, z);
			scratch_positions.push_back(pos);
			scratch_uvs.push_back(Vector2(uvs->buffer()[v * 2], uvs->buffer()[v * 2 + 1]));
			scratch_colors.push_back(Color(tint.r, tint.g, tint.b, tint.a));

			if (!aabb_init) {
				aabb.position = pos;
				aabb.size = Vector3();
				aabb_init = true;
			} else {
				aabb.expand_to(pos);
			}
		}

		for (int t = 0; t < (int) indices->size(); t++) {
			scratch_indices.push_back(base + (int) indices->buffer()[t]);
		}

		skeleton_clipper->clipEnd(*slot);
	}
	skeleton_clipper->clipEnd();

	flush(); // flush final surface

	if (aabb_init) {
		// Task 5: expand to cube when billboarding so rotation never causes frustum culling
		if (billboard != BILLBOARD_DISABLED) {
			float r = MAX(aabb.size.x, MAX(aabb.size.y, aabb.size.z));
			Vector3 c = aabb.position + aabb.size * 0.5f;
			aabb = AABB(c - Vector3(r, r, r), Vector3(2 * r, 2 * r, 2 * r));
		}
		RS::get_singleton()->mesh_set_custom_aabb(mesh, aabb);
	}
	set_base(mesh);
}

// ---------------------------------------------------------------------------
// Task 11: build_debug_mesh — rebuild the PRIMITIVE_LINES debug overlay.
//
// Strategy: accumulate line-segment pairs in dbg_positions / dbg_colors, then
// emit a single PRIMITIVE_LINES surface on debug_mesh.  Each vertex is:
//   Vector3(x * sx, y * sy, z_epsilon)
// where sx/sy mirror the render-path flip convention and z_epsilon keeps the
// lines in front of the attachment quads.
//
// NOTE: debug_bones_thickness has no visual effect in 3D — PRIMITIVE_LINES are
// always 1px regardless of thickness value.  The property is kept for API
// parity with SpineSprite 2D.
// ---------------------------------------------------------------------------
void SpineSprite3D::build_debug_mesh() {
	// The primary mesh is recreated from scratch by build_meshes() every frame.
	// We append the debug lines as an additional PRIMITIVE_LINES surface on that same mesh.
	// No separate RID management needed here — mesh cleanup is handled by build_meshes().

	// Early-out when nothing is enabled or skeleton not ready.
	bool any_enabled = debug_root || debug_bones || debug_regions || debug_meshes ||
					   debug_bounding_boxes || debug_paths || debug_clipping;
	if (!any_enabled) return;
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;

	spine::Skeleton *sk = skeleton->get_spine_object();
	auto &statics = SpineSprite3DStatics::instance();

	// Coordinate conventions matching build_meshes():
	float sx = flip_h ? -pixel_size : pixel_size;
	float sy = flip_v ? pixel_size : -pixel_size;
	// Small Z offset so debug lines sit in front of the attachment geometry.
	const float z_eps = 0.001f;

	// Line-segment scratch buffers.
#ifdef SPINE_GODOT_EXTENSION
	PackedVector3Array dbg_positions;
	PackedColorArray  dbg_colors;
#else
	Vector<Vector3> dbg_positions;
	Vector<Color>   dbg_colors;
#endif

	// Helper: emit one line segment between two 2D Spine world-space points.
	auto emit_line = [&](float x0, float y0, float x1, float y1, const Color &col) {
		dbg_positions.push_back(Vector3(x0 * sx, y0 * sy, z_eps));
		dbg_colors.push_back(col);
		dbg_positions.push_back(Vector3(x1 * sx, y1 * sy, z_eps));
		dbg_colors.push_back(col);
	};

	// Helper: emit a closed polygon outline (last vertex connects back to first).
	// verts: flat [x0,y0,x1,y1,...] with stride 2.
	auto emit_polygon = [&](float *verts, int num_verts, const Color &col) {
		if (num_verts < 2) return;
		for (int i = 0; i < num_verts - 1; i++) {
			emit_line(verts[i * 2], verts[i * 2 + 1],
					  verts[(i + 1) * 2], verts[(i + 1) * 2 + 1], col);
		}
		// Close: last -> first
		emit_line(verts[(num_verts - 1) * 2], verts[(num_verts - 1) * 2 + 1],
				  verts[0], verts[1], col);
	};

	// Helper: emit triangle-mesh edges (non-deduplicated — sufficient for debug).
	auto emit_mesh_triangles = [&](spine::Array<unsigned short> &triangles, float *verts, const Color &col) {
		for (int i = 0; i < triangles.size(); i += 3) {
			int i0 = triangles[i], i1 = triangles[i + 1], i2 = triangles[i + 2];
			emit_line(verts[i0 * 2], verts[i0 * 2 + 1], verts[i1 * 2], verts[i1 * 2 + 1], col);
			emit_line(verts[i1 * 2], verts[i1 * 2 + 1], verts[i2 * 2], verts[i2 * 2 + 1], col);
			emit_line(verts[i2 * 2], verts[i2 * 2 + 1], verts[i0 * 2], verts[i0 * 2 + 1], col);
		}
	};

	auto &draw_order = sk->getDrawOrder().getAppliedPose();

	// --- Regions ---
	if (debug_regions) {
		for (int i = 0; i < (int) draw_order.size(); i++) {
			spine::Slot *slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			spine::Attachment *att = slot->getAppliedPose().getAttachment();
			if (!att || !att->getRTTI().isExactly(spine::RegionAttachment::rtti)) continue;
			auto region = (spine::RegionAttachment *) att;
			auto &seq = region->getSequence();
			int seq_idx = seq.resolveIndex(slot->getAppliedPose());
			scratch_world_verts.setSize(8, 0);
			region->computeWorldVertices(*slot, seq.getOffsets(seq_idx).buffer(), scratch_world_verts.buffer(), 0);
			float *v = scratch_world_verts.buffer();
			// Quad hull (4 corners)
			emit_polygon(v, 4, debug_regions_color);
			// Triangle edges (2 triangles of the quad: 0,1,2 and 2,3,0)
			emit_line(v[0], v[1], v[4], v[5], debug_regions_color);
			emit_line(v[4], v[5], v[2], v[3], debug_regions_color);
		}
	}

	// --- Meshes ---
	if (debug_meshes) {
		for (int i = 0; i < (int) draw_order.size(); i++) {
			spine::Slot *slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			spine::Attachment *att = slot->getAppliedPose().getAttachment();
			if (!att || !att->getRTTI().isExactly(spine::MeshAttachment::rtti)) continue;
			auto mesh_att = (spine::MeshAttachment *) att;
			auto &seq = mesh_att->getSequence();
			int seq_idx = seq.resolveIndex(slot->getAppliedPose());
			int len = mesh_att->getWorldVerticesLength();
			scratch_world_verts.setSize(len, 0);
			mesh_att->computeWorldVertices(*sk, *slot, 0, len, scratch_world_verts.buffer(), 0, 2);
			float *v = scratch_world_verts.buffer();
			// Triangle edges
			emit_mesh_triangles(mesh_att->getTriangles(), v, debug_meshes_color);
			// Hull outline
			int hull_len = mesh_att->getHullLength();
			if (hull_len >= 2) {
				emit_polygon(v, hull_len, debug_meshes_color);
			}
		}
	}

	// --- Bounding boxes ---
	if (debug_bounding_boxes) {
		for (int i = 0; i < (int) draw_order.size(); i++) {
			spine::Slot *slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			spine::Attachment *att = slot->getAppliedPose().getAttachment();
			if (!att || !att->getRTTI().isExactly(spine::BoundingBoxAttachment::rtti)) continue;
			auto bbox = (spine::BoundingBoxAttachment *) att;
			int len = bbox->getWorldVerticesLength();
			scratch_world_verts.setSize(len, 0);
			bbox->computeWorldVertices(*sk, *slot, 0, len, scratch_world_verts.buffer(), 0, 2);
			int num_verts = len / 2;
			emit_polygon(scratch_world_verts.buffer(), num_verts, debug_bounding_boxes_color);
		}
	}

	// --- Paths ---
	if (debug_paths) {
		for (int i = 0; i < (int) draw_order.size(); i++) {
			spine::Slot *slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			spine::Attachment *att = slot->getAppliedPose().getAttachment();
			if (!att || !att->getRTTI().isExactly(spine::PathAttachment::rtti)) continue;
			auto path_att = (spine::PathAttachment *) att;
			int len = path_att->getWorldVerticesLength();
			scratch_world_verts.setSize(len, 0);
			path_att->computeWorldVertices(*sk, *slot, 0, len, scratch_world_verts.buffer(), 0, 2);
			int num_verts = len / 2;
			if (num_verts >= 2) {
				bool closed = path_att->getClosed();
				for (int vi = 0; vi < num_verts - 1; vi++) {
					emit_line(scratch_world_verts.buffer()[vi * 2], scratch_world_verts.buffer()[vi * 2 + 1],
							  scratch_world_verts.buffer()[(vi + 1) * 2], scratch_world_verts.buffer()[(vi + 1) * 2 + 1],
							  debug_paths_color);
				}
				if (closed) {
					emit_line(scratch_world_verts.buffer()[(num_verts - 1) * 2], scratch_world_verts.buffer()[(num_verts - 1) * 2 + 1],
							  scratch_world_verts.buffer()[0], scratch_world_verts.buffer()[1], debug_paths_color);
				}
			}
		}
	}

	// --- Clipping ---
	if (debug_clipping) {
		for (int i = 0; i < (int) draw_order.size(); i++) {
			spine::Slot *slot = draw_order[i];
			if (!slot->getBone().isActive()) continue;
			spine::Attachment *att = slot->getAppliedPose().getAttachment();
			if (!att || !att->getRTTI().isExactly(spine::ClippingAttachment::rtti)) continue;
			auto clip = (spine::ClippingAttachment *) att;
			int len = clip->getWorldVerticesLength();
			scratch_world_verts.setSize(len, 0);
			clip->computeWorldVertices(*sk, *slot, 0, len, scratch_world_verts.buffer(), 0, 2);
			int num_verts = len / 2;
			emit_polygon(scratch_world_verts.buffer(), num_verts, debug_clipping_color);
		}
	}

	// Helper: draw one bone as a line from world origin to tip along the bone's local X axis.
	// Uses the bone's world matrix (a,b = X-axis direction) to avoid trig dependencies.
	auto emit_bone = [&](spine::Bone *bone, const Color &col) {
		if (!bone || !bone->isActive()) return;
		float wx = bone->getAppliedPose().getWorldX();
		float wy = bone->getAppliedPose().getWorldY();
		float len = bone->getData().getLength();
		if (len == 0) len = debug_bones_thickness * 2.0f;
		// Bone local X axis in world space = (a, b) (column 0 of the 2x2 world matrix)
		float a = bone->getAppliedPose().getA();
		float b = bone->getAppliedPose().getB();
		// Normalise so length-0 bones still show a marker
		float mag = spine::MathUtil::sqrt(a * a + b * b);
		if (mag > 0.0f) { a /= mag; b /= mag; }
		float tip_x = wx + a * len;
		float tip_y = wy + b * len;
		emit_line(wx, wy, tip_x, tip_y, col);
	};

	// --- Root bone ---
	if (debug_root) {
		emit_bone(sk->getRootBone(), debug_root_color);
	}

	// --- Bones ---
	if (debug_bones) {
		auto &bones = sk->getBones();
		for (int i = 0; i < (int) bones.size(); i++) {
			emit_bone(bones[i], debug_bones_color);
		}
	}

	// --- Build the PRIMITIVE_LINES mesh surface ---
	if (dbg_positions.size() == 0) return;
	if (!mesh.is_valid()) return;

	// Add the debug lines as an additional PRIMITIVE_LINES surface on the primary mesh RID.
	// build_meshes() already called set_base(mesh), so this surface is rendered by the
	// same instance without needing a separate RS instance or scenario setup.
	// build_meshes() frees and recreates mesh each frame, so this surface is cleaned up
	// automatically — no separate debug mesh RID is needed.
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = dbg_positions;
	arrays[Mesh::ARRAY_COLOR]  = dbg_colors;

	RS::get_singleton()->mesh_add_surface_from_arrays(mesh, RS::PRIMITIVE_LINES, arrays, Array(), Dictionary(),
			RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE);

	// Lazily create the per-sprite debug lines material from the shared lines shader.
	// Each sprite has its own material so billboard_mode can be set independently.
	if (!debug_lines_material.is_valid()) {
		debug_lines_material.instantiate();
		debug_lines_material->set_shader(statics.get_lines_shader());
		debug_lines_material->set_render_priority(127); // draw on top among transparent surfaces
	}
	debug_lines_material->set_shader_parameter("billboard_mode", (int) billboard);

	int surface_count = RS::get_singleton()->mesh_get_surface_count(mesh);
	if (surface_count > 0) {
		RS::get_singleton()->mesh_surface_set_material(mesh, surface_count - 1, debug_lines_material->get_rid());
	}
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

void SpineSprite3D::set_pixel_size(float v) {
	pixel_size = v;
	if (skeleton.is_valid()) build_meshes();
}

float SpineSprite3D::get_pixel_size() {
	return pixel_size;
}

void SpineSprite3D::set_z_spacing(float v) {
	z_spacing = v;
	if (skeleton.is_valid()) build_meshes();
}

float SpineSprite3D::get_z_spacing() {
	return z_spacing;
}

void SpineSprite3D::set_flip_h(bool v) {
	flip_h = v;
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_flip_h() {
	return flip_h;
}

void SpineSprite3D::set_flip_v(bool v) {
	flip_v = v;
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_flip_v() {
	return flip_v;
}

void SpineSprite3D::set_billboard(BillboardMode v) {
	billboard = v;
	// Update billboard_mode on all already-cached per-instance material clones.
	for (auto &entry : material_cache) {
		entry.value->set_shader_parameter("billboard_mode", (int) billboard);
	}
	// Update debug lines material so overlay stays aligned with the rendered regions.
	if (debug_lines_material.is_valid()) {
		debug_lines_material->set_shader_parameter("billboard_mode", (int) billboard);
	}
	if (skeleton.is_valid()) build_meshes();
}

SpineSprite3D::BillboardMode SpineSprite3D::get_billboard() {
	return billboard;
}

void SpineSprite3D::set_shaded(bool v) {
	shaded = v;
	// Switching shaded mode invalidates the per-instance material cache since the
	// cache key encodes the shaded bit — clear so flush() picks the correct variants.
	material_cache.clear();
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_shaded() {
	return shaded;
}

void SpineSprite3D::set_normal_material(Ref<Material> v) {
	normal_material = v;
	if (skeleton.is_valid()) build_meshes();
}

Ref<Material> SpineSprite3D::get_normal_material() {
	return normal_material;
}

void SpineSprite3D::set_additive_material(Ref<Material> v) {
	additive_material = v;
	if (skeleton.is_valid()) build_meshes();
}

Ref<Material> SpineSprite3D::get_additive_material() {
	return additive_material;
}

void SpineSprite3D::set_multiply_material(Ref<Material> v) {
	multiply_material = v;
	if (skeleton.is_valid()) build_meshes();
}

Ref<Material> SpineSprite3D::get_multiply_material() {
	return multiply_material;
}

void SpineSprite3D::set_screen_material(Ref<Material> v) {
	screen_material = v;
	if (skeleton.is_valid()) build_meshes();
}

Ref<Material> SpineSprite3D::get_screen_material() {
	return screen_material;
}

void SpineSprite3D::clear_statics() {
	SpineSprite3DStatics::clear();
}

// ---------------------------------------------------------------------------
// Task 10: Editor animation preview — ported verbatim from SpineSprite
// ---------------------------------------------------------------------------

static void update_preview_animation_3d(SpineSprite3D *sprite, const String &skin, const String &animation, bool frame, float time) {
	if (!Engine::get_singleton()->is_editor_hint()) return;
	if (!sprite->get_skeleton().is_valid()) return;

	if (EMPTY(skin) || skin == "Default") {
		sprite->get_skeleton()->set_skin(nullptr);
	} else {
		sprite->get_skeleton()->set_skin_by_name(skin);
	}
	sprite->get_skeleton()->set_to_setup_pose();
	if (EMPTY(animation) || animation == "-- Empty --") {
		sprite->get_animation_state()->set_empty_animation(0, 0);
		return;
	}

	auto track_entry = sprite->get_animation_state()->set_animation(animation, true, 0);
	track_entry->set_mix_duration(0);
	if (frame) {
		track_entry->set_time_scale(0);
		track_entry->set_track_time(time);
	}
}

void SpineSprite3D::_get_property_list(List<PropertyInfo> *list) const {
	if (!skeleton_data_res.is_valid() || !skeleton_data_res->is_skeleton_data_loaded()) return;
#ifdef SPINE_GODOT_EXTENSION
	PackedStringArray animation_names;
	PackedStringArray skin_names;
#else
	Vector<String> animation_names;
	Vector<String> skin_names;
#endif
	skeleton_data_res->get_animation_names(animation_names);
	skeleton_data_res->get_skin_names(skin_names);
	animation_names.insert(0, "-- Empty --");

	PropertyInfo preview_skin_property;
	preview_skin_property.name = "preview_skin";
	preview_skin_property.type = Variant::STRING;
	preview_skin_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	preview_skin_property.hint_string = String(",").join(skin_names);
	preview_skin_property.hint = PROPERTY_HINT_ENUM;
	list->push_back(preview_skin_property);

	PropertyInfo preview_anim_property;
	preview_anim_property.name = "preview_animation";
	preview_anim_property.type = Variant::STRING;
	preview_anim_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	preview_anim_property.hint_string = String(",").join(animation_names);
	preview_anim_property.hint = PROPERTY_HINT_ENUM;
	list->push_back(preview_anim_property);

	PropertyInfo preview_frame_property;
	preview_frame_property.name = "preview_frame";
	preview_frame_property.type = Variant::BOOL;
	preview_frame_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	list->push_back(preview_frame_property);

	PropertyInfo preview_time_property;
	preview_time_property.name = "preview_time";
	preview_time_property.type = VARIANT_FLOAT;
	preview_time_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	float animation_duration = 0;
	if (!EMPTY(preview_animation) && preview_animation != "-- Empty --") {
		auto animation = skeleton_data_res->find_animation(preview_animation);
		if (animation.is_valid()) animation_duration = animation->get_duration();
	}
#ifdef SPINE_GODOT_EXTENSION
	preview_time_property.hint_string = String("0.0,") + String::num(animation_duration) + String(",0.01");
#else
	preview_time_property.hint_string = String("0.0,{0},0.01").format(varray(animation_duration));
#endif
	preview_time_property.hint = PROPERTY_HINT_RANGE;
	list->push_back(preview_time_property);
}

bool SpineSprite3D::_get(const StringName &property, Variant &value) const {
	if (property == StringName("preview_skin")) {
		value = preview_skin;
		return true;
	}

	if (property == StringName("preview_animation")) {
		value = preview_animation;
		return true;
	}

	if (property == StringName("preview_frame")) {
		value = preview_frame;
		return true;
	}

	if (property == StringName("preview_time")) {
		value = preview_time;
		return true;
	}
	return false;
}

bool SpineSprite3D::_set(const StringName &property, const Variant &value) {
	if (property == StringName("preview_skin")) {
		preview_skin = value;
		update_preview_animation_3d(this, preview_skin, preview_animation, preview_frame, preview_time);
		NOTIFY_PROPERTY_LIST_CHANGED();
		return true;
	}

	if (property == StringName("preview_animation")) {
		preview_animation = value;
		update_preview_animation_3d(this, preview_skin, preview_animation, preview_frame, preview_time);
		NOTIFY_PROPERTY_LIST_CHANGED();
		return true;
	}

	if (property == StringName("preview_frame")) {
		preview_frame = value;
		update_preview_animation_3d(this, preview_skin, preview_animation, preview_frame, preview_time);
		return true;
	}

	if (property == StringName("preview_time")) {
		preview_time = value;
		update_preview_animation_3d(this, preview_skin, preview_animation, preview_frame, preview_time);
		return true;
	}

	return false;
}

// Task 9 — Lifting helper: maps a Spine 2D bone (Y-down) into a Godot 3D local Transform3D (Y-up).
// The Spine affine matrix is [a c worldX; b d worldY] where a,b are the X-axis and c,d the Y-axis.
// To convert Y-down -> Y-up we negate the Y component of every world position and basis row.
// IMPORTANT: the exact sign convention in the basis columns may need visual tweaking (Task 12).
Transform3D SpineSprite3D::bone_to_transform3d(spine::Bone *bone, float slot_z) const {
	float a = bone->getAppliedPose().getA();
	float b = bone->getAppliedPose().getB();
	float c = bone->getAppliedPose().getC();
	float d = bone->getAppliedPose().getD();
	float wx = bone->getAppliedPose().getWorldX() * pixel_size;
	float wy = -bone->getAppliedPose().getWorldY() * pixel_size;
	Basis basis;
	basis.set_column(0, Vector3(a, -b, 0));   // X axis: negate y component for Y-up
	basis.set_column(1, Vector3(-c, d, 0));   // Y axis: negate x component for handedness
	basis.set_column(2, Vector3(0, 0, 1));    // Z normal (into screen)
	return Transform3D(basis, Vector3(wx, wy, slot_z));
}

Transform3D SpineSprite3D::get_global_bone_transform_3d(const String &bone_name) {
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return get_global_transform();
	auto bone_ref = skeleton->find_bone(bone_name);
	if (!bone_ref.is_valid()) return get_global_transform();
	spine::Bone *bone = bone_ref->get_spine_object();
	if (!bone) return get_global_transform();
	return get_global_transform() * bone_to_transform3d(bone, 0.0f);
}

void SpineSprite3D::set_global_bone_transform_3d(const String &bone_name, Transform3D xform) {
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;
	auto bone_ref = skeleton->find_bone(bone_name);
	if (!bone_ref.is_valid()) return;
	spine::Bone *bone = bone_ref->get_spine_object();
	if (!bone) return;

	// Convert from global 3D space to sprite-local bone space.
	Transform3D local = get_global_transform().affine_inverse() * xform;
	Vector3 origin = local.origin;
	// Extract basis columns back to Spine a,b,c,d (inverse of bone_to_transform3d).
	Vector3 col0 = local.basis.get_column(0); // (a, -b, ...)
	Vector3 col1 = local.basis.get_column(1); // (-c, d, ...)

	auto &pose = bone->getAppliedPose();
	pose.setA(col0.x);
	pose.setB(-col0.y);
	pose.setC(-col1.x);
	pose.setD(col1.y);
	pose.setWorldX(origin.x / pixel_size);
	pose.setWorldY(-origin.y / pixel_size);
	pose.updateLocalTransform(*skeleton->get_spine_object());
	bone->getPose().set(pose);

	modified_bones = true;
}
