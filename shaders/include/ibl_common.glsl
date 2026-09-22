// Helpers shared by the three IBL compute kernels: the cube-face
// direction mapping, the Hammersley sequence and GGX importance sampling.
// dir_for_face mirrors the CPU dir_for_face_uv in ibl/environment.cpp so
// the hardware samplerCube and the written cube agree on orientation.
#ifndef AE_IBL_COMMON_GLSL
#define AE_IBL_COMMON_GLSL

#include "include/constants.glsl"

vec3 dir_for_face(int face, vec2 uv)
{
    float sc = 2.0 * uv.x - 1.0;
    float tc = 2.0 * uv.y - 1.0;
    if (face == 0) return normalize(vec3(1.0, -tc, -sc));
    if (face == 1) return normalize(vec3(-1.0, -tc, sc));
    if (face == 2) return normalize(vec3(sc, 1.0, tc));
    if (face == 3) return normalize(vec3(sc, -1.0, -tc));
    if (face == 4) return normalize(vec3(sc, -tc, 1.0));
    return normalize(vec3(-sc, -tc, -1.0));
}

float radical_inverse_vdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n)
{
    return vec2(float(i) / float(n), radical_inverse_vdc(i));
}

// GGX lobe sample around an arbitrary normal N.
vec3 importance_sample_ggx(vec2 xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

#endif // AE_IBL_COMMON_GLSL
