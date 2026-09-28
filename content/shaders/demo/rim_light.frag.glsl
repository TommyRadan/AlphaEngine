// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// The rim-light material of the custom-material showcase: an unlit base
// colour, optionally modulated by the albedo map (whose uv scrolls with
// the clock under the template's own ANIMATED_UV keyword), with a
// Fresnel-style glow toward the silhouette. The parameter block mirrors
// the parameters the showcase declares for the template.

#include "include/per_frame.glsl"
#include "include/per_material.glsl"
#include "include/fog.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

// std140: vec4 base_color at 0, vec4 rim_color at 16, float rim_power at
// 32, float rim_strength at 36, vec2 uv_scroll at 40; 48 bytes.
layout(set = PER_MATERIAL_SET, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 baseColor;
    vec4 rimColor;
    float rimPower;
    float rimStrength;
    vec2 uvScroll;
} u_material;

#ifdef USE_ALBEDO_MAP
layout(set = PER_MATERIAL_SET, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D albedoMap;
#endif

void main()
{
    vec3 N = normalize(worldNormal);
    vec3 V = normalize(u_frame.cameraPosition.xyz - worldPosition);

    vec4 base = u_material.baseColor;
#ifdef USE_ALBEDO_MAP
    vec2 uv = texCoord;
#ifdef ANIMATED_UV
    uv += u_frame.time.x * u_material.uvScroll;
#endif
    base *= texture(albedoMap, uv);
#endif

    // A soft wrap toward the sky (world up is +Z), so the unlit surface
    // still reads as round.
    vec3 color = base.rgb * (0.6 + 0.4 * N.z);

    float facing = clamp(dot(N, V), 0.0, 1.0);
    float rim = pow(1.0 - facing, max(u_material.rimPower, 0.001)) * u_material.rimStrength;
    color += u_material.rimColor.rgb * rim;

#ifndef NO_FOG
    color = apply_fog(color, worldPosition, u_frame.cameraPosition.xyz);
#endif
    // A translucent instance keeps its rim solid while its body fades.
    fragColor = vec4(color, clamp(base.a + rim * u_material.rimColor.a, 0.0, 1.0));
}
