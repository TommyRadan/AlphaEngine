#version 450

// Instanced unlit geometry. The model matrix and colour arrive as
// ordinary vertex attributes from a per-instance stream (divisor 1), so
// the path does not depend on gl_InstanceIndex behaving across the
// SPIR-V -> GL translation. Location 0 is the shared geometry position;
// the model matrix occupies four consecutive vec4 slots (one per column)
// and the tint follows.

#include "include/per_frame.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 model0;
layout(location = 2) in vec4 model1;
layout(location = 3) in vec4 model2;
layout(location = 4) in vec4 model3;
layout(location = 5) in vec4 instanceTint;

layout(location = 0) out vec4 instanceColor;

void main()
{
    mat4 model = mat4(model0, model1, model2, model3);
    instanceColor = instanceTint;
    gl_Position = u_frame.projectionMatrix * u_frame.viewMatrix * model * vec4(position, 1.0);
}
