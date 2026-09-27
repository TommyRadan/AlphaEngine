#version 450

// Volumetric fog, stage 1 of 3 (volumetric_fog_pass): the raymarch, drawn
// over the shared fullscreen triangle into a half-resolution rgba16f
// target. Each texel stands for one full-resolution scene pixel (the
// first of its 2x2 block, whose depth the upsample reads back): it
// reconstructs that pixel's world position from the scene depth through
// the jittered inverse view-projection the depth was rasterised with,
// then marches the medium from the camera toward it, stopping at the
// max distance, in a fixed number of steps. It writes the light
// scattered toward the camera along the ray in rgb and the ray's
// transmittance in a; the composite blends scene * a + rgb.
//
// The medium is the scene's exponential height fog (density
// u_frame.fogParams.w at reference height heightFogParams.y, falling off
// by heightFogParams.x per world unit of +Z), times the volumetric
// density scale, with a white albedo: every point scatters as much as it
// extinguishes. The light a step scatters toward the camera sums
//   - the ambient term, isotropic, so the phase integrates out to 1;
//   - every directional light through a Henyey-Greenstein phase, shadowed
//     by one hardware-compared tap of the cascade covering the step, so
//     the caster's occluders cut light shafts out of the medium;
//   - the first few point and spot lights, with the lit materials'
//     range / cone attenuation and one tap of their shadow maps.
// Each step integrates that light over its segment in closed form
// (Hillaire 2015, "Physically Based and Unified Volumetric Rendering in
// Frostbite"), so thick fog stays energy conserving at a coarse step.
//
// The march start is offset by an interleaved-gradient-noise fraction of
// a step, per texel and, while temporal AA runs, per frame: the banding
// a low step count produces becomes fine noise the TAA resolve averages.

#include "include/bindings.glsl"
#include "include/constants.glsl"
#include "include/depth_utils.glsl"
#include "include/lights.glsl"
#include "include/per_frame.glsl"
#include "include/shadows.glsl"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

// std140, 96 bytes, mirroring volumetric_fog_pass's params block (shared
// with the upsample stage, which declares the same layout):
//     0   vec4 medium             x density scale, y anisotropy g,
//                                 z max distance, w intensity
//    16   vec4 march              x step count, y noise frame offset
//                                 (z, w unused)
//    32   mat4 inverseProjection  the camera's unjittered inverse
//                                 projection (the upsample's depth
//                                 linearisation; unused here)
layout(set = 1, binding = BINDING_VOLUMETRIC_FOG_PARAMS, std140) uniform VolumetricFog
{
    vec4 medium;
    vec4 march;
    mat4 inverseProjection;
} u_fog;

layout(set = 1, binding = BINDING_VOLUMETRIC_FOG_DEPTH) uniform sampler2D sceneDepth;

// Bounds that keep a pixel's cost fixed however many lights the scene
// has: only the first few point and spot lights scatter into the fog.
// Every directional light does (the block holds at most four).
const int FOG_MAX_POINT_LIGHTS = 4;
const int FOG_MAX_SPOT_LIGHTS = 4;
const int FOG_MAX_STEPS = 128;

