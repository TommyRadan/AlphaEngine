#version 450

// Auto exposure (auto_exposure_pass), stage 4: copies this frame's 1 x 1
// adaptation result into the history target the next frame's adaptation
// reads. A separate history keeps the result tonemap samples at one stable
// handle, where a ping-pong pair would make every consumer alternate
// between two.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D adapted;

void main()
{
    fragColor = texelFetch(adapted, ivec2(0, 0), 0);
}
