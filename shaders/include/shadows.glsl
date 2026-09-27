// Shadow lookups against the directional, omni and spot shadow maps the
// scene pass binds in set 0. Every function returns 1.0 for fully lit and
// 0.0 for fully shadowed, and only the caster light index is ever
// occluded: every other light returns 1.0.
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

// Omni (point-light) shadow data, also owned by the scene pass: the six
// face view-projections the point shadow pass rendered with, the
// caster's world position (w = the faces' near plane) and params:
// x enabled, y bias, z caster point-light index, w the faces' far plane.
// The depth cube map follows: one face per +-X / +-Y / +-Z, each a
// 90-degree perspective from the light, so a lookup direction selects
// the face the hardware way and the receiver's depth in that face is
// reconstructed from the near / far planes (point_face_depth) rather
// than through the matrix.
layout(set = 0, binding = BINDING_POINT_SHADOW, std140) uniform PointShadow
{
    mat4 faceViewProj[6];
    vec4 lightPos;
    vec4 params;
} u_point_shadow;

layout(set = 0, binding = BINDING_POINT_SHADOW_MAP) uniform samplerCube pointShadowMap;

// Spot-light shadow data, also owned by the scene pass: a single
// perspective map for the first shadow-casting spot light. params:
// x enabled, y bias, z caster spot-light index.
layout(set = 0, binding = BINDING_SPOT_SHADOW, std140) uniform SpotShadow
{
    mat4 lightViewProj;
    vec4 params;
} u_spot_shadow;

layout(set = 0, binding = BINDING_SPOT_SHADOW_MAP) uniform sampler2D spotShadowMap;

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

// The window-space depth a face of the omni cube stores for a point at
// distance z along that face's axis: the 90-degree perspective with the
// pass's near / far planes, through the GL-convention projection both
// backends share (0.5 * z_ndc + 0.5, see depth_utils.glsl). Every face
// has the same near and far, so the depth depends only on the distance
// along the face axis, never on which face or where within it.
float point_face_depth(float z)
{
    float n = u_point_shadow.lightPos.w;
    float f = u_point_shadow.params.w;
    float zNdc = (f + n) / (f - n) - (2.0 * f * n) / ((f - n) * max(z, n));
    return zNdc * 0.5 + 0.5;
}

// The receiver's depth in the face a lookup direction lands on. The
// cube map picks the face by the major axis of the direction, so the
// receiver's coordinate along that axis is what the face compared
// against; a kernel tap that crosses a face edge is then compared in
// the neighbour's terms rather than mismatched.
float point_receiver_depth(vec3 toFrag, vec3 dir)
{
    vec3 a = abs(dir);
    float z;
    if (a.x >= a.y && a.x >= a.z)
    {
        z = abs(toFrag.x);
    }
    else if (a.y >= a.z)
    {
        z = abs(toFrag.y);
    }
    else
    {
        z = abs(toFrag.z);
    }
    return point_face_depth(z);
}

// Omni shadow term for the fragment at worldPosition, lit by point light
// lightIndex along L with shading normal N. Samples the depth cube along
// (fragment - light) with a 3x3 PCF kernel stepped one face texel at a
// time across the face the fragment lands on, and compares each tap
// against the receiver depth reconstructed for the face the tap hits.
float point_shadow(vec3 worldPosition, int lightIndex, vec3 N, vec3 L)
{
    if (u_point_shadow.params.x == 0.0 || lightIndex != int(u_point_shadow.params.z))
    {
        return 1.0;
    }
    vec3 toFrag = worldPosition - u_point_shadow.lightPos.xyz;
    vec3 a = abs(toFrag);
    float major = max(a.x, max(a.y, a.z));
    // Past the far plane no face holds the receiver: unshadowed, as
    // the clipped projection was before.
    if (major >= u_point_shadow.params.w)
    {
        return 1.0;
    }

    // The two axes spanning the face the fragment lands on. A face
    // texel spans 2 * major / size world units at the fragment's
    // distance, so the taps step by that much along them.
    vec3 u;
    vec3 v;
    if (a.x >= a.y && a.x >= a.z)
    {
        u = vec3(0.0, 1.0, 0.0);
        v = vec3(0.0, 0.0, 1.0);
    }
    else if (a.y >= a.z)
    {
        u = vec3(1.0, 0.0, 0.0);
        v = vec3(0.0, 0.0, 1.0);
    }
    else
    {
        u = vec3(1.0, 0.0, 0.0);
        v = vec3(0.0, 1.0, 0.0);
    }
    float texel = 2.0 * major / float(textureSize(pointShadowMap, 0).x);

    float bias = max(u_point_shadow.params.y * (1.0 - dot(N, L)), u_point_shadow.params.y * 0.1);
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            vec3 dir = toFrag + (float(x) * u + float(y) * v) * texel;
            float closest = texture(pointShadowMap, dir).r;
            float receiver = point_receiver_depth(toFrag, dir);
            lit += (receiver - bias > closest) ? 0.0 : 1.0;
        }
    }
    return lit / 9.0;
}

// Spot shadow term for the fragment at worldPosition, lit by spot light
// lightIndex along L with shading normal N. The perspective projection
// divides the same way an orthographic one does, so this is the
// directional path's 5x5 PCF kernel against the spot's own map.
float spot_shadow(vec3 worldPosition, int lightIndex, vec3 N, vec3 L)
{
    if (u_spot_shadow.params.x == 0.0 || lightIndex != int(u_spot_shadow.params.z))
    {
        return 1.0;
    }
    vec4 lightClip = u_spot_shadow.lightViewProj * vec4(worldPosition, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
    {
        return 1.0;
    }
    float bias = max(u_spot_shadow.params.y * (1.0 - dot(N, L)), u_spot_shadow.params.y * 0.1);
    vec2 texelSize = 1.0 / vec2(textureSize(spotShadowMap, 0));
    float lit = 0.0;
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            float closest = texture(spotShadowMap, proj.xy + vec2(x, y) * texelSize).r;
            lit += (proj.z - bias > closest) ? 0.0 : 1.0;
        }
    }
    return lit / 25.0;
}

#endif // AE_SHADOWS_GLSL