// Interleaved gradient noise (Jimenez 2014) in [0, 1): neighbouring
// pixels differ strongly, so a step offset drawn from it dithers the
// banding into a fine pattern. The frame offset shifts the pattern so
// consecutive frames land on different offsets for TAA to average.
float interleaved_gradient_noise(vec2 pixel, float frame)
{
    pixel += 5.588238 * frame;
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Henyey-Greenstein phase function, normalised over the sphere, for the
// cosine of the angle the light turns through to reach the camera
// (dot(L, rayDir), L toward the light, rayDir away from the camera).
float henyey_greenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

// The medium's extinction coefficient at p: the height fog's density
// profile times the volumetric density scale. The exponent is capped so
// a point far below the reference height stays finite (it is opaque
// either way).
float fog_density(vec3 p)
{
    float heightDensity = u_frame.fogParams.w;
    float falloff = u_frame.heightFogParams.x;
    float referenceHeight = u_frame.heightFogParams.y;
    return u_fog.medium.x * heightDensity * exp(min(-falloff * (p.z - referenceHeight), 60.0));
}

// The directional caster's shadow at a point in the medium: one
// hardware-compared (bilinear PCF) tap of the cascade covering it. There
// is no surface, so no slope-scaled receiver bias; the cascade's constant
// bias alone keeps a point on an occluder from shadowing itself. The
// march jitter and the TAA resolve stand in for a wider kernel.
float volume_directional_shadow(vec3 p, int lightIndex)
{
    if (u_shadow.params.x == 0.0 || lightIndex != int(u_shadow.params.z))
    {
        return 1.0;
    }
    int count = clamp(int(u_shadow.params.y), 1, SHADOW_MAX_CASCADES);
    float viewDepth = -(u_frame.viewMatrix * vec4(p, 1.0)).z;
    int cascade = count - 1;
    for (int i = 0; i < count - 1; ++i)
    {
        if (viewDepth <= u_shadow.splitDepths[i])
        {
            cascade = i;
            break;
        }
    }
    vec4 lightClip = u_shadow.lightViewProj[cascade] * vec4(p, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
    {
        return 1.0;
    }
    float reference = proj.z - u_shadow.cascadeBias[cascade];
    return texture(shadowMap, vec4(proj.xy, float(cascade), reference));
}

// The omni caster's shadow at a point in the medium: one tap of the
// depth cube along (p - light), compared against the receiver depth of
// the face that direction selects.
float volume_point_shadow(vec3 p, int lightIndex)
{
    if (u_point_shadow.params.x == 0.0 || lightIndex != int(u_point_shadow.params.z))
    {
        return 1.0;
    }
    vec3 toPoint = p - u_point_shadow.lightPos.xyz;
    vec3 a = abs(toPoint);
    if (max(a.x, max(a.y, a.z)) >= u_point_shadow.params.w)
    {
        return 1.0;
    }
    float closest = texture(pointShadowMap, toPoint).r;
    float receiver = point_receiver_depth(toPoint, toPoint);
    return receiver - u_point_shadow.params.y > closest ? 0.0 : 1.0;
}

// The spot caster's shadow at a point in the medium: one tap of its
// perspective map. Only reached for points inside the cone (the caller
// skips zero cone attenuation first), so the point is in front of the
// light and the divide is safe.
float volume_spot_shadow(vec3 p, int lightIndex)
{
    if (u_spot_shadow.params.x == 0.0 || lightIndex != int(u_spot_shadow.params.z))
    {
        return 1.0;
    }
    vec4 lightClip = u_spot_shadow.lightViewProj * vec4(p, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
    {
        return 1.0;
    }
    float closest = texture(spotShadowMap, proj.xy).r;
    return proj.z - u_spot_shadow.params.y > closest ? 0.0 : 1.0;
}

// Light scattered toward the camera per unit of scattering coefficient
// at p, for a ray travelling along rayDir.
vec3 in_scattered_light(vec3 p, vec3 rayDir, float g)
{
    vec3 light = u_lights.ambient.rgb;

    for (int i = 0; i < u_lights.counts.x; ++i)
    {
        vec3 L = normalize(-u_lights.directional[i].direction.xyz);
        float phase = henyey_greenstein(dot(L, rayDir), g);
        light += u_lights.directional[i].color.rgb * phase * volume_directional_shadow(p, i);
    }

    int pointCount = min(u_lights.counts.y, FOG_MAX_POINT_LIGHTS);
    for (int i = 0; i < pointCount; ++i)
    {
        vec3 toLight = u_lights.point[i].position.xyz - p;
        float dist = length(toLight);
        vec4 attenuation = u_lights.point[i].attenuation;
        if (attenuation.x > 0.0 && dist > attenuation.x)
        {
            continue;
        }
        vec3 L = toLight / max(dist, 0.0001);
        float atten = 1.0 / (attenuation.y + attenuation.z * dist + attenuation.w * dist * dist);
        float phase = henyey_greenstein(dot(L, rayDir), g);
        light += u_lights.point[i].color.rgb * atten * phase * volume_point_shadow(p, i);
    }

    int spotCount = min(u_lights.counts.z, FOG_MAX_SPOT_LIGHTS);
    for (int i = 0; i < spotCount; ++i)
    {
        vec3 toLight = u_lights.spot[i].position.xyz - p;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 0.0001);
        float atten = spot_attenuation(i, L, dist);
        if (atten <= 0.0)
        {
            continue;
        }
        float phase = henyey_greenstein(dot(L, rayDir), g);
        light += u_lights.spot[i].color.rgb * atten * phase * volume_spot_shadow(p, i);
    }

    return light;
}

void main()
{
    // The full-resolution pixel this texel stands for. The target is
    // ceil(size / 2) wide and high, so texel i covers pixels 2i and 2i + 1
    // and marches from pixel 2i.
    ivec2 fullSize = textureSize(sceneDepth, 0);
    ivec2 halfSize = (fullSize + 1) / 2;
    ivec2 halfPixel = min(ivec2(texCoord * vec2(halfSize)), halfSize - 1);
    ivec2 fullPixel = min(halfPixel * 2, fullSize - 1);
    float depth = texelFetch(sceneDepth, fullPixel, 0).r;

    // Its world position: the surface under it, or the far plane where
    // nothing was drawn (the scene pass clears depth to 1), which the max
    // distance then caps.
    vec2 uv = (vec2(fullPixel) + 0.5) / vec2(fullSize);
    vec4 world = u_frame.inverseViewProjectionMatrix * vec4(uv * 2.0 - 1.0, depth_to_ndc(depth), 1.0);
    vec3 target = world.xyz / world.w;

    vec3 origin = u_frame.cameraPosition.xyz;
    vec3 toTarget = target - origin;
    float sceneDistance = length(toTarget);
    vec3 rayDir = toTarget / max(sceneDistance, 0.0001);
    float marchDistance = min(sceneDistance, u_fog.medium.z);

    int steps = clamp(int(u_fog.march.x), 1, FOG_MAX_STEPS);
    float stepLength = marchDistance / float(steps);
    float offset = interleaved_gradient_noise(vec2(halfPixel), u_fog.march.y);
    float g = u_fog.medium.y;

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < steps; ++i)
    {
        vec3 p = origin + rayDir * ((float(i) + offset) * stepLength);
        float extinction = fog_density(p);
        if (extinction <= 0.0)
        {
            continue;
        }
        // With the scattering coefficient equal to the extinction, the
        // light scattered over the segment and still reaching its start is
        // S * (1 - exp(-extinction * stepLength)); the transmittance so far
        // carries it the rest of the way to the camera.
        float stepTransmittance = exp(-extinction * stepLength);
        scattered += transmittance * in_scattered_light(p, rayDir, g) * (1.0 - stepTransmittance);
        transmittance *= stepTransmittance;
    }

    fragColor = vec4(scattered * u_fog.medium.w, transmittance);
}
