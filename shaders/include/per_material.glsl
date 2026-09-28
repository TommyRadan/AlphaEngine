// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

// The engine's binding model for material shaders — the built-in ones and
// the asset shaders a game's material templates name (see
// rendering_engine/materials/material_template.hpp) — and the set numbers
// it gives each part:
//
//   set 0 (PER_FRAME_SET)     the per-frame set of the pass the template
//                             draws in: for a scene template the view
//                             (include/per_frame.glsl, view_globals), the
//                             lights (include/lights.glsl) and the shadows
//                             (include/shadows.glsl);
//   push constants            the per-draw block, model and normal matrix
//                             (include/per_draw.glsl);
//   set 1 (PER_DRAW_SET)      what a draw binds beyond the push constants:
//                             a SKINNED variant's joint palette;
//   set 2 (PER_MATERIAL_SET)  the per-material set each instance owns: the
//                             std140 parameter block at
//                             BINDING_MATERIAL_PARAMS, laid out as the
//                             template declares its parameters, and the
//                             sampled maps at the BINDING_MATERIAL_*_MAP
//                             numbers its texture slots name.
//
// A material shader includes the headers of the parts it reads and
// declares its own block and samplers in the per-material set:
//
//   layout(set = PER_MATERIAL_SET, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
//   {
//       vec4 baseColor;
//   } u_material;
//   layout(set = PER_MATERIAL_SET, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D albedoMap;
//
// The template's keywords (USE_ALBEDO_MAP while its slot is bound, WIREFRAME,
// NO_FOG, its own) arrive as #defines, so the shader tests them with #ifdef.
#ifndef AE_PER_MATERIAL_GLSL
#define AE_PER_MATERIAL_GLSL

#include "include/bindings.glsl"

#define PER_FRAME_SET 0
#define PER_DRAW_SET 1
#define PER_MATERIAL_SET 2

#endif // AE_PER_MATERIAL_GLSL
