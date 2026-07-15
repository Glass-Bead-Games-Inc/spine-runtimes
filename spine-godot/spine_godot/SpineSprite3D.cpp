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

// This 3D node is intentionally Godot-4.x-only (relies on GeometryInstance3D and
// scene/3d APIs absent in Godot 3.x). SpineCommon.h provides VERSION_MAJOR, so the
// guard below excludes all content from the Godot 3.x module build entirely.
//
// Fix #15: additionally skip all 3D content when a Godot 4.x module is built with
// disable_3d=yes (the engine defines _3D_DISABLED and GeometryInstance3D / 3D
// rendering APIs are unavailable). In the GDExtension build VERSION_MAJOR==4 and
// _3D_DISABLED is never defined, so the 3D code still compiles there, which is
// correct.
#if VERSION_MAJOR > 3 && !defined(_3D_DISABLED)

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
#include <godot_cpp/classes/world3d.hpp>      // get_world_3d()->get_scenario(): World3D is only forward-declared by node3d.hpp
#include <godot_cpp/classes/image.hpp>        // Fix #2: Texture2D::get_image() return type
#include <godot_cpp/classes/image_texture.hpp>// Fix #2: ImageTexture::create_from_image (GDExtension 3D-safe copy)
#include <godot_cpp/classes/camera3d.hpp>     // silhouette buffer: read the active camera's projection/transform
#include <godot_cpp/classes/viewport.hpp>     // silhouette buffer: get_viewport()->get_camera_3d()/get_visible_rect()
#include <godot_cpp/classes/editor_interface.hpp>// design-time billboard: fall back to the editor 3D camera
#include <godot_cpp/classes/sub_viewport.hpp>    // EditorInterface::get_editor_viewport_3d() return type
#include <godot_cpp/variant/variant.hpp>
#else
#include "scene/resources/shader.h"
#include "scene/resources/material.h"// declares ShaderMaterial (no separate shader_material.h in 4.x)
#include "scene/resources/mesh.h"
#include "scene/resources/image_texture.h"// Fix #2: ImageTexture / Texture2D (3D-safe copy path is extension-only, kept for parity)
#include "core/config/engine.h"           // Engine::get_singleton(); not transitively included in Godot 4.7
#include "scene/3d/camera_3d.h"           // silhouette buffer: active camera projection/transform
#include "scene/main/viewport.h"          // silhouette buffer: get_viewport()->get_camera_3d() (also declares SubViewport)
#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"      // design-time billboard: fall back to the editor 3D camera
#endif
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

public:
	// Map a TextureFilter value to the GLSL sampler hint token used in the shader source.
	// Shared by build_shader_source (below) and callers. Kept in the statics struct so both
	// the shader generator and any external caller can reach it.
	static const char *texture_filter_hint(SpineSprite3D::TextureFilter f) {
		switch (f) {
			case SpineSprite3D::TEXTURE_FILTER_NEAREST:
				return "filter_nearest";
			case SpineSprite3D::TEXTURE_FILTER_NEAREST_MIPMAP:
				return "filter_nearest_mipmap";
			case SpineSprite3D::TEXTURE_FILTER_LINEAR_MIPMAP:
				return "filter_linear_mipmap";
			case SpineSprite3D::TEXTURE_FILTER_LINEAR:
			default:
				return "filter_linear";
		}
	}

