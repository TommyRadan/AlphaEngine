#version 450

// History store: a straight copy of the resolved frame into the history
// target so the next frame's resolve can read it.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D src;

void main()
{
    fragColor = texture(src, texCoord);
}
