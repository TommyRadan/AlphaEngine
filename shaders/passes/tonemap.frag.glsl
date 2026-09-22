#version 450

// A selectable tonemap curve followed by an explicit gamma-2.2 encode.
// The swapchain is regular GL_RGBA8 (no SDL sRGB attribute, no
// GL_FRAMEBUFFER_SRGB), so the framebuffer is treated as linear and the
// encode has to happen here. Gamma 2.2 is visually indistinguishable
// from the piecewise sRGB curve at these magnitudes and is the standard
// chain partner for these operators.
//
// u_tonemap.op selects the curve and matches the
// rendering_engine::tonemap_operator enumerator values exactly:
// 0 = none (clamp only), 1 = Reinhard, 2 = ACES filmic.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// std140 rounds the { float, int } block up to the 16-byte minimum; the
// float sits at offset 0 and the int at offset 4.
layout(set = 0, binding = 1, std140) uniform Tonemap
{
    float exposure;
    int op;
} u_tonemap;

// Krzysztof Narkowicz's ACES filmic approximation.
vec3 aces_filmic(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard's x / (1 + x) shoulder.
vec3 reinhard(vec3 x)
{
    return x / (1.0 + x);
}

void main()
{
    vec3 hdr = texture(sceneColor, texCoord).rgb * u_tonemap.exposure;
    vec3 mapped;
    if (u_tonemap.op == 2)
    {
        mapped = aces_filmic(hdr);
    }
    else if (u_tonemap.op == 1)
    {
        mapped = reinhard(hdr);
    }
    else
    {
        mapped = hdr;
    }
    vec3 ldr = clamp(mapped, 0.0, 1.0);
    vec3 srgb = pow(ldr, vec3(1.0 / 2.2));
    fragColor = vec4(srgb, 1.0);
}