private:
	// Build GLSL source for the requested variant.
	// Generates all {Normal, Additive, Multiply} blend x {straight, pma} x {unshaded, shaded} variants.
	// The DISPLAY material writes depth (depth_draw_opaque) but does not cast (shadows_disabled). Writing
	// depth keeps the sprite in the camera depth buffer so screen-space depth effects (fog, water, SSAO,
	// DOF) and scene occlusion work correctly. With coplanar parts (z_spacing 0 / small) the opaque pixels
	// of overlapping parts share the same depth, so they layer by draw order without z-fighting. Shadow
	// casting is handled by a SEPARATE shadows-only RS instance (see build_meshes / build_shadow_shader_source);
	// the cast setting does NOT affect the display shader, so it is not a parameter here.
	static String build_shader_source(spine::BlendMode blend, bool shaded, bool pma, SpineSprite3D::TextureFilter filter,
									  SpineSprite3D::AlphaCutMode alpha_cut, bool no_depth_test, bool double_sided) {
		const char *filter_hint = texture_filter_hint(filter);
		// Blend mode -> render_mode token
		String rm;
		switch (blend) {
			case spine::BlendMode_Additive:
				rm = "blend_add";
				break;
			case spine::BlendMode_Multiply:
				rm = "blend_mul";
				break;
			// F9: Spine Screen blend is intentionally unsupported in 3D; it maps to
			// blend_mix and therefore renders as Normal. A custom screen_material can
			// be supplied for correct Screen rendering. This is by design, not a bug.
			default:
				rm = "blend_mix";
				break;// Normal (Screen unsupported -> treat as normal)
		}

		// Assemble the depth-write render_mode token per alpha_cut mode. Sprite3D parity:
		//  - DISABLED:      depth_draw_opaque   (alpha blend, no opaque depth from the alpha pixels)
		//  - DISCARD:       depth_draw_opaque   + scissor discard (kept pixels write opaque depth -> hard cut)
		//  - OPAQUE_PREPASS:depth_prepass_alpha (alpha pre-pass writes opaque depth -> soft edges cut fog/water)
		//  - HASH:          depth_draw_opaque   + ALPHA_HASH_SCALE (hashed transparency in the opaque pass,
		//                   cuts fog/water like Discard but with a stochastic dither instead of a hard edge)
		const char *depth_mode = (alpha_cut == SpineSprite3D::ALPHA_CUT_OPAQUE_PREPASS) ? "depth_prepass_alpha" : "depth_draw_opaque";
		const char *cull_mode = double_sided ? "cull_disabled" : "cull_back";

		// HASH mode: writing the engine built-in ALPHA_HASH_SCALE sets the shader's uses_alpha_clip flag
		// (like ALPHA_SCISSOR_THRESHOLD does for DISCARD), which makes the engine render the material in the
		// OPAQUE pass and populate the opaque depth buffer (so it cuts fog/water/depth effects), but with
		// hashed/dithered transparency rather than a hard scissor edge. This differs from OPAQUE_PREPASS
		// (soft-edged alpha pre-pass) and from DISCARD (hard cut at the scissor threshold).
		const bool alpha_hash = (alpha_cut == SpineSprite3D::ALPHA_CUT_HASH);

		// shadows_disabled coexists with depth_prepass_alpha: the pre-pass is a camera-depth
		// pass, shadows_disabled only suppresses the light/shadow pass. The separate shadow
		// caster still owns shadow casting. Verified to compile with both in one render_mode.
		String extra_rm;
		if (no_depth_test) extra_rm += ", depth_test_disabled";

		const bool scissor = (alpha_cut == SpineSprite3D::ALPHA_CUT_DISCARD);

		// Vertex shader (same for shaded and unshaded). fixed_size is applied AFTER the
		// billboard block via the standard Godot snippet, gated on the fixed_size_enabled uniform.
		String vertex_fn = "void vertex() {\n"
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
						   "    if (fixed_size_enabled) {\n"
						   "        if (PROJECTION_MATRIX[3][3] != 0.0) {\n"
						   "            float h = abs(1.0 / (2.0 * PROJECTION_MATRIX[1][1]));\n"
						   "            float sc = (h * 2.0);\n"
						   "            MODELVIEW_MATRIX[0] *= sc;\n"
						   "            MODELVIEW_MATRIX[1] *= sc;\n"
						   "            MODELVIEW_MATRIX[2] *= sc;\n"
						   "        } else {\n"
						   "            float sc = -(MODELVIEW_MATRIX)[3].z;\n"
						   "            MODELVIEW_MATRIX[0] *= sc;\n"
						   "            MODELVIEW_MATRIX[1] *= sc;\n"
						   "            MODELVIEW_MATRIX[2] *= sc;\n"
						   "        }\n"
						   "    }\n"
						   // The mesh bakes the RAW draw-order index into VERTEX.z as -(float)i (independent of
						   // z_spacing). Capture it BEFORE applying z_spacing so depth_offset works at ANY z_spacing
						   // (including the default 0). Then convert the raw index into the world-space layer offset by
						   // scaling VERTEX.z by layer_z_spacing — this is what places coplanar parts along local +Z when
						   // z_spacing > 0, and collapses them onto z=0 when z_spacing == 0.
						   "    float _draw_index = VERTEX.z; // signed draw-order index -i (z stores -i raw)\n"
						   "    VERTEX.z *= layer_z_spacing;  // apply world-space layer spacing (no-op at z_spacing 0)\n"
						   // Per-part view-independent depth offset, applied in VIEW space (linear meters).
						   // We push each part toward the camera by (draw_order * depth_offset) METERS along the view
						   // axis BEFORE projection. Two benefits: (1) along the view axis it never collapses at grazing
						   // angles the way world-space z_spacing does, and (2) being in linear meters it is NOT amplified
						   // by the near plane the way a clip-space/NDC offset is (a small near stretches NDC enormously,
						   // turning a tiny NDC step into meters of real depth). depth_offset is meters-per-draw-order.
						   // Only overrides POSITION when enabled, so default behavior is unchanged.
						   // NOTE: writing POSITION here overrides the engine's projection for this path. Under TAA or
						   // XR multiview this forgoes the engine's per-frame jitter / per-eye projection matrix (known
						   // caveat). It is kept because it is the validated working approach for the depth offset.
						   "    if (depth_offset != 0.0) {\n"
						   "        vec4 _vpos = MODELVIEW_MATRIX * vec4(VERTEX, 1.0);\n"
						   "        _vpos.z -= _draw_index * depth_offset;\n"
						   "        POSITION = PROJECTION_MATRIX * _vpos;\n"
						   "    }\n"
						   "}\n";

		// Shared uniforms block (present on both shaded and unshaded variants).
		String common_uniforms = String("uniform int billboard_mode = 0; // 0 disabled, 1 enabled, 2 y\n") +
			"uniform bool fixed_size_enabled = false;\n"
			"uniform vec4 modulate_color = vec4(1.0);\n"
			"uniform float layer_z_spacing = 0.0;\n"
			"uniform float depth_offset = 0.0;\n";
		if (scissor) common_uniforms += "uniform float alpha_scissor_threshold = 0.5;\n";

		// Fragment alpha/albedo: compute alpha into `a`, apply scissor if needed, then modulate.
		String albedo_alpha = pma ? "vec4 tex = texture(albedo_tex, UV); vec3 c = tex.rgb * COLOR.rgb; ALBEDO = c;"
								  : "vec4 tex = texture(albedo_tex, UV); ALBEDO = tex.rgb * COLOR.rgb;";
		// Modulate. For the STRAIGHT (non-pma) path ALBEDO is plain color, so RGB scales by modulate.rgb
		// and ALPHA by modulate.a independently. For the PMA path ALBEDO is already premultiplied
		// (ALBEDO == color * ALPHA), so to keep that invariant after fading via modulate.a we MUST also
		// scale ALBEDO by modulate_color.a (ALBEDO *= modulate.rgb * modulate.a), matching ALPHA *= modulate.a.
		// Omitting the .a factor on ALBEDO would leave it brighter than its alpha and produce dark/edge
		// fringing as a PMA sprite fades. Spine atlases are PMA by default, so this is the common path.
		String alpha_calc = "    float a = tex.a * COLOR.a;\n"
							"    ALPHA = a;\n";
		alpha_calc += pma ? "    ALBEDO *= modulate_color.rgb * modulate_color.a;\n" : "    ALBEDO *= modulate_color.rgb;\n";
		alpha_calc += "    ALPHA *= modulate_color.a;\n";
		// DISCARD mode: assign the engine BUILT-IN ALPHA_SCISSOR_THRESHOLD instead of doing a manual
		// `if (a < t) discard;`. Only writing this built-in sets the shader's uses_alpha_clip flag,
		// which makes the engine render the material in the OPAQUE pass and populate the opaque depth
		// buffer that fog/water/depth effects sample -> the sprite gets cut at the water line. A
		// hand-rolled discard leaves uses_alpha=true / uses_alpha_clip=false -> has_base_alpha ->
		// alpha-blend (transparent) pass -> no opaque depth -> no cut. The engine performs the discard
		// itself from this threshold (matches BaseMaterial3D, material.cpp:1813).
		if (scissor) alpha_calc += "    ALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold;\n";
		// HASH: assign the engine built-in ALPHA_HASH_SCALE to enable hashed (dithered) alpha clipping in
		// the opaque pass. Writing it is what flips uses_alpha_clip, so the material lands opaque/hashed.
		if (alpha_hash) alpha_calc += "    ALPHA_HASH_SCALE = 1.0;\n";

		if (!shaded) {
			// Unshaded variant: flat rendering, no lighting, no shadow participation.
			return String("shader_type spatial;\n") + "render_mode " + rm + ", " + cull_mode + ", unshaded, " + depth_mode + ", shadows_disabled" +
				extra_rm +
				";\n"
				"\n"
				"uniform sampler2D albedo_tex : source_color, " +
				filter_hint + ";\n" + common_uniforms + "\n" + vertex_fn +
				"\n"
				"void fragment() {\n"
				"    " +
				albedo_alpha + "\n" + alpha_calc + "}\n";
		} else {
			// Shaded variant: participates in lighting and shadows.
			// PMA handling same as unshaded; normal/specular maps are optional.
			return String("shader_type spatial;\n") + "render_mode " + rm + ", " + cull_mode + ", " + depth_mode + ", shadows_disabled" + extra_rm +
				";\n"
				"\n"
				"uniform sampler2D albedo_tex : source_color, " +
				filter_hint +
				";\n"
				"uniform sampler2D normal_tex : hint_normal, " +
				filter_hint +
				";\n"
				"uniform sampler2D specular_tex : source_color, " +
				filter_hint +
				";\n"
				"uniform bool use_normal_tex = false;\n"
				"uniform bool use_specular_tex = false;\n" +
				common_uniforms + "\n" + vertex_fn +
				"\n"
				"void fragment() {\n"
				"    " +
				albedo_alpha + "\n" + alpha_calc +
				"    if (use_normal_tex) NORMAL_MAP = texture(normal_tex, UV).rgb;\n"
				"    if (use_specular_tex) SPECULAR = texture(specular_tex, UV).r;\n"
				"}\n";
		}
	}

	static Ref<ShaderMaterial> make_material(spine::BlendMode blend, bool shaded, bool pma, SpineSprite3D::TextureFilter filter,
											 SpineSprite3D::AlphaCutMode alpha_cut, bool no_depth_test, bool double_sided) {
		Ref<Shader> shader;
		shader.instantiate();
		shader->set_code(build_shader_source(blend, shaded, pma, filter, alpha_cut, no_depth_test, double_sided));

		Ref<ShaderMaterial> mat;
		mat.instantiate();
		mat->set_shader(shader);
		return mat;
	}

	// Build GLSL source for the SHADOW caster material. This material is only ever assigned as a
	// surface override on the separate SHADOWS_ONLY RS instance, so it never contributes to the
	// color pass. It is a minimal alpha-cutout: depth_prepass_alpha turns the per-pixel ALPHA into
	// a shaped (silhouette) shadow. ALBEDO is irrelevant for a shadows-only instance.
	// The shadow shader depends ONLY on the texture filter: the cutout alpha is the texture alpha
	// times vertex/modulate alpha, which is identical for straight and premultiplied atlases, so
	// `pma` is NOT a parameter (keying on pma would just duplicate identical shaders).
	//
	// The caster MUST share the SAME vertex() transform as the display shader: billboard_mode (1/2),
	// fixed_size, the raw-draw-index z_spacing scaling, and the per-part depth_offset view-space push.
	// Otherwise a billboarded sprite would cast a fixed-orientation silhouette that collapses edge-on
	// (the rendered card faces the camera but its shadow geometry would not). The vertex code below is
	// duplicated verbatim from build_shader_source's vertex_fn so the shadow tracks the rendered card.
	//
	// The fragment alpha MUST also match the display cutout: multiply by modulate_color.a (so a sprite
	// faded via modulate still fades its shadow) and assign ALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold
	// (so a re-cut via a lowered scissor matches the rendered silhouette). depth_prepass_alpha + the
	// scissor threshold give a hard cutout identical to the display's DISCARD path.
	static String build_shadow_shader_source(SpineSprite3D::TextureFilter filter) {
		const char *filter_hint = texture_filter_hint(filter);
		return String("shader_type spatial;\n"
					  "render_mode cull_disabled, unshaded, depth_prepass_alpha;\n"
					  "\n"
					  "uniform sampler2D albedo_tex : source_color, ") +
			filter_hint +
			";\n"
			"uniform int billboard_mode = 0; // 0 disabled, 1 enabled, 2 y\n"
			"uniform bool fixed_size_enabled = false;\n"
			"uniform vec4 modulate_color = vec4(1.0);\n"
			"uniform float layer_z_spacing = 0.0;\n"
			"uniform float depth_offset = 0.0;\n"
			"uniform float alpha_scissor_threshold = 0.5;\n"
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
			"    if (fixed_size_enabled) {\n"
			"        if (PROJECTION_MATRIX[3][3] != 0.0) {\n"
			"            float h = abs(1.0 / (2.0 * PROJECTION_MATRIX[1][1]));\n"
			"            float sc = (h * 2.0);\n"
			"            MODELVIEW_MATRIX[0] *= sc;\n"
			"            MODELVIEW_MATRIX[1] *= sc;\n"
			"            MODELVIEW_MATRIX[2] *= sc;\n"
			"        } else {\n"
			"            float sc = -(MODELVIEW_MATRIX)[3].z;\n"
			"            MODELVIEW_MATRIX[0] *= sc;\n"
			"            MODELVIEW_MATRIX[1] *= sc;\n"
			"            MODELVIEW_MATRIX[2] *= sc;\n"
			"        }\n"
			"    }\n"
			"    float _draw_index = VERTEX.z; // signed draw-order index -i (z stores -i raw)\n"
			"    VERTEX.z *= layer_z_spacing;  // apply world-space layer spacing (no-op at z_spacing 0)\n"
			"    if (depth_offset != 0.0) {\n"
			"        vec4 _vpos = MODELVIEW_MATRIX * vec4(VERTEX, 1.0);\n"
			"        _vpos.z -= _draw_index * depth_offset;\n"
			"        POSITION = PROJECTION_MATRIX * _vpos;\n"
			"    }\n"
			"}\n"
			"\n"
			"void fragment() {\n"
			"    ALPHA = texture(albedo_tex, UV).a * COLOR.a * modulate_color.a;\n"
			"    ALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold;\n"
			"}\n";
	}

	static Ref<ShaderMaterial> make_shadow_material(SpineSprite3D::TextureFilter filter) {
		Ref<Shader> shader;
		shader.instantiate();
		shader->set_code(build_shadow_shader_source(filter));

		Ref<ShaderMaterial> mat;
		mat.instantiate();
		mat->set_shader(shader);
		return mat;
	}

	// Build shader source for the debug lines overlay.
	// Unshaded, vertex_color_use_as_albedo, no depth test so lines always show through.
	// Includes the same billboard vertex() transform as the main render shader so that
	// on billboarded sprites the debug lines track the rendered geometry.
	// shadows_disabled: the debug PRIMITIVE_LINES surface lives on the same mesh that is bound to the
	// SHADOWS_ONLY shadow instance, and that surface has no shadow override material, so without this
	// the debug lines would themselves cast (garbage) shadows. Disabling shadows on the lines material
	// makes it never cast even when rendered through the shadow instance.
	static String build_lines_shader_source() {
		// blend_mix makes this a TRANSPARENT surface so it draws in the transparent pass AFTER all the
		// opaque character parts — i.e. always on top, like the 2D debug overlay and the SpineBoneNode3D
		// debug kite. Without it the overlay is opaque and (despite depth_test_disabled) gets overdrawn by
		// the character's later opaque surfaces, so the bones only show where they poke past the silhouette.
		return String("shader_type spatial;\n"
					  "render_mode unshaded, cull_disabled, depth_test_disabled, shadows_disabled, blend_mix;\n"
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
	// Display material variant cache. `casts` no longer varies the display shader (display is
	// always shadows_disabled; the separate caster owns shadows), so it is dropped from the key.
	// Shader-affecting dimensions, packed low->high:
	//   pma(1 bit) | shaded(1) | blend(2) | filter(2) | alpha_cut(2) | no_depth_test(1) | double_sided(1)
	// Total distinct variants = 2*2*4*4*4*2*2 = 2048. A HashMap<uint32_t, ...> holds them sparsely.
	static uint32_t variant_key(spine::BlendMode blend, bool shaded, bool pma, SpineSprite3D::TextureFilter filter,
								SpineSprite3D::AlphaCutMode alpha_cut, bool no_depth_test, bool double_sided) {
		uint32_t blend2 = (blend == spine::BlendMode_Additive) ? 1u : (blend == spine::BlendMode_Multiply) ? 2u : 0u;// Normal/Screen -> 0
		uint32_t key = (pma ? 1u : 0u);
		key |= (shaded ? 1u : 0u) << 1;
		key |= (blend2 & 0x3u) << 2;
		key |= ((uint32_t) filter & 0x3u) << 4;
		key |= ((uint32_t) alpha_cut & 0x3u) << 6;
		key |= (no_depth_test ? 1u : 0u) << 8;
		key |= (double_sided ? 1u : 0u) << 9;
		return key;
	}
	HashMap<uint32_t, Ref<ShaderMaterial>> materials;
	// Shadow caster BASE materials (shader only, no texture). Keyed on filter ONLY (the shadow shader is
	// independent of pma, blend, shaded, etc.), so there is exactly one per TextureFilter value (max 4).
	// These are cloned per-texture into SpineSprite3D::shadow_material_cache, exactly like the display
	// base materials above are cloned into material_cache.
	Ref<ShaderMaterial> shadow_materials[4];
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

	Ref<ShaderMaterial> get_material(spine::BlendMode blend, bool shaded, bool pma, SpineSprite3D::TextureFilter filter,
									 SpineSprite3D::AlphaCutMode alpha_cut, bool no_depth_test, bool double_sided) {
		uint32_t key = variant_key(blend, shaded, pma, filter, alpha_cut, no_depth_test, double_sided);
		if (!materials.has(key) || !materials[key].is_valid()) {
			materials[key] = make_material(blend, shaded, pma, filter, alpha_cut, no_depth_test, double_sided);
		}
		return materials[key];
	}

	// Cached BASE shadow material for `filter`. Cloned per-texture by the caller.
	Ref<ShaderMaterial> get_shadow_material(SpineSprite3D::TextureFilter filter) {
		int key = (int) filter & 0x3;// 0..3
		if (!shadow_materials[key].is_valid()) {
			shadow_materials[key] = make_shadow_material(filter);
		}
		return shadow_materials[key];
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

// ---------------------------------------------------------------------------
// Optional silhouette mask (opt-in per node). SpineSilhouetteBuffer is a shared offscreen ID/coverage
// buffer: ONE RS viewport + scenario + camera, created lazily on the first enabled node and torn down
// when the last disables (ref-counted). Each enabled node registers a mask RS instance (its display
// mesh + per-surface ID-mask materials) into this scenario. Each frame the camera mirrors the active
// Camera3D and the viewport is sized to the main viewport (halved if requested), so the buffer aligns
// with SCREEN_UV. viewport_get_texture() is set as `spine_coverage` on each enabled node's display
// material. Off by default -> this object never exists and nothing extra renders.
// ---------------------------------------------------------------------------
static void spine_free_rid(const RID &rid) {
	if (!rid.is_valid()) return;
#ifdef SPINE_GODOT_EXTENSION
	RS::get_singleton()->free_rid(rid);
#else
	RS::get_singleton()->free(rid);
#endif
}

// Mask shader for a filter: standard vertex (billboard / fixed_size / z_spacing collapse / depth_offset)
// so the silhouette matches the rendered card; fragment writes the per-material encoded id into ALBEDO
// where the atlas alpha passes the opaque cutout, so the frontmost character's id wins on overlap.
static String spine_silhouette_mask_shader_source(SpineSprite3D::TextureFilter filter) {
	const char *filter_hint = SpineSprite3DStatics::texture_filter_hint(filter);
	return String("shader_type spatial;\n"
				  "render_mode unshaded, cull_disabled, depth_draw_opaque, shadows_disabled;\n"
				  "uniform sampler2D albedo_tex : source_color, ") +
		filter_hint +
		";\n"
		"uniform float spine_object_id = 0.0;\n"
		"uniform int billboard_mode = 0;\n"
		"uniform bool fixed_size_enabled = false;\n"
		"uniform vec4 modulate_color = vec4(1.0);\n"
		"uniform float layer_z_spacing = 0.0;\n"
		"uniform float depth_offset = 0.0;\n"
		"uniform float alpha_scissor_threshold = 0.5;\n"
		"void vertex() {\n"
		"    if (billboard_mode == 1) {\n"
		"        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);\n"
		"        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);\n"
		"    } else if (billboard_mode == 2) {\n"
		"        MODELVIEW_MATRIX = VIEW_MATRIX * mat4(vec4(normalize(cross(vec3(0.0,1.0,0.0), INV_VIEW_MATRIX[2].xyz)), 0.0), "
		"vec4(0.0,1.0,0.0,0.0), vec4(normalize(cross(INV_VIEW_MATRIX[0].xyz, vec3(0.0,1.0,0.0))), 0.0), MODEL_MATRIX[3]);\n"
		"        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);\n"
		"    }\n"
		"    if (fixed_size_enabled) {\n"
		"        if (PROJECTION_MATRIX[3][3] != 0.0) { float sc = abs(1.0 / (2.0 * PROJECTION_MATRIX[1][1])) * 2.0; MODELVIEW_MATRIX[0] *= sc; "
		"MODELVIEW_MATRIX[1] *= sc; MODELVIEW_MATRIX[2] *= sc; }\n"
		"        else { float sc = -(MODELVIEW_MATRIX)[3].z; MODELVIEW_MATRIX[0] *= sc; MODELVIEW_MATRIX[1] *= sc; MODELVIEW_MATRIX[2] *= sc; }\n"
		"    }\n"
		"    float _draw_index = VERTEX.z;\n"
		"    VERTEX.z *= layer_z_spacing;\n"
		"    if (depth_offset != 0.0) { vec4 _vpos = MODELVIEW_MATRIX * vec4(VERTEX, 1.0); _vpos.z -= _draw_index * depth_offset; POSITION = "
		"PROJECTION_MATRIX * _vpos; }\n"
		"}\n"
		"void fragment() {\n"
		"    float a = texture(albedo_tex, UV).a * COLOR.a * modulate_color.a;\n"
		"    ALBEDO = vec3(spine_object_id);\n"
		"    ALPHA = a;\n"
		"    ALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold;\n"
		"}\n";
}

struct SpineSilhouetteBuffer {
private:
	static SpineSilhouetteBuffer *_instance;
	RID viewport;
	RID scenario;
	RID camera;
	int ref_count = 0;
	int next_id = 1;// 1..255; 0 = empty (background)
	Size2i size = Size2i(0, 0);
	uint64_t last_sync_frame = 0;
	Ref<Shader> mask_shaders[4];
	Ref<ShaderMaterial> discard_material;

public:
	static SpineSilhouetteBuffer &instance() {
		if (!_instance) _instance = new SpineSilhouetteBuffer();
		return *_instance;
	}

	RID get_scenario() const {
		return scenario;
	}
	RID get_texture() const {
		return viewport.is_valid() ? RS::get_singleton()->viewport_get_texture(viewport) : RID();
	}

	Ref<Shader> get_mask_shader(SpineSprite3D::TextureFilter filter) {
		int k = (int) filter & 0x3;
		if (!mask_shaders[k].is_valid()) {
			mask_shaders[k].instantiate();
			mask_shaders[k]->set_code(spine_silhouette_mask_shader_source(filter));
		}
		return mask_shaders[k];
	}

	Ref<ShaderMaterial> get_discard_material() {
		if (!discard_material.is_valid()) {
			Ref<Shader> shader;
			shader.instantiate();
			shader->set_code("shader_type spatial;\n"
							 "render_mode unshaded, cull_disabled, shadows_disabled;\n"
							 "void fragment() { ALPHA = 0.0; ALPHA_SCISSOR_THRESHOLD = 1.0; }\n");
			discard_material.instantiate();
			discard_material->set_shader(shader);
		}
		return discard_material;
	}

	int acquire_id() {
		int id = next_id++;
		if (next_id > 255) next_id = 1;
		return id;
	}

	// First ref creates the RS viewport/scenario/camera; last ref frees them.
	void add_ref() {
		ref_count++;
		if (ref_count == 1) {
			RS *rs = RS::get_singleton();
			scenario = rs->scenario_create();
			camera = rs->camera_create();
			viewport = rs->viewport_create();
			rs->viewport_set_scenario(viewport, scenario);
			rs->viewport_attach_camera(viewport, camera);
			rs->viewport_set_transparent_background(viewport, true);
			rs->viewport_set_update_mode(viewport, SPINE_RS_ENUM::VIEWPORT_UPDATE_ALWAYS);
			rs->viewport_set_active(viewport, true);
			size = Size2i(0, 0);
		}
	}
	void release_ref() {
		if (ref_count <= 0) return;
		ref_count--;
		if (ref_count == 0) {
			spine_free_rid(viewport);
			spine_free_rid(camera);
			spine_free_rid(scenario);
			viewport = camera = scenario = RID();
			size = Size2i(0, 0);
		}
	}

	// Per frame (guarded): size the viewport to the main viewport (halved if requested) and mirror the
	// active Camera3D's transform + projection so the coverage aligns with SCREEN_UV.
	void sync(Camera3D *active_camera, const Size2i &main_size, bool half_res, uint64_t frame) {
		if (!viewport.is_valid() || last_sync_frame == frame) return;
		last_sync_frame = frame;
		RS *rs = RS::get_singleton();
		Size2i target = main_size;
		if (half_res) target = Size2i(MAX(1, main_size.x / 2), MAX(1, main_size.y / 2));
		if (target != size && target.x > 0 && target.y > 0) {
			rs->viewport_set_size(viewport, target.x, target.y);
			size = target;
		}
		if (active_camera && size.x > 0) {
			rs->camera_set_transform(camera, active_camera->get_global_transform());
			if (active_camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
				rs->camera_set_orthogonal(camera, active_camera->get_size(), active_camera->get_near(), active_camera->get_far());
			} else {
				rs->camera_set_perspective(camera, active_camera->get_fov(), active_camera->get_near(), active_camera->get_far());
			}
		}
	}

	static void shutdown() {
		if (_instance) {
			delete _instance;
			_instance = nullptr;
		}
	}
};

SpineSilhouetteBuffer *SpineSilhouetteBuffer::_instance = nullptr;

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
	ClassDB::bind_method(D_METHOD("set_texture_filter", "v"), &SpineSprite3D::set_texture_filter);
	ClassDB::bind_method(D_METHOD("get_texture_filter"), &SpineSprite3D::get_texture_filter);
	BIND_ENUM_CONSTANT(TEXTURE_FILTER_NEAREST);
	BIND_ENUM_CONSTANT(TEXTURE_FILTER_LINEAR);
	BIND_ENUM_CONSTANT(TEXTURE_FILTER_NEAREST_MIPMAP);
	BIND_ENUM_CONSTANT(TEXTURE_FILTER_LINEAR_MIPMAP);
	ClassDB::bind_method(D_METHOD("set_shaded", "v"), &SpineSprite3D::set_shaded);
	ClassDB::bind_method(D_METHOD("get_shaded"), &SpineSprite3D::get_shaded);

	// Sprite3D-style rendering controls
	ClassDB::bind_method(D_METHOD("set_alpha_cut", "v"), &SpineSprite3D::set_alpha_cut);
	ClassDB::bind_method(D_METHOD("get_alpha_cut"), &SpineSprite3D::get_alpha_cut);
	BIND_ENUM_CONSTANT(ALPHA_CUT_DISABLED);
	BIND_ENUM_CONSTANT(ALPHA_CUT_DISCARD);
	BIND_ENUM_CONSTANT(ALPHA_CUT_OPAQUE_PREPASS);
	BIND_ENUM_CONSTANT(ALPHA_CUT_HASH);
	ClassDB::bind_method(D_METHOD("set_alpha_scissor_threshold", "v"), &SpineSprite3D::set_alpha_scissor_threshold);
	ClassDB::bind_method(D_METHOD("get_alpha_scissor_threshold"), &SpineSprite3D::get_alpha_scissor_threshold);
	ClassDB::bind_method(D_METHOD("set_depth_offset", "v"), &SpineSprite3D::set_depth_offset);
	ClassDB::bind_method(D_METHOD("get_depth_offset"), &SpineSprite3D::get_depth_offset);
	ClassDB::bind_method(D_METHOD("set_no_depth_test", "v"), &SpineSprite3D::set_no_depth_test);
	ClassDB::bind_method(D_METHOD("get_no_depth_test"), &SpineSprite3D::get_no_depth_test);
	ClassDB::bind_method(D_METHOD("set_double_sided", "v"), &SpineSprite3D::set_double_sided);
	ClassDB::bind_method(D_METHOD("get_double_sided"), &SpineSprite3D::get_double_sided);
	ClassDB::bind_method(D_METHOD("set_fixed_size", "v"), &SpineSprite3D::set_fixed_size);
	ClassDB::bind_method(D_METHOD("get_fixed_size"), &SpineSprite3D::get_fixed_size);
	ClassDB::bind_method(D_METHOD("set_render_priority", "v"), &SpineSprite3D::set_render_priority);
	ClassDB::bind_method(D_METHOD("get_render_priority"), &SpineSprite3D::get_render_priority);
	ClassDB::bind_method(D_METHOD("set_modulate", "v"), &SpineSprite3D::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &SpineSprite3D::get_modulate);
	ClassDB::bind_method(D_METHOD("set_silhouette_mask_enabled", "v"), &SpineSprite3D::set_silhouette_mask_enabled);
	ClassDB::bind_method(D_METHOD("get_silhouette_mask_enabled"), &SpineSprite3D::get_silhouette_mask_enabled);
	ClassDB::bind_method(D_METHOD("set_silhouette_mask_half_res", "v"), &SpineSprite3D::set_silhouette_mask_half_res);
	ClassDB::bind_method(D_METHOD("get_silhouette_mask_half_res"), &SpineSprite3D::get_silhouette_mask_half_res);

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
	ClassDB::bind_method(D_METHOD("pose_at", "animation_name", "time"), &SpineSprite3D::pose_at);

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
	ADD_SIGNAL(
		MethodInfo("before_animation_state_update", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("before_animation_state_apply", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(
		MethodInfo("before_world_transforms_change", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("world_transforms_changed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite3D")));
	ADD_SIGNAL(MethodInfo("_internal_spine_objects_invalidated"));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skeleton_data_res", PropertyHint::PROPERTY_HINT_RESOURCE_TYPE, "SpineSkeletonDataResource"),
				 "set_skeleton_data_res", "get_skeleton_data_res");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "time_scale"), "set_time_scale", "get_time_scale");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "pixel_size", PROPERTY_HINT_RANGE, "0.0001,1,0.0001"), "set_pixel_size", "get_pixel_size");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "z_spacing", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_z_spacing", "get_z_spacing");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "depth_offset", PROPERTY_HINT_RANGE, "-0.1,0.1,0.0001"), "set_depth_offset", "get_depth_offset");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_h"), "set_flip_h", "get_flip_h");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_v"), "set_flip_v", "get_flip_v");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "billboard", PROPERTY_HINT_ENUM, "Disabled,Enabled,Y-Billboard"), "set_billboard", "get_billboard");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_filter", PROPERTY_HINT_ENUM, "Nearest,Linear,Nearest Mipmap,Linear Mipmap"),
				 "set_texture_filter", "get_texture_filter");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shaded"), "set_shaded", "get_shaded");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "alpha_cut", PROPERTY_HINT_ENUM, "Disabled,Discard,Opaque Pre-Pass,Alpha Hash"), "set_alpha_cut",
				 "get_alpha_cut");
	ADD_PROPERTY(PropertyInfo(VARIANT_FLOAT, "alpha_scissor_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_alpha_scissor_threshold",
				 "get_alpha_scissor_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "no_depth_test"), "set_no_depth_test", "get_no_depth_test");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "double_sided"), "set_double_sided", "get_double_sided");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fixed_size"), "set_fixed_size", "get_fixed_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "render_priority", PROPERTY_HINT_RANGE, "-128,127,1"), "set_render_priority", "get_render_priority");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "silhouette_mask_enabled"), "set_silhouette_mask_enabled", "get_silhouette_mask_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "silhouette_mask_half_res"), "set_silhouette_mask_half_res", "get_silhouette_mask_half_res");
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
	  pixel_size(0.01f), z_spacing(0.0f), flip_h(false), flip_v(false), billboard(BILLBOARD_DISABLED), texture_filter(TEXTURE_FILTER_LINEAR),
	  shaded(false), alpha_cut(ALPHA_CUT_DISCARD), alpha_scissor_threshold(0.5f), depth_offset(0.0f), no_depth_test(false), double_sided(true),
	  fixed_size(false), render_priority(0), modulate(Color(1, 1, 1, 1)), silhouette_mask_enabled(false), silhouette_mask_half_res(true),
	  silhouette_id(0), preview_skin("Default"), preview_animation("-- Empty --"), preview_frame(false), preview_time(0),
	  // Task 11: debug overlay defaults (same as SpineSprite 2D)
	  debug_root(false), debug_root_color(Color(1, 1, 1, 0.5f)), debug_bones(false), debug_bones_color(Color(1, 1, 0, 0.5f)),
	  debug_bones_thickness(5.0f), debug_regions(false), debug_regions_color(Color(0, 0, 1, 0.5f)), debug_meshes(false),
	  debug_meshes_color(Color(0, 0, 1, 0.5f)), debug_bounding_boxes(false), debug_bounding_boxes_color(Color(0, 1, 0, 0.5f)), debug_paths(false),
	  debug_paths_color(Color::hex(0xff7f0077)), debug_clipping(false), debug_clipping_color(Color(0.8f, 0, 0, 0.8f)),
	  debug_active_last_frame(false) {
	scratch_world_verts.ensureCapacity(1200);
	// Shadow casting: GeometryInstance3D defaults cast_shadow to ON, which would spin up the
	// separate shadows-only caster instance on every SpineSprite3D. Default it OFF so a fresh node
	// casts nothing until the user opts in via the inherited cast_shadow setting; build_meshes()
	// reads get_cast_shadows_setting() to decide whether to build the per-surface shadow overrides.
	// NOTE: this does NOT make rendering "byte-identical" to a plain alpha-blend node — the DISPLAY
	// default alpha_cut is Discard (ALPHA_CUT_DISCARD), an alpha-scissor that writes opaque depth in
	// the opaque pass and so cuts fog/water/depth effects at hard edges. Set alpha_cut = Disabled to
	// restore plain alpha-blend (no opaque depth write). Cast state is independent of this.
	set_cast_shadows_setting(SHADOW_CASTING_SETTING_OFF);
	// Receive NOTIFICATION_TRANSFORM_CHANGED so the separate shadows-only RS instance can be kept
	// in sync with the node's global transform (it is not parented to the node's own instance).
	set_notify_transform(true);
}

