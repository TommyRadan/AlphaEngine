// Shadow lookups against the directional and omni shadow maps the scene
// pass binds in set 0. Both functions return 1.0 for fully lit and 0.0
// for fully shadowed, and only the caster light index is ever occluded:
// every other light returns 1.0.
#ifndef AE_SHADOWS_GLSL
#define AE_SHADOWS_GLSL

#include "include/bindings.glsl"

// Directional shadow data, owned by the scene pass alongside the
// lights block. params: x enabled, y bias, z caster light index.
layout(set = 0, binding = BINDING_SHADOW, std140) uniform Shadow
{
    mat4 lightViewProj;
    vec4 params;
} u_shadow;

layout(set = 0, binding = BINDING_SHADOW_MAP) uniform sampler2D shadowMap;

// Omni (point-light) shadow data, also owned by the scene pass. The six
// face view-projections, the caster's world position, and
// params: x enabled, y bias, z caster point-light index. The six face
// depth maps follow, selected by the major axis of (fragment - light).
layout(set = 0, binding = BINDING_POINT_SHADOW, std140) uniform PointShadow
{
    mat4 faceViewProj[6];
    vec4 lightPos;
    vec4 params;
} u_point_shadow;

layout(set = 0, binding = BINDING_POINT_SHADOW_MAP_0) uniform sampler2D pointShadowMap0;
layout(set = 0, binding = BINDING_POINT_SHADOW_MAP_1) uniform sampler2D pointShadowMap1;
layout(set = 0, binding = BINDING_POINT_SHADOW_MAP_2) uniform sampler2D pointShadowMap2;
layout(set = 0, binding = BINDING_POINT_SHADOW_MAP_3) uniform sampler2D pointShadowMap3;
layout(set = 0, binding = BINDING_POINT_SHADOW_MAP_4) uniform sampler2D pointShadowMap4;
layout(set = 0, binding = BINDING_POINT_SHADOW_MAP_5) uniform sampler2D pointShadowMap5;

// Directional shadow term for the fragment at worldPosition, lit by
// directional light lightIndex along L with shading normal N. A 5x5 PCF
// kernel softens the shadow edge: a wider kernel keeps the penumbra
// smooth even where the light-space texel-to-world ratio is coarse. The
// shadow pass auto-fits the light box to the view frustum; a
// resolution-independent filter (PCSS) and cascades remain a follow-on.
float directional_shadow(vec3 worldPosition, int lightIndex, vec3 N, vec3 L)
{
    if (u_shadow.params.x == 0.0 || lightIndex != int(u_shadow.params.z))
    {
        return 1.0;
    }
    vec4 lightClip = u_shadow.lightViewProj * vec4(worldPosition, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
    {
        return 1.0;
    }
    float bias = max(u_shadow.params.y * (1.0 - dot(N, L)), u_shadow.params.y * 0.1);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = 0.0;
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            float closest = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
            lit += (proj.z - bias > closest) ? 0.0 : 1.0;
        }
    }
    return lit / 25.0;
}

// Dynamic indexing of a sampler array by a non-uniform expression is
// disallowed, so pick the face's 2D map with a branch.
float sample_point_face(int face, vec2 uv)
{
    if (face == 0) return texture(pointShadowMap0, uv).r;
    if (face == 1) return texture(pointShadowMap1, uv).r;
    if (face == 2) return texture(pointShadowMap2, uv).r;
    if (face == 3) return texture(pointShadowMap3, uv).r;
    if (face == 4) return texture(pointShadowMap4, uv).r;
    return texture(pointShadowMap5, uv).r;
}

// Omni shadow term for the fragment at worldPosition, lit by point light
// lightIndex along L with shading normal N. Selects the cube face by the
// major axis of (fragment - light), then projects with that face's
// view-projection and does 3x3 PCF.
float point_shadow(vec3 worldPosition, int lightIndex, vec3 N, vec3 L)
{
    if (u_point_shadow.params.x == 0.0 || lightIndex != int(u_point_shadow.params.z))
    {
        return 1.0;
    }
    vec3 toFrag = worldPosition - u_point_shadow.lightPos.xyz;
    vec3 a = abs(toFrag);
    int face;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = toFrag.x > 0.0 ? 0 : 1;
    }
    else if (a.y >= a.z)
    {
        face = toFrag.y > 0.0 ? 2 : 3;
    }
    else
    {
        face = toFrag.z > 0.0 ? 4 : 5;
    }

    vec4 clip = u_point_shadow.faceViewProj[face] * vec4(worldPosition, 1.0);
    vec3 proj = clip.xyz / clip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
    {
        return 1.0;
    }
    float bias = max(u_point_shadow.params.y * (1.0 - dot(N, L)), u_point_shadow.params.y * 0.1);
    vec2 texelSize = 1.0 / vec2(textureSize(pointShadowMap0, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float closest = sample_point_face(face, proj.xy + vec2(x, y) * texelSize);
            lit += (proj.z - bias > closest) ? 0.0 : 1.0;
        }
    }
    return lit / 9.0;
}

#endif // AE_SHADOWS_GLSL
