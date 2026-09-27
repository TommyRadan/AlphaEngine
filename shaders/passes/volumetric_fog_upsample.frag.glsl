#version 450

// Volumetric fog, stage 2 of 3 (volumetric_fog_pass): the depth-aware
// upsample of the half-resolution march into a full-resolution rgba16f
// target (in-scattered light in rgb, transmittance in a). Plain bilinear
// filtering would bleed the fog of the background into the silhouette of
// a foreground object and vice versa, a bright or dark halo around every
// edge; instead each of the (up to) four half-resolution texels around
// the pixel is weighted by its bilinear weight times how closely the
// depth it was marched to matches this pixel's own, and when none of
// them matches the pixel takes the closest one outright.
//
// Texel i of the march stands for full-resolution pixel 2i (see
// volumetric_fog.frag.glsl), so an even pixel sits exactly on its texel
// and an odd one halfway between two.

#include "include/depth_utils.glsl"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D marchedFog;
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;

// The volumetric_fog_pass params block, the same std140 layout the march
// declares (see volumetric_fog.frag.glsl); only the inverse projection
// is read here.
layout(set = 0, binding = 2, std140) uniform VolumetricFog
{
    vec4 medium;
    vec4 march;
    mat4 inverseProjection;
} u_fog;

// Relative view-depth difference at which a texel's weight reaches zero.
const float DEPTH_TOLERANCE = 0.1;

// Positive view-space depth of the scene at a full-resolution pixel,
// through the unjittered inverse projection. Only its z and w rows are
// involved, which the temporal-AA jitter does not touch, and it holds for
// perspective and orthographic cameras alike.
float view_depth(ivec2 pixel)
{
    float depth = texelFetch(sceneDepth, pixel, 0).r;
    vec4 view = u_fog.inverseProjection * vec4(0.0, 0.0, depth_to_ndc(depth), 1.0);
    return -view.z / view.w;
}

void main()
{
    ivec2 fullSize = textureSize(sceneDepth, 0);
    ivec2 halfSize = textureSize(marchedFog, 0);
    ivec2 pixel = min(ivec2(texCoord * vec2(fullSize)), fullSize - 1);
    float depth = view_depth(pixel);

    ivec2 base = pixel / 2;
    vec2 f = vec2(pixel - base * 2) * 0.5;

    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    vec4 closest = vec4(0.0, 0.0, 0.0, 1.0);
    float closestDelta = 3.0e38;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            float bilinear = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
            if (bilinear <= 0.0)
            {
                continue;
            }
            ivec2 tap = min(base + ivec2(x, y), halfSize - 1);
            vec4 fog = texelFetch(marchedFog, tap, 0);
            float tapDepth = view_depth(min(tap * 2, fullSize - 1));
            float delta = abs(tapDepth - depth) / max(depth, 0.0001);
            float weight = bilinear * clamp(1.0 - delta / DEPTH_TOLERANCE, 0.0, 1.0);
            sum += fog * weight;
            weightSum += weight;
            if (delta < closestDelta)
            {
                closestDelta = delta;
                closest = fog;
            }
        }
    }

    fragColor = weightSum > 0.0001 ? sum / weightSum : closest;
}
