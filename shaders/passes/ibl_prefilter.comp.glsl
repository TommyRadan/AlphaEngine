#version 450

// Prefiltered specular: GGX importance sampling per output texel, with a
// mip bias on the source lookup to suppress fireflies. One dispatch per
// mip level supplies the roughness via the Params UBO.

#include "include/ibl_common.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform samplerCube envMap;
layout(rgba16f, set = 0, binding = 1) uniform writeonly imageCube outPrefiltered;
layout(set = 0, binding = 2, std140) uniform Params
{
    vec4 data; // x roughness, y source resolution
} u;

float distribution_ggx(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (nDotH * nDotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * d * d);
}

void main()
{
    ivec2 size = imageSize(outPrefiltered);
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    int face = int(gl_GlobalInvocationID.z);
    if (p.x >= size.x || p.y >= size.y)
    {
        return;
    }
    float roughness = u.data.x;
    float resolution = u.data.y;
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec3 N = dir_for_face(face, uv);
    vec3 V = N;

    const uint SAMPLE_COUNT = 1024u;
    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importance_sample_ggx(xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float nDotL = max(dot(N, L), 0.0);
        if (nDotL > 0.0)
        {
            float nDotH = max(dot(N, H), 0.0);
            float hDotV = max(dot(H, V), 0.0);
            float d = distribution_ggx(nDotH, roughness);
            float pdf = (d * nDotH / (4.0 * hDotV)) + 0.0001;
            float saTexel = 4.0 * PI / (6.0 * resolution * resolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
            float mip = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
            prefiltered += textureLod(envMap, L, mip).rgb * nDotL;
            totalWeight += nDotL;
        }
    }
    prefiltered = prefiltered / max(totalWeight, 0.001);
    imageStore(outPrefiltered, ivec3(p, face), vec4(prefiltered, 1.0));
}
