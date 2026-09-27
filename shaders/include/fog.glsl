// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

// Distance fog driven by the per-frame block's fogColor / fogParams.
#ifndef AE_FOG_GLSL
#define AE_FOG_GLSL

#include "include/per_frame.glsl"

// Blend color toward the scene fog colour by the camera distance of the
// fragment at worldPosition. Mode 0 disables the distance term; 1 is a
// linear near/far ramp; 2 is exponential-squared (factor = exp(-(density
// * distance)^2)). An analytic exponential height-fog term (Unreal's
// Exponential Height Fog) then layers on top whenever u_frame.fogParams.w
// (height density) is nonzero — including with mode 0, where it runs
// standalone instead of on top of a distance term, matching Unreal's
// height fog being independent of any distance fog. Applied in
// linear/HDR space before the tonemap pass.
vec3 apply_fog(vec3 color, vec3 worldPosition, vec3 cameraPosition)
{
    float mode = u_frame.fogColor.a;
    float heightDensity = u_frame.fogParams.w;
    if (mode < 0.5 && heightDensity <= 0.0)
    {
        return color;
    }
    float dist = distance(cameraPosition, worldPosition);
    float factor; // 1 = no fog, 0 = full fog
    if (mode < 0.5)
    {
        // No distance term active; start from "no fog" and let the
        // height-fog block below fold in its own attenuation.
        factor = 1.0;
    }
    else if (mode < 1.5)
    {
        factor = clamp((u_frame.fogParams.y - dist) / (u_frame.fogParams.y - u_frame.fogParams.x), 0.0, 1.0);
    }
    else
    {
        float d = u_frame.fogParams.z * dist;
        factor = exp(-d * d);
    }

    // Height fog: density(z) = heightDensity * exp(-falloff * (z -
    // referenceHeight)), integrated along the camera-to-fragment ray
    // (world Z is "up"). Closed form of that integral for a ray with
    // world-Z delta deltaZ over its length dist:
    //
    //   opticalDepth = heightDensity * exp(-falloff * (cameraZ - referenceHeight))
    //                  * dist * (1 - exp(-falloff * deltaZ)) / (falloff * deltaZ)
    //
    // The trailing ratio -> 1 as falloff * deltaZ -> 0 (a near-horizontal
    // ray sees a constant density over its length), so a small-epsilon
    // check stands in for that limit instead of dividing by zero.
    //
    // While the volumetric fog pass runs it marches this same medium from
    // the camera out to heightFogParams.z, so the integral here starts
    // that far along the ray (a fragment closer than that gets no height
    // fog of its own) instead of attenuating the stretch a second time.
    // At 0, the default, the ray starts at the camera as above.
    if (heightDensity > 0.0)
    {
        float falloff = u_frame.heightFogParams.x;
        float referenceHeight = u_frame.heightFogParams.y;
        float skipped = min(u_frame.heightFogParams.z, dist);
        vec3 rayStart = cameraPosition + (worldPosition - cameraPosition) * (skipped / max(dist, 1e-4));
        float rayLength = dist - skipped;
        float deltaZ = worldPosition.z - rayStart.z;
        float falloffDeltaZ = falloff * deltaZ;
        float ratio = abs(falloffDeltaZ) > 1e-4 ? (1.0 - exp(-falloffDeltaZ)) / falloffDeltaZ : 1.0;
        float opticalDepth = heightDensity * exp(-falloff * (rayStart.z - referenceHeight)) * rayLength * ratio;
        factor = clamp(factor * exp(-opticalDepth), 0.0, 1.0);
    }

    return mix(u_frame.fogColor.rgb, color, factor);
}

#endif // AE_FOG_GLSL