SpineSprite3D::~SpineSprite3D() {
	delete skeleton_clipper;
	// F1: clear the instance base before freeing the mesh RID so the
	// GeometryInstance3D never references a freed RID.
	set_base(RID());
	// Free the separate shadows-only instance before the mesh it references.
	if (shadow_instance.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->free_rid(shadow_instance);
#else
		RS::get_singleton()->free(shadow_instance);
#endif
		shadow_instance = RID();
	}
	// Free the silhouette mask instance and release this node's ref on the shared buffer.
	if (silhouette_instance.is_valid()) {
		spine_free_rid(silhouette_instance);
		silhouette_instance = RID();
	}
	if (silhouette_registered) {
		SpineSilhouetteBuffer::instance().release_ref();
		silhouette_registered = false;
	}
	if (mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->free_rid(mesh);
#else
		RS::get_singleton()->free(mesh);
#endif
		mesh = RID();
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
	// F7/F8: free the existing mesh before clearing the material cache. material_cache
	// is the sole owner of the per-(variant,texture) ShaderMaterial clones, so clearing
	// it frees their RIDs. The mesh's surfaces still reference those freed material RIDs;
	// if the new data is null/unloaded — or in UpdateMode_Manual where build_meshes() is
	// not driven per-frame — the stale mesh would otherwise persist with dangling
	// materials. build_meshes() recreates the mesh on the next update when data is valid.
	if (mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->free_rid(mesh);
#else
		RS::get_singleton()->free(mesh);
#endif
		mesh = RID();
	}
	set_base(RID());
	// Detach the shadows-only instance from the freed mesh (it persists and is re-pointed by the
	// next build_meshes()); the spec keeps shadow_instance alive across skeleton-data changes.
	if (shadow_instance.is_valid()) {
		RS::get_singleton()->instance_set_base(shadow_instance, RID());
		RS::get_singleton()->instance_set_scenario(shadow_instance, RID());
	}
	if (silhouette_instance.is_valid()) {
		RS::get_singleton()->instance_set_base(silhouette_instance, RID());
		RS::get_singleton()->instance_set_scenario(silhouette_instance, RID());
	}
	// Task 4: clear per-instance material cache; textures change with skeleton data
	material_cache.clear();
	// Fix #2: textures change with the skeleton data, so any cached 3D-safe copies are stale.
	texture_3d_cache.clear();
	// Custom-material clones are keyed on texture too; drop them when the skeleton's textures change.
	custom_material_cache.clear();
	custom_shader_uniform.clear();
	// Shadow material clones are keyed on texture; textures change with skeleton data, so drop them.
	shadow_material_cache.clear();
	// Silhouette mask materials are also keyed on texture and point at this mesh's surfaces.
	silhouette_material_cache.clear();
	silhouette_surface_materials.clear();
	silhouette_surface_count = 0;
	silhouette_instance_attached = false;
	silhouette_applied_mesh = RID();
	silhouette_applied_scenario = RID();
	silhouette_surface_generation++;
	// Drop persisted per-surface shadow material RIDs too: the mesh is freed above, so the surface
	// RIDs they refer to are stale and must not be reused against the next (re)built mesh.
	shadow_surface_materials.clear();
	shadow_surface_count = 0;
	// Fix (shadow #5): the mesh was freed and the instance detached out-of-band, so reset the cached
	// applied-state and bump the generation; the next build_meshes()/update_shadow_instance() then does
	// a full re-bind + override re-push instead of skipping on stale cached values.
	shadow_instance_attached = false;
	shadow_applied_mesh = RID();
	shadow_applied_scenario = RID();
	shadow_surface_generation++;
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
			set_process_internal(update_mode == SpineConstant::UpdateMode_Process || silhouette_mask_enabled);
			set_physics_process_internal(update_mode == SpineConstant::UpdateMode_Physics);
			break;
		}
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (update_mode == SpineConstant::UpdateMode_Process)
				update_skeleton(get_process_delta_time());
			else if (silhouette_mask_enabled)
				update_silhouette_instance();
			break;
		}
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (update_mode == SpineConstant::UpdateMode_Physics) update_skeleton(get_physics_process_delta_time());
			break;
		}
		case NOTIFICATION_ENTER_WORLD: {
			// Now safe to call get_world_3d(); record it so update_shadow_instance() may use the world.
			inside_world = true;
			// Re-attach/refresh the shadows-only instance against the (new) world's scenario. This also
			// covers the user's case: build_meshes() may have already run (in _ready / editor-preview)
			// BEFORE the node entered the world — at which point get_world_3d() could not be used, so the
			// shadow caster was deferred. update_shadow_instance() uses the persisted shadow_surface_*
			// state (not the build-local arrays) so it can finish the setup here, even though no further
			// build_meshes() may run in Manual update mode.
			update_shadow_instance();
			if (silhouette_mask_enabled) update_silhouette_instance();
			break;
		}
		case NOTIFICATION_EXIT_WORLD: {
			// No longer in a world: forbid any get_world_3d() access until ENTER_WORLD fires again.
			inside_world = false;
			// Detach from the scenario being torn down so the instance never dangles a freed scenario.
			// (instance_set_scenario does NOT require the world, so this is safe here.)
			if (shadow_instance.is_valid()) RS::get_singleton()->instance_set_scenario(shadow_instance, RID());
			// Fix (shadow #5): reflect the out-of-band detach in the cached state so the next
			// ENTER_WORLD's update_shadow_instance() does a full re-attach (it compares against this).
			shadow_instance_attached = false;
			shadow_applied_scenario = RID();
			if (silhouette_instance.is_valid()) RS::get_singleton()->instance_set_scenario(silhouette_instance, RID());
			silhouette_instance_attached = false;
			silhouette_applied_scenario = RID();
			silhouette_applied_surface_generation = 0;

			break;
		}
		case NOTIFICATION_TRANSFORM_CHANGED:
		case NOTIFICATION_LOCAL_TRANSFORM_CHANGED: {
			// Keep the separate shadows-only instance aligned with the node's global transform.
			if (shadow_instance.is_valid()) RS::get_singleton()->instance_set_transform(shadow_instance, get_global_transform());
			if (silhouette_instance.is_valid()) RS::get_singleton()->instance_set_transform(silhouette_instance, get_global_transform());
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
	// F14: even while hidden, still apply the animation and update world transforms +
	// emit world_transforms_changed so attached SpineBoneNode3D / SpineSlotNode3D children
	// (and gameplay code reading bone transforms) stay current. Only the GPU mesh work
	// (build_meshes / build_debug_mesh) is skipped when hidden.
	emit_signal(SNAME("before_animation_state_apply"), this);
	animation_state->apply(skeleton);
	emit_signal(SNAME("before_world_transforms_change"), this);
	skeleton->update(delta * time_scale);
	skeleton->update_world_transform(SpineConstant::Physics_Update);
	modified_bones = false;
	emit_signal(SNAME("world_transforms_changed"), this);
	if (modified_bones) skeleton->update_world_transform(SpineConstant::Physics_Update);
	if (!is_visible_in_tree()) return;// skip only the GPU rebuild while hidden
	build_meshes();
	build_debug_mesh();// Task 11: rebuild debug line overlay
	if (silhouette_mask_enabled) update_silhouette_instance();
}

