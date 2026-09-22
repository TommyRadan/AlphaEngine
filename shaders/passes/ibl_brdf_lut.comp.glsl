#version 450

// Split-sum environment BRDF integration into a 2D rg table. The
// geometry term uses the IBL remap k = a^2 / 2, which is why it is not
// the direct-lighting geometry_schlick_ggx from include/brdf.glsl.

#include "include/ibl_common.glsl"

layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, set = 0, binding = 0) uniform writeonly image2D outLut;

float geometry_schlick_ggx(float nDotX, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    return nDotX / (nDotX * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometry_schlick_ggx(max(dot(N, V), 0.0), roughness) *
           geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);
}

vec2 integrate_brdf(float nDotV, float roughness)
{
    vec3 V = vec3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    vec3 N = vec3(0.0, 0.0, 1.0);
    float scale = 0.0;
    float bias = 0.0;
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importance_sample_ggx(xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float nDotL = max(L.z, 0.0);
        float nDotH = max(H.z, 0.0);
        float vDotH = max(dot(V, H), 0.0);
        if (nDotL > 0.0)
        {
            float g = geometry_smith(N, V, L, roughness);
            float gVis = (g * vDotH) / (nDotH * nDotV);
            float fc = pow(1.0 - vDotH, 5.0);
            scale += (1.0 - fc) * gVis;
            bias += fc * gVis;
        }
    }
    return vec2(scale, bias) / float(SAMPLE_COUNT);
}

void main()
{
    ivec2 size = imageSize(outLut);
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= size.x || p.y >= size.y)
    {
        return;
    }
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec2 result = integrate_brdf(uv.x, uv.y);
    imageStore(outLut, p, vec4(result, 0.0, 1.0));
}
