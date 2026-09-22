// The packed lights block the scene pass uploads into set 0, binding
// BINDING_LIGHTS. Mirrors rendering_engine::gpu_lights byte-for-byte
// (lighting/lights_ubo.hpp); bump the array capacities there and here
// together.
#ifndef AE_LIGHTS_GLSL
#define AE_LIGHTS_GLSL

#include "include/bindings.glsl"

const int MAX_DIRECTIONAL = 4;
const int MAX_POINT = 16;

struct DirectionalLight
{
    vec4 direction; // xyz normalized, w unused
    vec4 color;     // rgb radiance * intensity, a unused
};

struct PointLight
{
    vec4 position;    // xyz world, w unused
    vec4 color;       // rgb radiance * intensity, a unused
    vec4 attenuation; // x range, y constant, z linear, w quadratic
};

layout(set = 0, binding = BINDING_LIGHTS, std140) uniform Lights
{
    vec4 ambient;
    ivec4 counts; // x directional, y point
    DirectionalLight directional[MAX_DIRECTIONAL];
    PointLight point[MAX_POINT];
} u_lights;

#endif // AE_LIGHTS_GLSL