// Fix #10: a single fully-resolved CPU surface produced by build_meshes()'s slot
// iteration, before it is committed to the RS mesh. Holding these lets us defer the
// free/recreate decision until we know whether topology matches last frame.
struct SpineSprite3DLocalSurface {
#ifdef SPINE_GODOT_EXTENSION
	PackedVector3Array positions;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
#else
	Vector<Vector3> positions;
	Vector<Vector2> uvs;
	Vector<Color> colors;
	Vector<int> indices;
#endif
	bool shaded = false;
	RID material;           // resolved DISPLAY material RID for this surface (RID() if none assigned)
	RID shadow_material;    // resolved SHADOW caster material RID for this surface (RID() if none / custom material)
	RID silhouette_material;// resolved silhouette mask material RID for this surface
};

// Fix #2: avoid Godot's "texture used in 3D" automatic reimport, which re-imports the
// imported atlas texture with mipmaps + VRAM compression and corrupts packed atlases.
//
// The two builds need different strategies because the module-only RenderingServer API
// (texture_set_detect_3d_callback) is not exposed by godot-cpp:
//   - MODULE: clear the detect-3D callback on the texture's RID so the editor never queues
//     a reimport, then keep using the same imported texture (no copy, no VRAM cost).
//   - GDEXTENSION: there is no way to suppress the callback, so render an ImageTexture COPY
//     of the image instead. The imported resource itself never enters a 3D draw, so the
//     editor never flags it. Copies are cached (keyed on the original rid) so this happens
//     at most once per texture.
//
// The caller keeps its per-(variant,texture) cache keyed on the ORIGINAL texture rid, so the
// substitution here is transparent to that cache.
Ref<Texture2D> SpineSprite3D::get_3d_safe_texture(const Ref<Texture> &tex) {
	// Null/invalid input -> nothing to make safe; return an empty (null) ref.
	if (!tex.is_valid()) return Ref<Texture2D>();

	// SpineRendererObject holds Ref<Texture>; the shader sampler needs a Texture2D. Atlas
	// textures are ImageTexture (a Texture2D), so this cast succeeds in practice.
	Texture2D *tex2d = Object::cast_to<Texture2D>(tex.ptr());
	if (!tex2d) return Ref<Texture2D>();
	Ref<Texture2D> tex2d_ref(tex2d);

#ifndef SPINE_GODOT_EXTENSION
	// Module build: stop the editor from ever reimporting this texture for 3D usage.
	// texture_set_detect_3d_callback exists in the engine; the 2nd arg is a C callback type
	// that differs by version, but passing nullptr is type-correct on both 4.6 and 4.7.
	RS::get_singleton()->texture_set_detect_3d_callback(tex2d_ref->get_rid(), nullptr, nullptr);
	return tex2d_ref;
#else
	// GDExtension build: render an ImageTexture copy so the imported resource never draws in 3D.
	// A runtime ImageTexture (no resource path) is already safe — use it directly.
	if (tex2d_ref->get_path().is_empty()) return tex2d_ref;

	uint64_t rid_id = (uint64_t) tex2d_ref->get_rid().get_id();
	if (texture_3d_cache.has(rid_id)) return texture_3d_cache[rid_id];

	Ref<Image> img = tex2d_ref->get_image();
	if (img.is_null() || img->is_empty()) return tex2d_ref;
	Ref<ImageTexture> it = ImageTexture::create_from_image(img);
	texture_3d_cache[rid_id] = it;
	return it;
#endif
}

// Custom-material opt-in probe. Returns the name of the recognized texture uniform a user's custom
// shader declares — "spine_texture" (the fork's convention, so those shaders port here unchanged) or
// "albedo_texture" — or StringName() if it declares neither. When non-empty, flush() binds the atlas
// texture to that uniform on a per-(material,texture) clone; when empty, the material is used verbatim.
// Only the sampler NAME is matched (not its type); a user who declares one of these names is opting in.
// flush() memoizes the result per shader RID in custom_shader_uniform, so this runs once per shader,
// not every frame.
static StringName spine_sprite_3d_custom_texture_uniform(const Ref<Shader> &shader) {
	if (!shader.is_valid()) return StringName();
	static const char *CANDIDATES[] = {"spine_texture", "albedo_texture"};
#ifdef SPINE_GODOT_EXTENSION
	Array params = shader->get_shader_uniform_list(false);
	for (int i = 0; i < params.size(); i++) {
		Dictionary d = params[i];
		String name = d.get("name", String());
		for (const char *candidate : CANDIDATES) {
			if (name == candidate) return StringName(candidate);
		}
	}
#else
	List<PropertyInfo> params;
	shader->get_shader_uniform_list(&params, false);
	for (const PropertyInfo &pi : params) {
		for (const char *candidate : CANDIDATES) {
			if (pi.name == candidate) return StringName(candidate);
		}
	}
#endif
	return StringName();
}

void SpineSprite3D::apply_custom_material_uniforms(const Ref<ShaderMaterial> &mat) const {
	if (!mat.is_valid()) return;
	// Same standard uniforms the built-in material clones receive (SpineSprite3D.cpp flush()). Set
	// unconditionally: assigning a parameter the shader does not declare is harmless in Godot. A
	// custom shader that DOES declare them (typically via spine_sprite_3d.gdshaderinc) then gets the
	// same live billboard / z_spacing / depth_offset / modulate / scissor / fixed_size values.
	mat->set_shader_parameter("billboard_mode", (int) billboard);
	mat->set_shader_parameter("layer_z_spacing", z_spacing);
	mat->set_shader_parameter("depth_offset", depth_offset);
	mat->set_shader_parameter("modulate_color", modulate);
	mat->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
	mat->set_shader_parameter("fixed_size_enabled", fixed_size);
}

