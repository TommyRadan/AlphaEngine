#version 450

// Depth-only stage for instanced casters, shared by the directional shadow
// pass and each face of the omni shadow pass. The instanced twin of
// shadow.vert.glsl: the model matrix arrives as four vec4 attributes from
// the per-instance stream instanced_mesh binds at vertex slot 1 (the same
// record materials/instanced.vert.glsl reads; the tint that follows the
// matrix is not declared here), so there is no per-draw block.

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 model0;
layout(location = 2) in vec4 model1;
layout(location = 3) in vec4 model2;
layout(location = 4) in vec4 model3;

// Pass-private: the light's view-projection at binding 0 of set 0, as in
// shadow.vert.glsl.
layout(set = 0, binding = 0, std140) uniform LightFrame
{
    mat4 lightViewProj;
} u_light;

void main()
{
    mat4 model = mat4(model0, model1, model2, model3);
    gl_Position = u_light.lightViewProj * model * vec4(position, 1.0);
}
