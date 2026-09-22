#version 450

// Analytic infinite grid: intersect the per-pixel view ray with the z = 0
// ground plane, draw two grid scales plus the X/Y world axes with
// derivative-based anti-aliasing, fade with distance, and write the
// reconstructed depth so the scene occludes the grid.
//
// GRID_FADE_DISTANCE is the world-space radius from the camera past which
// the grid has fully faded to nothing; grid_material injects it as a
// define, the default below only keeps the file compiling on its own.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

#ifndef GRID_FADE_DISTANCE
#define GRID_FADE_DISTANCE 100.0
#endif

layout(location = 0) in vec3 nearPoint;
layout(location = 1) in vec3 farPoint;
layout(location = 2) in vec3 cameraPoint;

layout(location = 0) out vec4 fragColor;

// Coverage of the nearest grid line at the given spacing, with
// screen-space-derivative anti-aliasing. 1.0 on a line, 0.0 in a
// cell.
float gridCoverage(vec2 p, float spacing)
{
    vec2 coord = p / spacing;
    vec2 derivative = fwidth(coord);
    vec2 distance = abs(fract(coord - 0.5) - 0.5) / max(derivative, vec2(1e-8));
    float line = min(distance.x, distance.y);
    return 1.0 - min(line, 1.0);
}

void main()
{
    vec3 rayDirection = farPoint - nearPoint;

    // Intersect the ray with the Z-up ground plane (z = 0). t
    // outside (0, 1) means the plane is behind the camera or past
    // the far plane, so there is nothing to draw.
    float t = -nearPoint.z / rayDirection.z;
    if (t <= 0.0 || t >= 1.0)
    {
        discard;
    }

    vec3 world = nearPoint + t * rayDirection;

    // Reconstructed depth (references the per-draw model matrix so
    // the binding stays live; identity for the origin grid). Map
    // the GL-convention clip z in [-1, 1] to the [0, 1] window
    // depth range used by both backends.
    vec4 clip = u_frame.projectionMatrix * u_frame.viewMatrix * u_draw.modelMatrix * vec4(world, 1.0);
    gl_FragDepth = 0.5 * (clip.z / clip.w) + 0.5;

    vec2 plane = world.xy;
    float minor = gridCoverage(plane, 1.0);
    float major = gridCoverage(plane, 10.0);

    vec3 minorColor = vec3(0.32);
    vec3 majorColor = vec3(0.55);
    vec3 color = mix(minorColor, majorColor, major);
    float alpha = max(minor * 0.55, major);

    // Coloured world axes: the Y axis (x = 0) green, the X axis
    // (y = 0) red, each one derivative-width wide.
    vec2 axisWidth = fwidth(plane);
    if (abs(world.x) < axisWidth.x)
    {
        color = vec3(0.25, 0.85, 0.35);
        alpha = max(alpha, 1.0);
    }
    if (abs(world.y) < axisWidth.y)
    {
        color = vec3(0.9, 0.3, 0.35);
        alpha = max(alpha, 1.0);
    }

    // Fade out with distance from the camera so the grid dissolves
    // into the horizon rather than aliasing into a solid mass.
    float fade = 1.0 - clamp(length(world - cameraPoint) / GRID_FADE_DISTANCE, 0.0, 1.0);
    alpha *= fade * fade;

    if (alpha <= 0.001)
    {
        discard;
    }
    fragColor = vec4(color, alpha);
}
