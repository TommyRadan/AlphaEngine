#version 450

// The canonical compact FXAA (Timothy Lottes, NVIDIA whitepaper),
// operating on the gamma-encoded LDR image tonemap leaves behind. A 3x3
// luma neighbourhood around the centre texel chooses the edge direction;
// two pairs of bilinear taps along that direction give a narrow (rgbA)
// and a wider (rgbB) blend, and the wider one is kept only while its
// luma stays inside the local min/max so high-contrast detail is not
// over-smeared. clamp_edge sampling on the LDR render-target texture
// keeps the off-centre taps well-defined at the borders, and its linear
// filter is what makes the sub-texel taps actually average neighbours.
//
// u_fxaa.rcp_frame.xy is (1/width, 1/height), the per-texel step the
// edge search walks by, baked once at construction.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D ldrColor;

layout(set = 0, binding = 1, std140) uniform Fxaa
{
    vec4 rcp_frame; // xy = 1/resolution, zw unused
} u_fxaa;

// Largest distance, in texels, the edge blend may reach.
const float span_max = 8.0;
// Scales the neighbourhood luma into the directional reduce term so
// near-uniform regions stop searching; the floor keeps the divide
// below well-conditioned on a perfectly flat patch.
const float reduce_mul = 1.0 / 8.0;
const float reduce_min = 1.0 / 128.0;

void main()
{
    vec2 inv = u_fxaa.rcp_frame.xy;

    vec3 rgb_m = texture(ldrColor, texCoord).rgb;
    vec3 rgb_nw = texture(ldrColor, texCoord + vec2(-1.0, -1.0) * inv).rgb;
    vec3 rgb_ne = texture(ldrColor, texCoord + vec2(1.0, -1.0) * inv).rgb;
    vec3 rgb_sw = texture(ldrColor, texCoord + vec2(-1.0, 1.0) * inv).rgb;
    vec3 rgb_se = texture(ldrColor, texCoord + vec2(1.0, 1.0) * inv).rgb;

    const vec3 luma_weights = vec3(0.299, 0.587, 0.114);
    float luma_m = dot(rgb_m, luma_weights);
    float luma_nw = dot(rgb_nw, luma_weights);
    float luma_ne = dot(rgb_ne, luma_weights);
    float luma_sw = dot(rgb_sw, luma_weights);
    float luma_se = dot(rgb_se, luma_weights);

    float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));
    float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));

    // Edge direction is the gradient of the corner lumas, rotated
    // 90 degrees so the blend runs *along* the edge, not across it.
    vec2 dir;
    dir.x = -((luma_nw + luma_ne) - (luma_sw + luma_se));
    dir.y = ((luma_nw + luma_sw) - (luma_ne + luma_se));

    float dir_reduce = max((luma_nw + luma_ne + luma_sw + luma_se) * (0.25 * reduce_mul), reduce_min);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, vec2(-span_max), vec2(span_max)) * inv;

    // Narrow blend: the two inner taps (+/- 1/6 of the span).
    vec3 rgb_a = 0.5 * (texture(ldrColor, texCoord + dir * (1.0 / 3.0 - 0.5)).rgb +
                        texture(ldrColor, texCoord + dir * (2.0 / 3.0 - 0.5)).rgb);
    // Wider blend: average in the two outer taps (+/- 1/2 of span).
    vec3 rgb_b = rgb_a * 0.5 + 0.25 * (texture(ldrColor, texCoord + dir * -0.5).rgb +
                                       texture(ldrColor, texCoord + dir * 0.5).rgb);

    float luma_b = dot(rgb_b, luma_weights);
    if (luma_b < luma_min || luma_b > luma_max)
    {
        fragColor = vec4(rgb_a, 1.0);
    }
    else
    {
        fragColor = vec4(rgb_b, 1.0);
    }
}
