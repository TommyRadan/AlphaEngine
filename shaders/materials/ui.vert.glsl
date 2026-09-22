#version 450

// UI quads arrive already in clip space; the stage only forwards the uv.

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 texCoord;

void main()
{
    texCoord = uv;
    gl_Position = vec4(position, 1.0);
}
