// SpineSprite3D custom-material helper.
//
// #include this in a `shader_type spatial;` material assigned to a SpineSprite3D blend-mode slot
// (normal_material / additive_material / multiply_material / screen_material, or a per-slot
// SpineSlotNode3D material). It gives your custom material the SAME layout as the built-in one while
// you keep full control of the look:
//   - `spine_texture` is auto-bound by the runtime to the atlas — declare it YOURSELF with the
//     sampler filter you want (see the note above the uniforms; the filter cannot be propagated).
//   - the standard uniforms below are fed live by the runtime (billboard / z_spacing / depth_offset /
//     modulate / alpha scissor / fixed_size), matching the SpineSprite3D node's properties.
//   - spine_apply_vertex(...) reproduces the built-in vertex logic. Call it from your vertex(),
//     passing the vertex built-ins through (they cannot be read as globals inside a function, so they
//     are passed as parameters — copy the call below verbatim).
//   - spine_resolve_color(tex, COLOR, premultiplied) does the PMA-correct albedo/alpha + modulate
//     math from the built-in fragment. You still assign ALBEDO/ALPHA and — for the OPAQUE-pass cut
//     that lets fog/water/depth effects clip the sprite — write the ALPHA_SCISSOR_THRESHOLD built-in
//     yourself (it is a fragment built-in, so it cannot be set inside the helper).
//
// Your shader's render_mode MUST include `cull_disabled` — the spine mesh winding is reversed by the
// base Y-flip, so back-face culling makes the slot invisible. This include never sets render_mode.
//
// Why spine_apply_vertex matters: the mesh bakes the RAW draw-order index into VERTEX.z so z_spacing /
// depth_offset can be live uniforms. Without this logic a custom material renders every part one unit
// apart in depth (the "unified z spacing" spread). It applies layer_z_spacing and the per-part
// depth_offset push exactly like the built-in shader.
//
// Minimal usage (OPAQUE cut, matches the SpineSprite3D default alpha_cut = Discard):
//   shader_type spatial;
//   render_mode unshaded, cull_disabled, depth_draw_opaque, shadows_disabled;
//   #include "res://addons/spine_godot/spine_sprite_3d.gdshaderinc"
//   uniform sampler2D spine_texture : source_color, filter_linear; // your choice of filter
//   void vertex() {
//       spine_apply_vertex(VIEW_MATRIX, INV_VIEW_MATRIX, MODEL_MATRIX, PROJECTION_MATRIX,
//                          MODELVIEW_MATRIX, MODELVIEW_NORMAL_MATRIX, VERTEX, POSITION);
//   }
//   void fragment() {
//       vec4 c = spine_resolve_color(texture(spine_texture, UV), COLOR, true);
//       ALBEDO = c.rgb;
//       ALPHA = c.a;
//       ALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold; // opaque pass -> fog/water/depth cut
//   }
// For a plain alpha-blended look instead, drop the ALPHA_SCISSOR_THRESHOLD line and add `blend_mix`
// to render_mode (transparent pass, no opaque-buffer cut).

// NOTE: declare `spine_texture` YOURSELF in your shader (this include intentionally does not), so you
// choose its sampler FILTER — the runtime binds the atlas to it by name either way. The filter is a
// compile-time sampler qualifier, NOT a runtime uniform, so it cannot be fed from the node's
// texture_filter property like the uniforms below. Pick the hint matching the node's Texture Filter:
//   Nearest        -> filter_nearest          Linear         -> filter_linear (default)
//   Nearest Mipmap -> filter_nearest_mipmap   Linear Mipmap  -> filter_linear_mipmap
// e.g.  uniform sampler2D spine_texture : source_color, filter_nearest;
uniform int billboard_mode = 0;// 0 disabled, 1 billboard, 2 y-billboard
uniform bool fixed_size_enabled = false;
uniform vec4 modulate_color = vec4(1.0);
uniform float layer_z_spacing = 0.0;
uniform float depth_offset = 0.0;
uniform float alpha_scissor_threshold = 0.5;

