#version 450

// Separable Gaussian. offset.xy is the per-tap texel step along the blur
// axis (horizontal or vertical), baked from the source texture's texel
// size. Nine taps (centre + four mirrored pairs) with the standard
// sigma-2 weights. clamp_edge sampling on the render-target textures
// keeps the kernel well-defined at the borders.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D src;

layout(set = 0, binding = 1, std140) uniform Blur
{
    vec4 offset; // xy texel step per tap, zw unused
} u_blur;

void main()
{
    const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 step = u_blur.offset.xy;

    vec3 result = texture(src, texCoord).rgb * weight[0];
    for (int i = 1; i < 5; ++i)
    {
        result += texture(src, texCoord + step * float(i)).rgb * weight[i];
        result += texture(src, texCoord - step * float(i)).rgb * weight[i];
    }

    fragColor = vec4(result, 1.0);
}
