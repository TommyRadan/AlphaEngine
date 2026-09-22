#version 450

// Depth-only: the colour attachment exists only to keep the framebuffer
// complete, so emit a constant. The depth the lit materials sample comes
// from the depth attachment, written automatically by the rasterizer.

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(1.0);
}