// Resolves a spine atlas sample to the final (rgb = ALBEDO, a = ALPHA), applying the vertex/slot
// COLOR and the mirrored modulate_color exactly like the built-in SpineSprite3D fragment(). Pass
// `premultiplied = true` for the default premultiplied-alpha spine atlas: it scales ALBEDO by
// modulate_color.a as well, preserving the premultiplied invariant so the sprite does not develop
// dark edge fringing as it fades via modulate. Pass false only for a straight-alpha atlas.
vec4 spine_resolve_color(vec4 tex, vec4 color, bool premultiplied) {
	vec3 albedo = tex.rgb * color.rgb * modulate_color.rgb;
	if (premultiplied) {
		albedo *= modulate_color.a;
	}
	float alpha = tex.a * color.a * modulate_color.a;
	return vec4(albedo, alpha);
}

// Optional whole-character silhouette coverage buffer. Set by the runtime ONLY when the node's
// silhouette_mask_enabled is on; otherwise it samples as 0 (no cost, no rim). Each covered pixel holds
// the character's id in .r (background = 0), rendered as the union of ALL the character's pieces from
// the active camera, so the silhouette is the whole character — not per-attachment. Robust to whatever
// is behind the character (it is the character's OWN coverage, not scene depth). filter_nearest keeps
// ids crisp for the equality compare.
uniform sampler2D spine_coverage : source_color, filter_nearest;

// Returns 1.0 when this fragment is within ~`width_px` screen pixels of the WHOLE-character silhouette
// edge (a neighbor whose id differs from this fragment's — background, or a different character), else
// 0.0. Call from fragment() with SCREEN_UV. Example rim:
//   float rim = spine_silhouette_edge(SCREEN_UV, 2.0);
//   ALBEDO = mix(ALBEDO, rim_color, rim);
// Do your own light-direction weighting / falloff on top for a directional rim.
float spine_silhouette_edge(vec2 screen_uv, float width_px) {
	float me = texture(spine_coverage, screen_uv).r;
	vec2 texel = width_px / vec2(textureSize(spine_coverage, 0));
	float e = 0.0;
	e = max(e, abs(texture(spine_coverage, screen_uv + vec2(texel.x, 0.0)).r - me));
	e = max(e, abs(texture(spine_coverage, screen_uv - vec2(texel.x, 0.0)).r - me));
	e = max(e, abs(texture(spine_coverage, screen_uv + vec2(0.0, texel.y)).r - me));
	e = max(e, abs(texture(spine_coverage, screen_uv - vec2(0.0, texel.y)).r - me));
	return step(0.0005, e);
}

// Applies billboard orientation, fixed_size scaling, the z_spacing layer collapse, and the per-part
// view-space depth_offset push — identical to the built-in SpineSprite3D vertex(). The vertex-stage
// built-ins are passed in because Godot does not expose them as globals inside a function.
void spine_apply_vertex(mat4 view, mat4 inv_view, mat4 model, mat4 proj,
		inout mat4 modelview, inout mat3 modelview_normal, inout vec3 vertex, inout vec4 position) {
	if (billboard_mode == 1) {
		modelview = view * mat4(inv_view[0], inv_view[1], inv_view[2], model[3]);
		modelview_normal = mat3(modelview);
	} else if (billboard_mode == 2) {
		modelview = view * mat4(
			vec4(normalize(cross(vec3(0.0, 1.0, 0.0), inv_view[2].xyz)), 0.0),
			vec4(0.0, 1.0, 0.0, 0.0),
			vec4(normalize(cross(inv_view[0].xyz, vec3(0.0, 1.0, 0.0))), 0.0),
			model[3]);
		modelview_normal = mat3(modelview);
	}
	if (fixed_size_enabled) {
		if (proj[3][3] != 0.0) {
			float sc = abs(1.0 / (2.0 * proj[1][1])) * 2.0;
			modelview[0] *= sc;
			modelview[1] *= sc;
			modelview[2] *= sc;
		} else {
			float sc = -modelview[3].z;
			modelview[0] *= sc;
			modelview[1] *= sc;
			modelview[2] *= sc;
		}
	}
	float draw_index = vertex.z;// signed draw-order index -i baked into local z
	vertex.z *= layer_z_spacing;// world-space layer spacing (collapses to 0 at z_spacing 0)
	if (depth_offset != 0.0) {
		vec4 vpos = modelview * vec4(vertex, 1.0);
		vpos.z -= draw_index * depth_offset;
		position = proj * vpos;
	}
}