void SpineSprite3D::build_meshes() {
	// F1/F6: if there is no skeleton, clear the instance base (the mesh RID, if any,
	// would otherwise be referenced by the GeometryInstance3D) and free the mesh.
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) {
		set_base(RID());
		// Detach the shadows-only instance from its now-stale base/scenario before the mesh is
		// freed (the instance itself is reused and freed in the destructor). Drop its per-surface
		// shadow material clones too, since the mesh surfaces are gone.
		if (shadow_instance.is_valid()) {
			RS::get_singleton()->instance_set_base(shadow_instance, RID());
			RS::get_singleton()->instance_set_scenario(shadow_instance, RID());
		}
		// Same for the silhouette mask instance: detach it from the mesh being freed (it is rebuilt by
		// update_silhouette_instance() on the next build with a valid mesh).
		if (silhouette_instance.is_valid()) {
			RS::get_singleton()->instance_set_base(silhouette_instance, RID());
			RS::get_singleton()->instance_set_scenario(silhouette_instance, RID());
		}
		silhouette_material_cache.clear();
		silhouette_surface_materials.clear();
		silhouette_surface_count = 0;
		silhouette_instance_attached = false;
		silhouette_applied_mesh = RID();
		silhouette_applied_scenario = RID();
		silhouette_applied_surface_generation = 0;
		silhouette_surface_generation++;
		shadow_material_cache.clear();
		// Mesh is being freed below; drop the persisted per-surface shadow RIDs so they are not
		// reused against a future mesh (parallel to clearing shadow_material_cache).
		shadow_surface_materials.clear();
		shadow_surface_count = 0;
		// Fix (shadow #5): we detached the instance and are freeing the mesh out-of-band here, so reset
		// the cached applied-state. Otherwise a future build would see a stale shadow_applied_mesh/
		// scenario and could skip the re-bind. A generation bump forces the override loop to re-run too.
		shadow_instance_attached = false;
		shadow_applied_mesh = RID();
		shadow_applied_scenario = RID();
		shadow_surface_generation++;
		if (mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
			RS::get_singleton()->free_rid(mesh);
#else
			RS::get_singleton()->free(mesh);
#endif
			mesh = RID();
		}
		surface_cache.clear();
		debug_active_last_frame = false;
		return;
	}
	spine::Skeleton *sk = skeleton->get_spine_object();
	auto &statics = SpineSprite3DStatics::instance();

	// Shadow casting: honor the inherited GeometryInstance3D cast_shadow setting. When any casting
	// mode is selected, the generated shader uses depth_prepass_alpha to cast a shaped (alpha-cutout)
	// shadow; OFF keeps shadows_disabled. Computed once here and threaded into the material/cache keys.
	// cast_shadow changes are picked up on the next build_meshes() (every frame for animating
	// skeletons); a fully static / Manual-update skeleton may need a refresh to start/stop casting.
	const bool casts = get_cast_shadows_setting() != SHADOW_CASTING_SETTING_OFF;

	AABB aabb;
	bool aabb_init = false;
	// F12: track the max distance from the model origin (0,0,0) to any vertex.
	// The billboard vertex shader rotates geometry about MODEL_MATRIX[3] (the
	// model origin), not the geometry centroid, so the swept bounding sphere is
	// centered on the origin with this radius.
	float billboard_radius_sq = 0.0f;

	// Fix #10: collect resolved surfaces here first; the slow/fast path is chosen afterwards.
	Vector<SpineSprite3DLocalSurface> local_surfaces;

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

	// Task 4: Flush the accumulated scratch buffers as one resolved surface.
	// Per-instance material cache: look up or create a ShaderMaterial for (variant, texture).
	// Key: high byte = blend*4 + shaded*2 + pma; lower 56 bits = texture RID id.
	// This ensures each surface gets its own correctly-textured material independent of other
	// SpineSprite3D instances — fixing the Task 2 shared-material / last-write-wins bug.
	//
	// Fix #10: this no longer touches the RS mesh; it resolves the material RID exactly as
	// before and appends a SpineSprite3DLocalSurface. Whether the mesh is rebuilt or
	// region-updated is decided once after all surfaces are collected.
	//
	// Shadow caster resolver: returns the cutout SHADOW material RID for (texture, filter), cloned from
	// the shared per-filter base shadow shader and cached per (filter, texture) in shadow_material_cache.
	// The shadow shader is a minimal alpha-scissor cutout INDEPENDENT of the display material, so it is
	// shared by built-in AND opted-in custom-material surfaces alike — both only need the atlas alpha
	// plus the standard vertex uniforms so the silhouette tracks the rendered card. Keyed on (filter,
	// texture) only (pma-independent). Caller gates on `casts`.
	auto resolve_shadow_rid = [&](const Ref<Texture> &texture, uint64_t tex_id) -> RID {
		uint64_t shadow_variant_bits = (uint64_t) ((int) texture_filter & 0x3);
		uint64_t shadow_cache_key = (shadow_variant_bits << 56) | (tex_id & 0x00FFFFFFFFFFFFFFull);
		Ref<ShaderMaterial> smat;
		if (shadow_material_cache.has(shadow_cache_key)) {
			smat = shadow_material_cache[shadow_cache_key];
		} else {
			Ref<ShaderMaterial> base_shadow = statics.get_shadow_material(texture_filter);
			smat.instantiate();
			smat->set_shader(base_shadow->get_shader());
			smat->set_shader_parameter("albedo_tex", get_3d_safe_texture(texture));
			// Mirror the display clone's vertex/cutout uniforms so the shadow tracks the rendered card:
			// billboard orientation, fixed_size scaling, per-part z_spacing + depth_offset push, the
			// scissor threshold, and the modulate fade.
			smat->set_shader_parameter("billboard_mode", (int) billboard);
			smat->set_shader_parameter("fixed_size_enabled", fixed_size);
			smat->set_shader_parameter("modulate_color", modulate);
			smat->set_shader_parameter("layer_z_spacing", z_spacing);
			smat->set_shader_parameter("depth_offset", depth_offset);
			smat->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
			shadow_material_cache[shadow_cache_key] = smat;
		}
		return smat->get_rid();
	};

	auto resolve_silhouette_rid = [&](const Ref<Texture> &texture, uint64_t tex_id) -> RID {
		if (!silhouette_mask_enabled || !texture.is_valid()) return RID();
		uint64_t silhouette_variant_bits = (uint64_t) ((int) texture_filter & 0x3);
		uint64_t silhouette_cache_key = (silhouette_variant_bits << 56) | (tex_id & 0x00FFFFFFFFFFFFFFull);
		Ref<ShaderMaterial> mm;
		if (silhouette_material_cache.has(silhouette_cache_key)) {
			mm = silhouette_material_cache[silhouette_cache_key];
		} else {
			mm.instantiate();
			mm->set_shader(SpineSilhouetteBuffer::instance().get_mask_shader(texture_filter));
			mm->set_shader_parameter("albedo_tex", get_3d_safe_texture(texture));
			mm->set_shader_parameter("spine_object_id", (float) silhouette_id / 255.0f);
			mm->set_shader_parameter("billboard_mode", (int) billboard);
			mm->set_shader_parameter("fixed_size_enabled", fixed_size);
			mm->set_shader_parameter("modulate_color", modulate);
			mm->set_shader_parameter("layer_z_spacing", z_spacing);
			mm->set_shader_parameter("depth_offset", depth_offset);
			mm->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
			silhouette_material_cache[silhouette_cache_key] = mm;
		}
		return mm->get_rid();
	};

	auto flush = [&]() {
		if (scratch_indices.size() == 0) return;

		// Task 9: slot-node material override takes precedence over sprite-level (Task 8) and library clone.
		Ref<Material> custom_mat;
		if (current_slot_node) {
			switch (current_blend) {
				case spine::BlendMode_Normal:
					custom_mat = current_slot_node->get_normal_material();
					break;
				case spine::BlendMode_Additive:
					custom_mat = current_slot_node->get_additive_material();
					break;
				case spine::BlendMode_Multiply:
					custom_mat = current_slot_node->get_multiply_material();
					break;
				default:
					custom_mat = current_slot_node->get_screen_material();
					break;
			}
		}
		// Task 8: fall back to sprite-level per-blend-mode custom material.
		if (!custom_mat.is_valid()) {
			switch (current_blend) {
				case spine::BlendMode_Normal:
					custom_mat = normal_material;
					break;
				case spine::BlendMode_Additive:
					custom_mat = additive_material;
					break;
				case spine::BlendMode_Multiply:
					custom_mat = multiply_material;
					break;
				default:
					custom_mat = screen_material;
					break;// Screen (rare/none in practice)
			}
		}

		RID surface_material;
		RID surface_shadow_material;// SHADOW caster override for this surface (auto-generated surfaces only)
		RID surface_silhouette_material;
		uint64_t current_tex_id = (current_ro && current_ro->texture.is_valid()) ? (uint64_t) current_ro->texture->get_rid().get_id() : 0ull;
		if (custom_mat.is_valid()) {
			// A custom ShaderMaterial may OPT IN to automatic atlas binding by declaring a
			// "spine_texture" or "albedo_texture" sampler2D uniform (spatial shaders have no
			// draw-time TEXTURE like 2D canvas_item shaders do, so the texture must arrive as a
			// uniform). When it does, hand the surface a per-(material, texture) CLONE with that
			// uniform set to the 3D-safe atlas texture, and leave everything else about the material
			// exactly as authored. When it declares neither uniform — or is not a ShaderMaterial
			// (e.g. StandardMaterial3D) — it is used verbatim, exactly as before.
			//
			// F13 NOTE (still applies): the spine mesh winding is reversed by the base Y-flip
			// (sy = -pixel_size). We do NOT rewrite the user's render_mode, so a custom material
			// MUST disable back-face culling (cull_disabled / no cull) or the slot renders invisible.
			//
			// Shadows: OPTED-IN custom materials (declaring spine_texture/albedo_texture) DO cast the auto
			// alpha-cutout shadow — see the resolve_shadow_rid call below. Verbatim custom materials (no
			// recognized uniform, or a non-ShaderMaterial) still get no auto shadow, since we cannot know
			// their atlas usage.
			Ref<ShaderMaterial> shader_mat = custom_mat;// invalid Ref if custom_mat is not a ShaderMaterial

			// Opt-in detection, memoized per shader RID so the uniform list is scanned once, not per frame.
			StringName tex_uniform;
			if (shader_mat.is_valid()) {
				Ref<Shader> shd = shader_mat->get_shader();
				uint64_t shader_id = shd.is_valid() ? (uint64_t) shd->get_rid().get_id() : 0;
				if (custom_shader_uniform.has(shader_id)) {
					tex_uniform = custom_shader_uniform[shader_id];
				} else {
					tex_uniform = spine_sprite_3d_custom_texture_uniform(shd);
					custom_shader_uniform[shader_id] = tex_uniform;
				}
			}

			if (shader_mat.is_valid() && tex_uniform != StringName() && current_ro && current_ro->texture.is_valid()) {
				// Opted in: hand the surface a per-(material,texture) clone with the atlas + standard
				// uniforms bound. Key = hash-combine(custom material rid, texture rid); detection + clone
				// run once per unique pair, later frames hit the cache. The cache holds ONLY these owned
				// clones, so the live uniform setters can safely update them.
				uint64_t mat_id = (uint64_t) shader_mat->get_rid().get_id();
				uint64_t tex_id = current_tex_id;
				uint64_t key = mat_id;
				key ^= tex_id + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);

				Ref<ShaderMaterial> clone;
				if (custom_material_cache.has(key)) {
					clone = custom_material_cache[key];
				} else {
					// Shallow duplicate: shares the Shader resource but copies the parameter map, so
					// setting parameters on the clone never mutates the user's (possibly shared) material.
					clone = shader_mat->duplicate();
					if (clone.is_valid()) {
						clone->set_shader_parameter(tex_uniform, get_3d_safe_texture(current_ro->texture));
						apply_custom_material_uniforms(clone);
						custom_material_cache[key] = clone;
					} else {
						clone = shader_mat;// duplicate failed -> fall back to verbatim
					}
				}
				surface_material = clone->get_rid();

				// Opted-in custom materials cast the SAME auto cutout shadow as built-in surfaces: the
				// shadow shader is independent of the display material and only needs the atlas alpha +
				// the standard vertex uniforms. Requires the node's cast_shadow ON and the custom
				// shader's render_mode to include shadows_disabled (as the include's examples do) so the
				// main mesh does not also cast and double up. The shadow tracks the card via the mirrored
				// billboard/z_spacing/depth_offset uniforms, so it matches when the custom shader uses
				// spine_apply_vertex; a divergent custom vertex may not track.
				if (casts) surface_shadow_material = resolve_shadow_rid(current_ro->texture, tex_id);
			} else {
				// Not a ShaderMaterial, declares no recognized uniform, or no texture yet -> verbatim.
				surface_material = custom_mat->get_rid();
			}
		} else if (current_ro && current_ro->texture.is_valid()) {
			// Build cache key: 10-bit shader variant key in the top bits, texture identity in the lower
			// 54 bits. The variant key (pma|shaded|blend|filter|alpha_cut|no_depth_test|double_sided)
			// matches the statics variant cache. The key stays keyed on the ORIGINAL texture rid (not the
			// 3D-safe copy), so it is stable across frames.
			//
			// Fix #4: the shaded variant also binds normal_tex/specular_tex (set only on a cache MISS), so
			// the key MUST include the normal/specular map identity — otherwise two shaded surfaces sharing
			// one albedo page but using DIFFERENT normal/specular maps collide and the second silently
			// reuses the first's maps. Hash-combine all three rids into the lower 54 bits (the normal/
			// specular maps only exist on the shaded path, so unshaded surfaces are unaffected and don't
			// fragment the cache). The variant bits stay in the top 10 bits and never collide with this.
			uint64_t variant_bits = (uint64_t) SpineSprite3DStatics::variant_key(current_blend, shaded, current_pma, texture_filter, alpha_cut,
																				 no_depth_test, double_sided);
			uint64_t tex_id = current_tex_id;
			uint64_t tex_identity = tex_id;
			if (shaded) {
				// Boost-style hash_combine so distinct (albedo, normal, specular) triples map to distinct
				// lower-54-bit values. Invalid maps contribute their 0 rid id (a stable, distinct slot).
				uint64_t normal_id = current_ro->normal_map.is_valid() ? (uint64_t) current_ro->normal_map->get_rid().get_id() : 0ull;
				uint64_t specular_id = current_ro->specular_map.is_valid() ? (uint64_t) current_ro->specular_map->get_rid().get_id() : 0ull;
				tex_identity ^= normal_id + 0x9E3779B97F4A7C15ull + (tex_identity << 6) + (tex_identity >> 2);
				tex_identity ^= specular_id + 0x9E3779B97F4A7C15ull + (tex_identity << 6) + (tex_identity >> 2);
			}
			uint64_t cache_key = (variant_bits << 54) | (tex_identity & 0x003FFFFFFFFFFFFFull);

			Ref<ShaderMaterial> mat;
			if (material_cache.has(cache_key)) {
				mat = material_cache[cache_key];
			} else {
				// Clone the shared shader variant into a fresh per-(variant,texture) material
				Ref<ShaderMaterial> variant_mat = statics.get_material(current_blend, shaded, current_pma, texture_filter, alpha_cut, no_depth_test,
																	   double_sided);
				mat.instantiate();
				mat->set_shader(variant_mat->get_shader());
				mat->set_shader_parameter("albedo_tex", get_3d_safe_texture(current_ro->texture));
				mat->set_shader_parameter("billboard_mode", (int) billboard);
				// Sprite3D-style uniform controls (set on every clone; harmless when not used by the variant).
				mat->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
				mat->set_shader_parameter("fixed_size_enabled", fixed_size);
				mat->set_shader_parameter("modulate_color", modulate);
				mat->set_shader_parameter("layer_z_spacing", z_spacing);
				mat->set_shader_parameter("depth_offset", depth_offset);
				mat->set_render_priority(render_priority);
				if (shaded) {
					bool has_normal = current_ro->normal_map.is_valid();
					bool has_specular = current_ro->specular_map.is_valid();
					if (has_normal) mat->set_shader_parameter("normal_tex", get_3d_safe_texture(current_ro->normal_map));
					mat->set_shader_parameter("use_normal_tex", has_normal);
					if (has_specular) mat->set_shader_parameter("specular_tex", get_3d_safe_texture(current_ro->specular_map));
					mat->set_shader_parameter("use_specular_tex", has_specular);
				}
				material_cache[cache_key] = mat;
			}
			surface_material = mat->get_rid();

			// Shadow caster: cutout material keyed on this surface's (texture, filter). Only when the
			// node casts; assigned as a surface override on the separate SHADOWS_ONLY instance below.
			if (casts) surface_shadow_material = resolve_shadow_rid(current_ro->texture, tex_id);
		}
		if (current_ro && current_ro->texture.is_valid()) surface_silhouette_material = resolve_silhouette_rid(current_ro->texture, current_tex_id);

		SpineSprite3DLocalSurface ls;
		ls.positions = scratch_positions;
		ls.uvs = scratch_uvs;
		ls.colors = scratch_colors;
		ls.indices = scratch_indices;
		ls.shaded = shaded;
		ls.material = surface_material;
		ls.shadow_material = surface_shadow_material;
		ls.silhouette_material = surface_silhouette_material;
		local_surfaces.push_back(ls);

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
		spine::Color tint(sk_color.r * slot_color.r, sk_color.g * slot_color.g, sk_color.b * slot_color.b, sk_color.a * slot_color.a);

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
				quad_idx[0] = 0;
				quad_idx[1] = 1;
				quad_idx[2] = 2;
				quad_idx[3] = 2;
				quad_idx[4] = 3;
				quad_idx[5] = 0;
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

		int vertex_base = (int) scratch_positions.size();
		int num_verts = (int) world_verts->size() / 2;
		// Fix: store the RAW draw-order index in local z (NOT pre-multiplied by z_spacing) so the
		// vertex shader can recover the index for depth_offset at ANY z_spacing (including the
		// default 0). The shader applies z_spacing via the layer_z_spacing uniform (VERTEX.z *=
		// layer_z_spacing). The AABB is therefore computed with raw indices in z and its z-extent
		// is scaled by z_spacing below so culling matches the shader-applied world spacing.
		float z = -((float) i);

		float sx = flip_h ? -pixel_size : pixel_size;
		float sy = flip_v ? pixel_size : -pixel_size;// base is -pixel_size (Y-flip); flip_v cancels it

		for (int v = 0; v < num_verts; v++) {
			float x = world_verts->buffer()[v * 2] * sx;
			float y = world_verts->buffer()[v * 2 + 1] * sy;
			// Stored z is the RAW draw index (shader multiplies by layer_z_spacing). For culling we
			// need the WORLD-space z the shader actually produces, so scale by z_spacing here only.
			Vector3 pos(x, y, z);
			scratch_positions.push_back(pos);
			scratch_uvs.push_back(Vector2(uvs->buffer()[v * 2], uvs->buffer()[v * 2 + 1]));
			scratch_colors.push_back(Color(tint.r, tint.g, tint.b, tint.a));

			Vector3 world_pos(x, y, z * z_spacing);// shader-applied world position, for AABB/billboard bounds
			if (!aabb_init) {
				aabb.position = world_pos;
				aabb.size = Vector3();
				aabb_init = true;
			} else {
				aabb.expand_to(world_pos);
			}

			// F12: distance from the model origin, used to bound billboard rotation.
			float dist_sq = world_pos.x * world_pos.x + world_pos.y * world_pos.y + world_pos.z * world_pos.z;
			if (dist_sq > billboard_radius_sq) billboard_radius_sq = dist_sq;
		}

		for (int t = 0; t < (int) indices->size(); t++) {
			scratch_indices.push_back(vertex_base + (int) indices->buffer()[t]);
		}

		skeleton_clipper->clipEnd(*slot);
	}
	skeleton_clipper->clipEnd();

	flush();// flush final surface

	// Task 5 / F12: when billboarding, the AABB becomes a cube centered on the MODEL
	// ORIGIN (not the geometry centroid) so rotation about the origin never causes
	// frustum culling. The billboard vertex shader rotates geometry about
	// MODEL_MATRIX[3] (the origin), so the swept volume is a sphere centered at
	// (0,0,0) with radius = max distance from origin to any vertex.
	AABB final_aabb = aabb;
	if (aabb_init && billboard != BILLBOARD_DISABLED) {
		float r = spine::MathUtil::sqrt(billboard_radius_sq);
		final_aabb = AABB(Vector3(-r, -r, -r), Vector3(2 * r, 2 * r, 2 * r));
	}
	// Fix: when fixed_size is enabled the vertex shader rescales the card every frame to keep a
	// constant screen size (MODELVIEW columns are multiplied by a view/projection-derived factor),
	// so the rendered geometry no longer fits the tight local-space AABB computed above and the
	// sprite would be wrongly frustum-culled when it pops out of that small box. Mirror Godot's
	// FLAG_FIXED_SIZE handling (which disables culling for fixed-size geometry) by using a large,
	// effectively-unbounded custom AABB so a fixed_size sprite is never falsely culled. Applied to
	// BOTH the mesh custom AABB (used by the shadow caster instance) and the display instance AABB
	// below. Keep the tight AABB when fixed_size is false.
	if (aabb_init && fixed_size) {
		const float big = 1.0e9f;
		final_aabb = AABB(Vector3(-big, -big, -big), Vector3(2 * big, 2 * big, 2 * big));
	}

	const int surface_count = local_surfaces.size();

	// Persist the per-surface shadow materials so update_shadow_instance() can be driven later from
	// NOTIFICATION_ENTER_WORLD (outside build_meshes(), where local_surfaces is gone). One RID per
	// mesh surface (RID() for custom-material/no-shadow surfaces). Mirror the build-local result here.
	// Fix (shadow #5): rewrite the persisted per-surface shadow set, and bump the generation ONLY when
	// it actually changed (count or any RID differs) vs last frame. update_shadow_instance() compares
	// this generation to what it last pushed and re-runs the per-surface override loop only on a change,
	// so the animated fast path (identical surfaces every frame) skips that loop entirely.
	bool shadow_set_changed = (shadow_surface_count != surface_count);
	shadow_surface_count = surface_count;
	shadow_surface_materials.resize(surface_count);
	for (int s = 0; s < surface_count; s++) {
		const RID new_sm = local_surfaces[s].shadow_material;
		if (!shadow_set_changed && shadow_surface_materials[s] != new_sm) shadow_set_changed = true;
		shadow_surface_materials.write[s] = new_sm;
	}
	if (shadow_set_changed) shadow_surface_generation++;

	if (silhouette_mask_enabled) {
		bool silhouette_set_changed = (silhouette_surface_count != surface_count);
		silhouette_surface_count = surface_count;
		silhouette_surface_materials.resize(surface_count);
		for (int s = 0; s < surface_count; s++) {
			const RID new_sm = local_surfaces[s].silhouette_material;
			if (!silhouette_set_changed && silhouette_surface_materials[s] != new_sm) silhouette_set_changed = true;
			silhouette_surface_materials.write[s] = new_sm;
		}
		if (silhouette_set_changed) silhouette_surface_generation++;
	} else if (silhouette_surface_count != 0 || silhouette_surface_materials.size() != 0) {
		silhouette_surface_count = 0;
		silhouette_surface_materials.clear();
		silhouette_surface_generation++;
	}

	// Fix #10: the debug overlay (build_debug_mesh) appends a PRIMITIVE_LINES surface
	// to this same mesh AFTER build_meshes() returns; that surface is not region-updatable
	// here. Whenever debug overlays are enabled this frame OR were last frame, take the
	// SLOW PATH so the freshly-created mesh has exactly the right surface set for
	// build_debug_mesh to append to (and any stale debug surface is gone). This is the
	// simplest provably-correct rule.
	const bool debug_active_this_frame = debug_root || debug_bones || debug_regions || debug_meshes || debug_bounding_boxes || debug_paths ||
		debug_clipping;
	const bool debug_forces_slow = debug_active_this_frame || debug_active_last_frame;

	// Decide FAST PATH: existing mesh, no debug involvement, and per-surface topology
	// (count, vertex/index counts, shaded vertex layout, index contents, material RID)
	// all identical to last frame. Otherwise SLOW PATH.
	bool can_fast = mesh.is_valid() && !debug_forces_slow && surface_cache.size() == surface_count;
	if (can_fast) {
		for (int s = 0; s < surface_count; s++) {
			const SpineSprite3DLocalSurface &ls = local_surfaces[s];
			const SurfaceCache &sc = surface_cache[s];
			if (sc.num_vertices != (int) ls.positions.size() || sc.num_indices != (int) ls.indices.size() || sc.shaded != ls.shaded ||
				sc.material != ls.material || sc.indices != ls.indices) {
				can_fast = false;
				break;
			}
		}
	}

	if (can_fast) {
		// FAST PATH: update existing surface buffers in place (no free / recreate).
		for (int s = 0; s < surface_count; s++) {
			const SpineSprite3DLocalSurface &ls = local_surfaces[s];
			SurfaceCache &sc = surface_cache.ptrw()[s];// Vector::operator[] is const; need a mutable ref
			const int vc = sc.num_vertices;

			uint8_t *vertex_write = sc.vertex_buffer.ptrw();
			uint8_t *attribute_write = sc.attribute_buffer.ptrw();
			const uint32_t v_off = sc.surface_offsets[SPINE_RS_ENUM::ARRAY_VERTEX];
			const uint32_t c_off = sc.surface_offsets[SPINE_RS_ENUM::ARRAY_COLOR];
			const uint32_t uv_off = sc.surface_offsets[SPINE_RS_ENUM::ARRAY_TEX_UV];

			for (int v = 0; v < vc; v++) {
				// Positions: only the ARRAY_VERTEX slot changes. For shaded surfaces the
				// constant normal/tangent bytes live elsewhere in the same vertex stride and
				// are deliberately left untouched, preserving the validated shaded layout.
				const Vector3 &p = ls.positions[v];
				float pos[3] = {(float) p.x, (float) p.y, (float) p.z};
				memcpy(&vertex_write[v * sc.vertex_stride + v_off], pos, sizeof(float) * 3);

				const Color &col = ls.colors[v];
				uint8_t color[4] = {uint8_t(CLAMP(col.r * 255.0, 0.0, 255.0)), uint8_t(CLAMP(col.g * 255.0, 0.0, 255.0)),
									uint8_t(CLAMP(col.b * 255.0, 0.0, 255.0)), uint8_t(CLAMP(col.a * 255.0, 0.0, 255.0))};
				memcpy(&attribute_write[v * sc.attribute_stride + c_off], color, 4);

				const Vector2 &t = ls.uvs[v];
				float uv[2] = {(float) t.x, (float) t.y};
				memcpy(&attribute_write[v * sc.attribute_stride + uv_off], uv, sizeof(float) * 2);
			}

			RS::get_singleton()->mesh_surface_update_vertex_region(mesh, s, 0, sc.vertex_buffer);
			RS::get_singleton()->mesh_surface_update_attribute_region(mesh, s, 0, sc.attribute_buffer);
		}
		if (aabb_init) {
			RS::get_singleton()->mesh_set_custom_aabb(mesh, final_aabb);
			// Also set the instance AABB (same as Godot's SpriteBase3D::_draw) so the
			// GeometryInstance3D reports a real bounding volume to culling / editor gizmos.
			set_custom_aabb(final_aabb);
		}
		// Surface topology is unchanged on the fast path, but the cast_shadow setting (or world
		// membership) may have toggled since last frame, so refresh the shadow instance here too.
		update_shadow_instance();
		if (silhouette_mask_enabled) update_silhouette_instance();
		// Base already set last (slow) frame; nothing else to do. No debug surface on this path.
		debug_active_last_frame = false;
		return;
	}

	// SLOW PATH: free + recreate the mesh, add all surfaces, assign materials, refresh cache.
	if (mesh.is_valid()) {
#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->free_rid(mesh);
#else
		RS::get_singleton()->free(mesh);
#endif
		mesh = RID();
	}
	surface_cache.clear();
	mesh = RS::get_singleton()->mesh_create();

	for (int s = 0; s < surface_count; s++) {
		const SpineSprite3DLocalSurface &ls = local_surfaces[s];

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = ls.positions;
		arrays[Mesh::ARRAY_TEX_UV] = ls.uvs;
		arrays[Mesh::ARRAY_COLOR] = ls.colors;
		arrays[Mesh::ARRAY_INDEX] = ls.indices;

		// Task 6: shaded surfaces need per-vertex normals and tangents so the spatial shader
		// can write NORMAL_MAP without triggering Godot's "mesh missing tangents" warning.
		// The card faces +Z in local space; billboard reorients toward camera at render time.
		// Unshaded surfaces skip these arrays to avoid unnecessary vertex data.
		if (ls.shaded) {
			int vc = (int) ls.positions.size();
#ifdef SPINE_GODOT_EXTENSION
			PackedVector3Array normals;
			PackedFloat32Array tangents;
			normals.resize(vc);
			tangents.resize(vc * 4);
			for (int ni = 0; ni < vc; ni++) {
				normals[ni] = Vector3(0, 0, 1);
				tangents[ni * 4 + 0] = 1.0f;// tangent X
				tangents[ni * 4 + 1] = 0.0f;// tangent Y
				tangents[ni * 4 + 2] = 0.0f;// tangent Z
				tangents[ni * 4 + 3] = 1.0f;// binormal sign
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

		SurfaceCache sc;
		sc.num_vertices = (int) ls.positions.size();
		sc.num_indices = (int) ls.indices.size();
		sc.shaded = ls.shaded;
		sc.indices = ls.indices;
		sc.material = ls.material;

#ifdef SPINE_GODOT_EXTENSION
		RS::get_singleton()->mesh_add_surface_from_arrays(mesh, SPINE_RS_ENUM::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
														  SPINE_RS_ENUM::ARRAY_FLAG_USE_DYNAMIC_UPDATE);
		// Capture the surface layout + buffers for subsequent fast-path region updates.
		Dictionary surface = RS::get_singleton()->mesh_get_surface(mesh, s);
		SPINE_RS_ENUM::ArrayFormat surface_format = (SPINE_RS_ENUM::ArrayFormat) static_cast<int64_t>(surface["format"]);
		sc.surface_offsets[SPINE_RS_ENUM::ARRAY_VERTEX] = RS::get_singleton()->mesh_surface_get_format_offset(surface_format, sc.num_vertices,
																											  SPINE_RS_ENUM::ARRAY_VERTEX);
		sc.surface_offsets[SPINE_RS_ENUM::ARRAY_COLOR] = RS::get_singleton()->mesh_surface_get_format_offset(surface_format, sc.num_vertices,
																											 SPINE_RS_ENUM::ARRAY_COLOR);
		sc.surface_offsets[SPINE_RS_ENUM::ARRAY_TEX_UV] = RS::get_singleton()->mesh_surface_get_format_offset(surface_format, sc.num_vertices,
																											  SPINE_RS_ENUM::ARRAY_TEX_UV);
		sc.vertex_stride = RS::get_singleton()->mesh_surface_get_format_vertex_stride(surface_format, sc.num_vertices);
		sc.attribute_stride = RS::get_singleton()->mesh_surface_get_format_attribute_stride(surface_format, sc.num_vertices);
		sc.vertex_buffer = surface["vertex_data"];
		sc.attribute_buffer = surface["attribute_data"];
#else
		SPINE_RS_TYPE::SurfaceData surface;
		uint32_t skin_stride = 0;
		RS::get_singleton()->mesh_create_surface_data_from_arrays(&surface, (SPINE_RS_ENUM::PrimitiveType) Mesh::PRIMITIVE_TRIANGLES, arrays,
																  TypedArray<Array>(), Dictionary(),
																  Mesh::ArrayFormat::ARRAY_FLAG_USE_DYNAMIC_UPDATE);
		RS::get_singleton()->mesh_add_surface(mesh, surface);
		RS::get_singleton()->mesh_surface_make_offsets_from_format(surface.format, surface.vertex_count, surface.index_count, sc.surface_offsets,
																   sc.vertex_stride, sc.normal_tangent_stride, sc.attribute_stride, skin_stride);
		sc.vertex_buffer = surface.vertex_data;
		sc.attribute_buffer = surface.attribute_data;
#endif

		if (ls.material.is_valid()) {
			RS::get_singleton()->mesh_surface_set_material(mesh, s, ls.material);
		}

		surface_cache.push_back(sc);
	}

	if (aabb_init) {
		RS::get_singleton()->mesh_set_custom_aabb(mesh, final_aabb);
		// Also set the instance AABB (same as Godot's SpriteBase3D::_draw) so the
		// GeometryInstance3D reports a real bounding volume to culling / editor gizmos.
		set_custom_aabb(final_aabb);
	}
	set_base(mesh);
	// Re-point/refresh the separate shadows-only instance at the freshly (re)built mesh and its
	// new surfaces. build_debug_mesh() appends extra surfaces AFTER this, but those are debug-only
	// overlays that should not cast, and we never set shadow overrides on them, so the shadow
	// instance correctly ignores them (they fall back to their depth_test_disabled debug material).
	update_shadow_instance();
	if (silhouette_mask_enabled) update_silhouette_instance();
	// build_debug_mesh() runs next and may append a PRIMITIVE_LINES surface to this mesh.
	// Record this frame's debug state so the next frame forces a slow rebuild if debug
	// was (or becomes) active, keeping surface_cache aligned with the renderable surfaces.
	debug_active_last_frame = debug_active_this_frame;
}

// ---------------------------------------------------------------------------
// Shadow caster instance management. The display path never casts (its materials are
// depth_draw_opaque/shadows_disabled). When this node casts AND it is inside a world, keep a
// SEPARATE RS instance bound to the SAME mesh RID but rendering into shadow maps ONLY, with
// per-surface alpha-cutout override materials so it casts a shaped shadow without any z-fight in
// the visible pass. When not casting (or not in a world), detach the instance so it stops
// rendering (it is freed only in the destructor).
//
// CRITICAL (the bug this method fixes): Node3D::get_world_3d() PRINTS an error and returns an
// invalid ref whenever the node is not inside the world, and is_inside_world() is not exposed in
// godot-cpp (the extension build). So get_world_3d() is gated entirely behind the inside_world
// flag — when false we never touch it (and simply detach the instance's scenario, which does not
// need the world). This makes the method safe to call while the node is built but detached from
// the tree (the user's _ready / editor-preview case).
//
// Operates on the PERSISTED shadow_surface_* state (not build-local arrays) so it can also be
// driven from NOTIFICATION_ENTER_WORLD after build_meshes() already ran while detached. Called
// from both build_meshes() paths and from NOTIFICATION_ENTER_WORLD.
void SpineSprite3D::update_shadow_instance() {
	const bool casts_now = get_cast_shadows_setting() != SHADOW_CASTING_SETTING_OFF;

	// Local helper: tear down the attached state (detach scenario) once, and remember it is detached so
	// repeated calls (e.g. every frame while not in a world, or while cast_shadow == Off) become no-ops.
	auto detach_shadow = [&]() {
		if (shadow_instance.is_valid() && shadow_instance_attached) {
			RS::get_singleton()->instance_set_scenario(shadow_instance, RID());
		}
		shadow_instance_attached = false;
		shadow_applied_scenario = RID();
	};

	// Never call get_world_3d() unless we are actually inside a world (see header note): doing so
	// spams the engine error and returns an invalid ref. When detached, just detach the instance's
	// scenario (no world access needed) and bail.
	if (!inside_world) {
		detach_shadow();
		return;
	}

	Ref<World3D> world = get_world_3d();
	RID scenario;
	if (world.is_valid()) scenario = world->get_scenario();

	if (casts_now && scenario.is_valid() && mesh.is_valid()) {
		// Fix (shadow #5): the animated fast path calls this every frame. Only re-issue the heavy RS
		// work (base bind, scenario, cast-setting, per-surface override loop) when something that drives
		// it actually changed since we last attached: a different mesh, a different scenario, a freshly
		// rewritten shadow-surface set (generation bump), or coming from a detached state. When nothing
		// changed, return immediately. The instance transform is NOT touched here on the steady path —
		// NOTIFICATION_TRANSFORM_CHANGED keeps it synced; we push the transform only on (re)attach below.
		const bool mesh_changed = (shadow_applied_mesh != mesh);
		const bool scenario_changed = (shadow_applied_scenario != scenario);
		const bool surfaces_changed = (shadow_applied_surface_generation != shadow_surface_generation);
		if (shadow_instance_attached && !mesh_changed && !scenario_changed && !surfaces_changed) {
			return;// nothing relevant changed: skip all RS calls this frame
		}

		const bool reattaching = !shadow_instance_attached || mesh_changed || scenario_changed;
		if (!shadow_instance.is_valid()) shadow_instance = RS::get_singleton()->instance_create();
		if (mesh_changed || !shadow_instance_attached) RS::get_singleton()->instance_set_base(shadow_instance, mesh);
		if (scenario_changed || !shadow_instance_attached) RS::get_singleton()->instance_set_scenario(shadow_instance, scenario);
		if (reattaching) {
			// Push the current transform only when (re)attaching; subsequent moves arrive via
			// NOTIFICATION_TRANSFORM_CHANGED. get_global_transform() does NOT require being in the world.
			RS::get_singleton()->instance_set_transform(shadow_instance, get_global_transform());
			// SHADOWS_ONLY: renders the mesh into shadow maps only (never the color pass). The enum's home
			// differs by build/version: Godot 4.7's module split it into RenderingServerEnums, while Godot
			// <= 4.6 and the GDExtension keep it on RenderingServer. SPINE_RS_ENUM resolves to the right one
			// (RenderingServerEnums on 4.7 module, RS otherwise) -- a bare RSE:: breaks the 4.6 module build.
			RS::get_singleton()->instance_geometry_set_cast_shadows_setting(shadow_instance, SPINE_RS_ENUM::SHADOW_CASTING_SETTING_SHADOWS_ONLY);
		}
		// Re-push the per-surface overrides when the set changed OR we just (re)attached (a fresh base
		// bind drops any previous overrides). Iterate the PERSISTED per-surface shadow materials
		// (build_meshes filled these). When this runs from ENTER_WORLD before the first build_meshes(),
		// shadow_surface_count is 0 and the loop is a no-op.
		if (surfaces_changed || reattaching) {
			for (int s = 0; s < shadow_surface_count; s++) {
				const RID &sm = shadow_surface_materials[s];
				// RID() override (custom-material surfaces) clears any stale override so the
				// surface falls back to the mesh's display material (which is shadows_disabled).
				RS::get_singleton()->instance_set_surface_override_material(shadow_instance, s, sm);
			}
			shadow_applied_surface_generation = shadow_surface_generation;
		}
		shadow_applied_mesh = mesh;
		shadow_applied_scenario = scenario;
		shadow_instance_attached = true;
	} else {
		// Not casting (cast_shadow == Off) or no valid scenario/mesh: detach so it stops rendering.
		// Do NOT free here; the instance is reused across rebuilds and freed in the destructor.
		detach_shadow();
	}
}

// ---------------------------------------------------------------------------
// Task 11: build_debug_mesh — rebuild the debug overlay.
//
// Two surfaces are appended to the primary mesh (after the render surfaces):
//   1. PRIMITIVE_TRIANGLES — bones (root + all bones) as filled kite/diamond
//      polygons whose width is controlled by debug_bones_thickness.
//   2. PRIMITIVE_LINES — all other categories (regions, meshes, bounding boxes,
//      paths, clipping).  3D lines are always 1px regardless of thickness.
//
// Both surfaces share the per-sprite debug_lines_material (unshaded,
// cull_disabled, depth_test_disabled, vertex-color-as-albedo).
//
// Coordinate conventions match build_meshes():
//   3D point = Vector3(spine_x * sx, spine_y * sy, z_eps)
//   sx = flip_h ? -pixel_size : pixel_size
//   sy = flip_v ?  pixel_size : -pixel_size
// ---------------------------------------------------------------------------
void SpineSprite3D::build_debug_mesh() {
	// The primary mesh is recreated from scratch by build_meshes() every frame.
	// We append the debug surfaces on that same mesh; cleanup is handled by build_meshes().

	// Early-out when nothing is enabled or skeleton not ready.
	bool any_enabled = debug_root || debug_bones || debug_regions || debug_meshes || debug_bounding_boxes || debug_paths || debug_clipping;
	if (!any_enabled) return;
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;

	spine::Skeleton *sk = skeleton->get_spine_object();
	auto &statics = SpineSprite3DStatics::instance();

	// Coordinate conventions matching build_meshes():
	float sx = flip_h ? -pixel_size : pixel_size;
	float sy = flip_v ? pixel_size : -pixel_size;
	// Small Z offset so debug geometry sits in front of the attachment quads.
	const float z_eps = 0.001f;

	// --- TRIANGLES surface (bones) ---
#ifdef SPINE_GODOT_EXTENSION
	PackedVector3Array tri_positions;
	PackedColorArray tri_colors;
	PackedInt32Array tri_indices;
#else
	Vector<Vector3> tri_positions;
	Vector<Color> tri_colors;
	Vector<int> tri_indices;
#endif

	// Helper: emit one bone as a filled kite in local bone space, transformed by the
	// bone's world matrix (a, b, c, d, worldX, worldY).
	//
	// Kite local points (matching SpineSprite::draw_bone / 2D reference):
	//   P0 = (-t,  0)
	//   P1 = ( 0,  t)
	//   P2 = (bone_length, 0)
	//   P3 = ( 0, -t)
	//
	// Transform local (lx, ly) to Spine world space:
	//   spine_x = a*lx + c*ly + worldX
	//   spine_y = b*lx + d*ly + worldY
	// Then to 3D: Vector3(spine_x * sx, spine_y * sy, z_eps)
	//
	// Emitted as 2 triangles: [P0,P1,P2] and [P0,P2,P3].
	// cull_disabled on the material handles either winding under flip.
	auto emit_bone_kite = [&](spine::Bone *bone, const Color &col) {
		if (!bone || !bone->isActive()) return;
		auto &bp = bone->getAppliedPose();
		float wx = bp.getWorldX(), wy = bp.getWorldY();
		// Match SpineSprite::draw_bone (the 2D reference): build the kite from the bone's world ROTATION
		// (getWorldRotationX) + world SCALE (getWorldScaleX/Y), NOT the full a/b/c/d matrix. The full
		// matrix additionally bakes in SHEAR and the reflection of flipped bones, which skews / mirrors
		// the kite for exactly those bones and makes it disagree with the 2D debug view (the "a few bones
		// look different" cases — e.g. the mirrored _left/_right aim poses). Rotation+scale keeps every
		// bone's kite identical to 2D. (The actual bone transform used by SpineBoneNode3D still uses the
		// full a/b/c/d matrix, which is correct for parenting attachments; this only affects the overlay.)
		float rot = spine::MathUtil::Deg_Rad * bp.getWorldRotationX();
		float cr = spine::MathUtil::cos(rot), sr = spine::MathUtil::sin(rot);
		float wsx = bp.getWorldScaleX(), wsy = bp.getWorldScaleY();
		float bone_length = bone->getData().getLength();
		float t = debug_bones_thickness;
		if (bone_length == 0) bone_length = t * 2.0f;

		// 4 local kite points
		float lx[4] = {-t, 0.0f, bone_length, 0.0f};
		float ly[4] = {0.0f, t, 0.0f, -t};

		int base = (int) tri_positions.size();
		for (int k = 0; k < 4; k++) {
			// Transform2D(rotation, scale) order: scale the local point, rotate, then translate by world.
			float px = wsx * lx[k];
			float py = wsy * ly[k];
			float sxp = wx + (cr * px - sr * py);
			float syp = wy + (sr * px + cr * py);
			tri_positions.push_back(Vector3(sxp * sx, syp * sy, z_eps));
			tri_colors.push_back(col);
		}
		// Triangle 0: P0, P1, P2
		tri_indices.push_back(base + 0);
		tri_indices.push_back(base + 1);
		tri_indices.push_back(base + 2);
		// Triangle 1: P0, P2, P3
		tri_indices.push_back(base + 0);
		tri_indices.push_back(base + 2);
		tri_indices.push_back(base + 3);
	};

	// --- Root bone ---
	if (debug_root) {
		emit_bone_kite(sk->getRootBone(), debug_root_color);
	}

	// --- All bones ---
	if (debug_bones) {
		auto &bones = sk->getBones();
		for (int i = 0; i < (int) bones.size(); i++) {
			emit_bone_kite(bones[i], debug_bones_color);
		}
	}

	// --- LINES surface (regions, meshes, bounding boxes, paths, clipping) ---
#ifdef SPINE_GODOT_EXTENSION
	PackedVector3Array dbg_positions;
	PackedColorArray dbg_colors;
#else
	Vector<Vector3> dbg_positions;
	Vector<Color> dbg_colors;
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
			emit_line(verts[i * 2], verts[i * 2 + 1], verts[(i + 1) * 2], verts[(i + 1) * 2 + 1], col);
		}
		// Close: last -> first
		emit_line(verts[(num_verts - 1) * 2], verts[(num_verts - 1) * 2 + 1], verts[0], verts[1], col);
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
							  scratch_world_verts.buffer()[(vi + 1) * 2], scratch_world_verts.buffer()[(vi + 1) * 2 + 1], debug_paths_color);
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

	// Nothing to render at all — skip surface creation.
	if (tri_indices.size() == 0 && dbg_positions.size() == 0) return;
	if (!mesh.is_valid()) return;

	// Lazily create the per-sprite debug material (shared by both surfaces).
	// Unshaded / cull_disabled / depth_test_disabled / vertex-color-as-albedo.
	// The same material works for both PRIMITIVE_TRIANGLES and PRIMITIVE_LINES.
	if (!debug_lines_material.is_valid()) {
		debug_lines_material.instantiate();
		debug_lines_material->set_shader(statics.get_lines_shader());
		debug_lines_material->set_render_priority(127);// draw on top among transparent surfaces
	}
	debug_lines_material->set_shader_parameter("billboard_mode", (int) billboard);
	RID dbg_mat = debug_lines_material->get_rid();

	// --- Append TRIANGLES surface (bones / root) ---
	if (tri_indices.size() > 0) {
		Array tri_arrays;
		tri_arrays.resize(Mesh::ARRAY_MAX);
		tri_arrays[Mesh::ARRAY_VERTEX] = tri_positions;
		tri_arrays[Mesh::ARRAY_COLOR] = tri_colors;
		tri_arrays[Mesh::ARRAY_INDEX] = tri_indices;

		RS::get_singleton()->mesh_add_surface_from_arrays(mesh, SPINE_RS_ENUM::PRIMITIVE_TRIANGLES, tri_arrays, Array(), Dictionary(),
														  SPINE_RS_ENUM::ARRAY_FLAG_USE_DYNAMIC_UPDATE);

		int sc = RS::get_singleton()->mesh_get_surface_count(mesh);
		if (sc > 0) {
			RS::get_singleton()->mesh_surface_set_material(mesh, sc - 1, dbg_mat);
		}
	}

	// --- Append LINES surface (regions/meshes/bbox/paths/clipping) ---
	if (dbg_positions.size() > 0) {
		Array line_arrays;
		line_arrays.resize(Mesh::ARRAY_MAX);
		line_arrays[Mesh::ARRAY_VERTEX] = dbg_positions;
		line_arrays[Mesh::ARRAY_COLOR] = dbg_colors;

		RS::get_singleton()->mesh_add_surface_from_arrays(mesh, SPINE_RS_ENUM::PRIMITIVE_LINES, line_arrays, Array(), Dictionary(),
														  SPINE_RS_ENUM::ARRAY_FLAG_USE_DYNAMIC_UPDATE);

		int sc = RS::get_singleton()->mesh_get_surface_count(mesh);
		if (sc > 0) {
			RS::get_singleton()->mesh_surface_set_material(mesh, sc - 1, dbg_mat);
		}
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
	set_process_internal(update_mode == SpineConstant::UpdateMode_Process || silhouette_mask_enabled);
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
	// layer_z_spacing is a shader uniform set only on a cache MISS in build_meshes(); cache HITS
	// would otherwise keep the old value. Re-apply it to every already-cached clone (mirrors
	// set_modulate). build_meshes() is still needed because the AABB depends on z_spacing (the raw
	// draw-order index is baked into local z and scaled by z_spacing for the world-space bounds).
	for (auto &entry : material_cache) {
		entry.value->set_shader_parameter("layer_z_spacing", z_spacing);
	}
	// Mirror onto the shadow clones (same uniform, set only on a cache MISS in build_meshes()).
	for (auto &entry : shadow_material_cache) {
		entry.value->set_shader_parameter("layer_z_spacing", z_spacing);
	}
	for (auto &entry : silhouette_material_cache) {
		entry.value->set_shader_parameter("layer_z_spacing", z_spacing);
	}
	// Mirror onto opted-in custom-material clones so their layering tracks z_spacing at runtime.
	for (auto &entry : custom_material_cache) {
		entry.value->set_shader_parameter("layer_z_spacing", z_spacing);
	}
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
	// Mirror onto the shadow clones so the cast silhouette billboards with the rendered card.
	for (auto &entry : shadow_material_cache) {
		entry.value->set_shader_parameter("billboard_mode", (int) billboard);
	}
	for (auto &entry : silhouette_material_cache) {
		entry.value->set_shader_parameter("billboard_mode", (int) billboard);
	}
	// Mirror onto opted-in custom-material clones so a billboarding custom shader tracks the mode.
	for (auto &entry : custom_material_cache) {
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

void SpineSprite3D::set_texture_filter(TextureFilter v) {
	if (texture_filter == v) return;
	texture_filter = v;
	// The sampler hint is baked into the shader variant, and the cache key encodes the
	// filter bits — clear the per-instance material cache (mirror set_shaded) so flush()
	// rebuilds materials with the new filter. Also clear the 3D-safe texture cache for parity.
	material_cache.clear();
	texture_3d_cache.clear();
	custom_material_cache.clear();
	custom_shader_uniform.clear();
	silhouette_material_cache.clear();
	silhouette_surface_materials.clear();
	silhouette_surface_count = 0;
	silhouette_surface_generation++;
	if (skeleton.is_valid()) build_meshes();
}

SpineSprite3D::TextureFilter SpineSprite3D::get_texture_filter() {
	return texture_filter;
}

void SpineSprite3D::set_shaded(bool v) {
	shaded = v;
	// Switching shaded mode invalidates the per-instance material cache since the
	// cache key encodes the shaded bit — clear so flush() picks the correct variants.
	material_cache.clear();
	texture_3d_cache.clear();
	custom_material_cache.clear();
	custom_shader_uniform.clear();
	silhouette_material_cache.clear();
	silhouette_surface_materials.clear();
	silhouette_surface_count = 0;
	silhouette_surface_generation++;
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_shaded() {
	return shaded;
}

void SpineSprite3D::set_alpha_cut(AlphaCutMode v) {
	if (alpha_cut == v) return;
	alpha_cut = v;
	// alpha_cut is a SHADER dimension (changes render_mode + fragment scissor) and is part of the
	// material cache key, so invalidate the per-instance clones and let build_meshes() rebuild.
	material_cache.clear();
	texture_3d_cache.clear();
	custom_material_cache.clear();
	custom_shader_uniform.clear();
	silhouette_material_cache.clear();
	silhouette_surface_materials.clear();
	silhouette_surface_count = 0;
	silhouette_surface_generation++;
	if (skeleton.is_valid()) build_meshes();
}

SpineSprite3D::AlphaCutMode SpineSprite3D::get_alpha_cut() {
	return alpha_cut;
}

void SpineSprite3D::set_alpha_scissor_threshold(float v) {
	alpha_scissor_threshold = v;
	// Uniform-only: re-apply to existing clones (no shader recompile needed).
	for (auto &entry : material_cache) {
		entry.value->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
	}
	// Mirror onto the shadow clones so the cast cutout matches the display silhouette.
	for (auto &entry : shadow_material_cache) {
		entry.value->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
	}
	for (auto &entry : silhouette_material_cache) {
		entry.value->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
	}
	// Mirror onto opted-in custom-material clones.
	for (auto &entry : custom_material_cache) {
		entry.value->set_shader_parameter("alpha_scissor_threshold", alpha_scissor_threshold);
	}
}

float SpineSprite3D::get_alpha_scissor_threshold() {
	return alpha_scissor_threshold;
}

void SpineSprite3D::set_depth_offset(float v) {
	if (depth_offset == v) return;
	depth_offset = v;
	// depth_offset is a pure shader UNIFORM (not part of the cache key or mesh geometry), so just
	// re-apply it to the existing clones — no cache clear, no mesh rebuild (mirrors set_modulate).
	for (auto &entry : material_cache) {
		entry.value->set_shader_parameter("depth_offset", depth_offset);
	}
	// Mirror onto the shadow clones so the cast silhouette gets the same per-part depth push.
	for (auto &entry : shadow_material_cache) {
		entry.value->set_shader_parameter("depth_offset", depth_offset);
	}
	for (auto &entry : silhouette_material_cache) {
		entry.value->set_shader_parameter("depth_offset", depth_offset);
	}
	// Mirror onto opted-in custom-material clones so their per-part depth push tracks depth_offset.
	for (auto &entry : custom_material_cache) {
		entry.value->set_shader_parameter("depth_offset", depth_offset);
	}
}

void SpineSprite3D::set_no_depth_test(bool v) {
	if (no_depth_test == v) return;
	no_depth_test = v;
	// SHADER dimension (appends depth_test_disabled to render_mode); part of the cache key.
	material_cache.clear();
	texture_3d_cache.clear();
	custom_material_cache.clear();
	custom_shader_uniform.clear();
	silhouette_material_cache.clear();
	silhouette_surface_materials.clear();
	silhouette_surface_count = 0;
	silhouette_surface_generation++;
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_no_depth_test() {
	return no_depth_test;
}

void SpineSprite3D::set_double_sided(bool v) {
	if (double_sided == v) return;
	double_sided = v;
	// SHADER dimension (cull_disabled vs cull_back); part of the cache key.
	material_cache.clear();
	texture_3d_cache.clear();
	custom_material_cache.clear();
	custom_shader_uniform.clear();
	silhouette_material_cache.clear();
	silhouette_surface_materials.clear();
	silhouette_surface_count = 0;
	silhouette_surface_generation++;
	if (skeleton.is_valid()) build_meshes();
}

bool SpineSprite3D::get_double_sided() {
	return double_sided;
}

void SpineSprite3D::set_fixed_size(bool v) {
	fixed_size = v;
	// Uniform-only (gated branch in the vertex shader): re-apply to existing clones.
	for (auto &entry : material_cache) {
		entry.value->set_shader_parameter("fixed_size_enabled", fixed_size);
	}
	// Mirror onto the shadow clones so the cast silhouette keeps the same fixed-size scaling.
	for (auto &entry : shadow_material_cache) {
		entry.value->set_shader_parameter("fixed_size_enabled", fixed_size);
	}
	for (auto &entry : silhouette_material_cache) {
		entry.value->set_shader_parameter("fixed_size_enabled", fixed_size);
	}
	// Mirror onto opted-in custom-material clones.
	for (auto &entry : custom_material_cache) {
		entry.value->set_shader_parameter("fixed_size_enabled", fixed_size);
	}
}

bool SpineSprite3D::get_fixed_size() {
	return fixed_size;
}

void SpineSprite3D::set_render_priority(int v) {
	render_priority = v;
	// Material::set_render_priority is a per-material property (not a shader recompile).
	for (auto &entry : material_cache) {
		entry.value->set_render_priority(render_priority);
	}
}

int SpineSprite3D::get_render_priority() {
	return render_priority;
}

void SpineSprite3D::set_modulate(const Color &v) {
	modulate = v;
	// Uniform-only: re-apply to existing clones.
	for (auto &entry : material_cache) {
		entry.value->set_shader_parameter("modulate_color", modulate);
	}
	// Mirror onto the shadow clones so fading via modulate.a also fades the cast shadow.
	for (auto &entry : shadow_material_cache) {
		entry.value->set_shader_parameter("modulate_color", modulate);
	}
	for (auto &entry : silhouette_material_cache) {
		entry.value->set_shader_parameter("modulate_color", modulate);
	}
	// Mirror onto opted-in custom-material clones so modulate tints/fades a custom material too.
	for (auto &entry : custom_material_cache) {
		entry.value->set_shader_parameter("modulate_color", modulate);
	}
}

Color SpineSprite3D::get_modulate() {
	return modulate;
}

void SpineSprite3D::set_silhouette_mask_enabled(bool v) {
	if (silhouette_mask_enabled == v) return;
	silhouette_mask_enabled = v;
	set_process_internal(update_mode == SpineConstant::UpdateMode_Process || silhouette_mask_enabled);
	update_silhouette_registration();
}

bool SpineSprite3D::get_silhouette_mask_enabled() {
	return silhouette_mask_enabled;
}

void SpineSprite3D::set_silhouette_mask_half_res(bool v) {
	silhouette_mask_half_res = v;// applied on the next SpineSilhouetteBuffer::sync()
}

bool SpineSprite3D::get_silhouette_mask_half_res() {
	return silhouette_mask_half_res;
}

// Register/unregister this node with the shared silhouette buffer as the enabled flag flips. Enabling
// takes a ref (which lazily creates the buffer) + allocates an id and triggers a mesh rebuild so the
// mask appears; disabling frees this node's mask instance, clears `spine_coverage` off its display
// clones, and releases the ref (which tears the buffer down when the last node disables).
void SpineSprite3D::update_silhouette_registration() {
	SpineSilhouetteBuffer &buf = SpineSilhouetteBuffer::instance();
	if (silhouette_mask_enabled && !silhouette_registered) {
		buf.add_ref();
		silhouette_registered = true;
		if (silhouette_id == 0) silhouette_id = buf.acquire_id();
		if (skeleton.is_valid()) build_meshes();
	} else if (!silhouette_mask_enabled && silhouette_registered) {
		if (silhouette_instance.is_valid()) {
			spine_free_rid(silhouette_instance);
			silhouette_instance = RID();
		}
		silhouette_material_cache.clear();
		silhouette_surface_materials.clear();
		silhouette_surface_count = 0;
		silhouette_instance_attached = false;
		silhouette_applied_mesh = RID();
		silhouette_applied_scenario = RID();
		silhouette_applied_surface_generation = 0;
		silhouette_surface_generation++;
		RS *rs = RS::get_singleton();
		for (auto &e : material_cache) rs->material_set_param(e.value->get_rid(), "spine_coverage", RID());
		for (auto &e : custom_material_cache) rs->material_set_param(e.value->get_rid(), "spine_coverage", RID());
		silhouette_registered = false;
		buf.release_ref();
	}
}

// Per-frame: sync the shared buffer's camera+size to the active camera, bind this node's mask
// instance into the buffer scenario, push per-surface mask materials (one atlas page per surface),
// hide any debug-only surfaces, and expose the buffer texture as `spine_coverage` on display materials.
void SpineSprite3D::update_silhouette_instance() {
	if (!silhouette_mask_enabled || !silhouette_registered || !is_inside_tree()) return;
	SpineSilhouetteBuffer &buf = SpineSilhouetteBuffer::instance();
	RS *rs = RS::get_singleton();

	auto detach_silhouette = [&]() {
		if (silhouette_instance.is_valid() && silhouette_instance_attached) {
			rs->instance_set_scenario(silhouette_instance, RID());
		}
		silhouette_instance_attached = false;
		silhouette_applied_scenario = RID();
		silhouette_applied_surface_generation = 0;
	};

	Viewport *vp = get_viewport();
	if (vp) {
		Size2 rect = vp->get_visible_rect().size;
		buf.sync(vp->get_camera_3d(), Size2i((int) rect.x, (int) rect.y), silhouette_mask_half_res, Engine::get_singleton()->get_process_frames());
	}

	RID scenario = buf.get_scenario();
	if (!scenario.is_valid() || !mesh.is_valid()) {
		detach_silhouette();
		return;
	}

	const bool mesh_changed = (silhouette_applied_mesh != mesh);
	const bool scenario_changed = (silhouette_applied_scenario != scenario);
	const bool surfaces_changed = (silhouette_applied_surface_generation != silhouette_surface_generation);
	const bool reattaching = !silhouette_instance_attached || mesh_changed || scenario_changed;

	if (!silhouette_instance.is_valid()) silhouette_instance = rs->instance_create();
	if (mesh_changed || !silhouette_instance_attached) rs->instance_set_base(silhouette_instance, mesh);
	if (scenario_changed || !silhouette_instance_attached) rs->instance_set_scenario(silhouette_instance, scenario);
	if (reattaching) {
		rs->instance_set_transform(silhouette_instance, get_global_transform());
		rs->instance_geometry_set_material_override(silhouette_instance, RID());
	}

	int mesh_surface_count = rs->mesh_get_surface_count(mesh);
	if (surfaces_changed || reattaching) {
		for (int s = 0; s < silhouette_surface_count && s < mesh_surface_count; s++) {
			rs->instance_set_surface_override_material(silhouette_instance, s, silhouette_surface_materials[s]);
		}
		silhouette_applied_surface_generation = silhouette_surface_generation;
	}
	if (mesh_surface_count > silhouette_surface_count) {
		RID discard = buf.get_discard_material()->get_rid();
		for (int s = silhouette_surface_count; s < mesh_surface_count; s++) {
			rs->instance_set_surface_override_material(silhouette_instance, s, discard);
		}
	}

	silhouette_applied_mesh = mesh;
	silhouette_applied_scenario = scenario;
	silhouette_instance_attached = true;

	RID cov = buf.get_texture();
	for (auto &e : material_cache) rs->material_set_param(e.value->get_rid(), "spine_coverage", cov);
	for (auto &e : custom_material_cache) rs->material_set_param(e.value->get_rid(), "spine_coverage", cov);
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
	// F9: Spine Screen blend is not supported by the auto-generated 3D shaders and
	// renders as Normal (blend_mix). This setter is exposed for parity with the 2D
	// SpineSprite; supply a custom screen_material to get correct Screen blending.
	screen_material = v;
	if (skeleton.is_valid()) build_meshes();
}

Ref<Material> SpineSprite3D::get_screen_material() {
	return screen_material;
}

void SpineSprite3D::clear_statics() {
	SpineSprite3DStatics::clear();
	SpineSilhouetteBuffer::shutdown();
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
	// set_animation() returns null when the animation no longer exists (e.g. preview_animation was
	// set, then the Spine source was changed/re-exported without it). Guard before use so a stale
	// preview name prints the "Can not find animation" warning instead of dereferencing null (crash).
	if (track_entry.is_valid()) {
		track_entry->set_mix_duration(0);
		if (frame) {
			track_entry->set_time_scale(0);
			track_entry->set_track_time(time);
		}
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
//
// F2: honor flip_h / flip_v exactly as build_meshes() does. build_meshes maps a Spine
// world point (X,Y) to the 3D point (X*sx, Y*sy, z) with sx = flip_h?-pixel_size:pixel_size
// and sy = flip_v?pixel_size:-pixel_size. Relative to the non-flipped baseline
// (sx0=+pixel_size, sy0=-pixel_size), enabling flip_h multiplies the final 3D X by -1 and
// enabling flip_v multiplies the final 3D Y by -1. That is a post-multiply diagonal
// D = diag(fx, fv, 1) applied to the whole (validated) transform — origin AND basis.
// With fx = fv = 1 this reduces EXACTLY to the previously validated non-flipped result.
//
// F4: column 2 (Z) is given a length consistent with the in-plane (X-axis) scale instead of
// a fixed unit, so a non-uniformly/degenerately scaled bone does not produce a skewed or
// degenerate basis for attached children.
// NOTE on scale convention: columns 0/1 carry the bone's Spine world scale in *Spine units*
// (a,b,c,d are not multiplied by pixel_size here), so a child of a SpineBoneNode3D inherits
// the bone's pixel-scale rather than pixel_size. This matches the human-validated demo; do
// not change it without re-validating the attached-node scale.
Transform3D SpineSprite3D::bone_to_transform3d(spine::Bone *bone, float slot_z) const {
	float a = bone->getAppliedPose().getA();
	float b = bone->getAppliedPose().getB();
	float c = bone->getAppliedPose().getC();
	float d = bone->getAppliedPose().getD();

	const float fx = flip_h ? -1.0f : 1.0f;
	const float fv = flip_v ? -1.0f : 1.0f;

	float wx = bone->getAppliedPose().getWorldX() * pixel_size * fx;
	float wy = -bone->getAppliedPose().getWorldY() * pixel_size * fv;

	// Map the bone's spine axes into 3D the SAME way build_meshes() maps mesh vertices and the debug
	// bone overlay maps its kite: spine (x,y) -> 3D (x*sx, y*sy) with sx = fx, sy = -fv. pixel_size lives
	// in the position (not the basis), so a child inherits the bone's pixel-scale, not pixel_size.
	// Spine's world matrix is COLUMN-major for the axes (localToWorld: worldX = a*x + b*y, worldY = c*x + d*y),
	// so the bone's axes are:
	//   X axis = image of (1,0) = spine (a,c) -> (a*fx, -c*fv)
	//   Y axis = image of (0,1) = spine (b,d) -> (b*fx, -d*fv)
	// This matches the 2D SpineBone::get_global_transform (local[0]=(a,c), local[1]=(b,d)) and the debug
	// kite, whose X axis is getWorldRotationX/getWorldScaleX = atan2(c,a)/sqrt(a*a+c*c) = spine (a,c).
	// (An earlier version used (a,b)/(c,d) — matrix ROWS — which is invisible for unrotated bones like the
	// root, but reflects the X axis of every rotated bone, so attachments drifted off rotated bones.)
	// Z is the cross product, so its sign naturally tracks the mesh's Y-flip reflection.
	Vector3 col0(a * fx, -c * fv, 0);
	Vector3 col1(b * fx, -d * fv, 0);
	float in_plane_scale = col0.length();
	if (in_plane_scale <= 0.0f) in_plane_scale = 1.0f;
	Vector3 col2 = col0.cross(col1);
	if (col2.length() <= 0.0f) col2 = Vector3(0, 0, in_plane_scale);
	else col2 = col2.normalized() * in_plane_scale;

	Basis basis;
	basis.set_column(0, col0);
	basis.set_column(1, col1);
	basis.set_column(2, col2);
	return Transform3D(basis, Vector3(wx, wy, slot_z));
}

// Design-time fallback: in the editor there is no "current" game Camera3D, but the body still billboards
// because its shader reads that viewport's INV_VIEW_MATRIX (the editor camera). Hand a Follow attachment the
// same editor camera so it tracks at design time instead of freezing in the flat card plane. Returns null
// in a running game / export template (there Engine::is_editor_hint() is false, or EditorInterface is absent).
static Camera3D *spine_get_editor_camera_3d() {
#if defined(SPINE_GODOT_EXTENSION) || defined(TOOLS_ENABLED)
	if (!Engine::get_singleton()->is_editor_hint()) return nullptr;
	EditorInterface *ei = EditorInterface::get_singleton();
	if (!ei) return nullptr;
	SubViewport *ev = ei->get_editor_viewport_3d(0);
	return ev ? ev->get_camera_3d() : nullptr;
#else
	return nullptr;
#endif
}

bool SpineSprite3D::get_billboard_basis(Basis &out) const {
	if (billboard == BILLBOARD_DISABLED || !is_inside_tree()) return false;
	Viewport *vp = get_viewport();
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	if (!cam) cam = spine_get_editor_camera_3d();
	if (!cam) return false;
	// INV_VIEW_MATRIX in the shader == the camera's global transform. Reproduce the same billboard basis
	// the vertex() builds (see build_shader_source): full billboard uses the camera basis directly; the
	// Y variant keeps world-up and rotates about it to face the camera.
	Basis cb = cam->get_global_transform().basis;
	Vector3 cam_x = cb.get_column(0), cam_y = cb.get_column(1), cam_z = cb.get_column(2);
	Basis b;
	if (billboard == BILLBOARD_ENABLED) {
		b.set_column(0, cam_x);
		b.set_column(1, cam_y);
		b.set_column(2, cam_z);
	} else {// BILLBOARD_Y
		Vector3 up(0, 1, 0);
		b.set_column(0, up.cross(cam_z).normalized());
		b.set_column(1, up);
		b.set_column(2, cam_x.cross(up).normalized());
	}
	out = b;
	return true;
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
	// F2: undo the same flip diagonal D = diag(fx, fv, 1) applied by bone_to_transform3d.
	// Since fx, fv are ±1, dividing by them is the same as multiplying by them.
	const float fx = flip_h ? -1.0f : 1.0f;
	const float fv = flip_v ? -1.0f : 1.0f;
	Vector3 col0 = local.basis.get_column(0);// (a*fx, -c*fv, ...) — spine X axis (a,c)
	Vector3 col1 = local.basis.get_column(1);// (b*fx, -d*fv, ...) — spine Y axis (b,d)

	auto &pose = bone->getAppliedPose();
	pose.setA(col0.x * fx); // a
	pose.setB(col1.x * fx); // b
	pose.setC(-col0.y * fv);// c
	pose.setD(-col1.y * fv);// d
	pose.setWorldX(origin.x * fx / pixel_size);
	pose.setWorldY(-origin.y * fv / pixel_size);
	pose.updateLocalTransform(*skeleton->get_spine_object());
	bone->getPose().set(pose);

	modified_bones = true;
}

void SpineSprite3D::pose_at(const String &animation_name, float time) {
	if (!skeleton.is_valid() || !skeleton->get_spine_object()) return;
	if (!animation_state.is_valid() || !animation_state->get_spine_object()) return;
	skeleton->set_to_setup_pose();
	if (animation_name.is_empty()) return;
	Ref<SpineTrackEntry> entry = animation_state->set_animation(animation_name, false, 0);
	if (entry.is_valid() && entry->get_spine_object()) {
		entry->set_mix_duration(0);
		entry->set_time_scale(0);
		entry->set_track_time(time);
	}
	animation_state->update(0);
	animation_state->apply(skeleton);
	skeleton->update_world_transform(SpineConstant::Physics_Update);
	if (is_visible_in_tree()) {
		build_meshes();
		build_debug_mesh();
	}
}

#endif// _3D_DISABLED
