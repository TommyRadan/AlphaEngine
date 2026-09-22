// Distance fog driven by the per-frame block's fogColor / fogParams.
#ifndef AE_FOG_GLSL
#define AE_FOG_GLSL

#include "include/per_frame.glsl"

// Blend color toward the scene fog colour by the camera distance of the
// fragment at worldPosition. Mode 0 disables fog; 1 is a linear
// near/far ramp; 2 is exponential-squared (THREE.FogExp2). Applied in
// linear/HDR space before the tonemap pass.
vec3 apply_fog(vec3 color, vec3 worldPosition, vec3 cameraPosition)
{
    float mode = u_frame.fogColor.a;
    if (mode < 0.5)
    {
        return color;
    }
    float dist = distance(cameraPosition, worldPosition);
    float factor; // 1 = no fog, 0 = full fog
    if (mode < 1.5)
    {
        factor = clamp((u_frame.fogParams.y - dist) / (u_frame.fogParams.y - u_frame.fogParams.x), 0.0, 1.0);
    }
    else
    {
        float d = u_frame.fogParams.z * dist;
        factor = exp(-d * d);
    }
    return mix(u_frame.fogColor.rgb, color, factor);
}

#endif // AE_FOG_GLSL
