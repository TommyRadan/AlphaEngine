#version 450

// Sample the environment cube along the unprojected view ray.

layout(location = 0) in vec3 viewDir;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform samplerCube skybox;

void main()
{
    fragColor = vec4(texture(skybox, normalize(viewDir)).rgb, 1.0);
}
