// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

// The packed lights block the scene pass uploads into set 0, binding
// BINDING_LIGHTS. Mirrors rendering_engine::gpu_lights byte-for-byte
// (lighting/lights_ubo.hpp); bump the array capacities there and here
// together.
#ifndef AE_LIGHTS_GLSL
#define AE_LIGHTS_GLSL

#include "include/bindings.glsl"

const int MAX_DIRECTIONAL = 4;
const int MAX_POINT = 16;
const int MAX_SPOT = 4;

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

struct SpotLight
{
    vec4 position;    // xyz world, w unused
    vec4 direction;   // xyz normalized, from the light toward the scene, w unused
    vec4 color;       // rgb radiance * intensity, a unused
    vec4 attenuation; // x range, y constant, z linear, w quadratic
    vec4 cone;        // x cos(outer angle), y cos(inner angle), zw unused
};

layout(set = 0, binding = BINDING_LIGHTS, std140) uniform Lights
{
    vec4 ambient;
    ivec4 counts; // x directional, y point, z spot
    DirectionalLight directional[MAX_DIRECTIONAL];
    PointLight point[MAX_POINT];
    SpotLight spot[MAX_SPOT];
} u_lights;

// Cone + range attenuation for spot light i, shared by every lit material
// so phong and standard agree on the falloff. @p L is the normalized
// fragment-to-light direction (as built by the point-light loop). Range
// attenuation matches PointLight; the cone term smoothsteps between the
// outer and inner half-angles, so the light is full strength inside the
// inner cone, fades to zero at the outer cone, and is zero beyond it.
float spot_attenuation(int i, vec3 L, float dist)
{
    vec4 attenuation = u_lights.spot[i].attenuation;
    float range = attenuation.x;
    if (range > 0.0 && dist > range)
    {
        return 0.0;
    }
    float rangeAtten = 1.0 / (attenuation.y + attenuation.z * dist + attenuation.w * dist * dist);

    vec3 spotDir = normalize(u_lights.spot[i].direction.xyz);
    float cosAngle = dot(-L, spotDir);
    float cosOuter = u_lights.spot[i].cone.x;
    float cosInner = u_lights.spot[i].cone.y;
    float coneAtten = smoothstep(cosOuter, cosInner, cosAngle);

    return rangeAtten * coneAtten;
}

#endif // AE_LIGHTS_GLSL
