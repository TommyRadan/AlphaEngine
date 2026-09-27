#version 450

// Auto exposure (auto_exposure_pass), stage 2: one step of the metering
// reduction, a quarter of the source size in each axis. The output texel's
// centre sits on the middle corner of its 4 x 4 source block, so four
// bilinear taps one source texel away from it along each diagonal land
// exactly between 2 x 2 texel quads and average all sixteen texels with
// equal weight. Both channels are averaged: r (weight * log2 luminance)
// and g (weight), whose ratio the adaptation stage takes.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D source;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(source, 0));
    vec2 sum = textureLod(source, texCoord + vec2(-1.0, -1.0) * texel, 0.0).rg +
               textureLod(source, texCoord + vec2(1.0, -1.0) * texel, 0.0).rg +
               textureLod(source, texCoord + vec2(-1.0, 1.0) * texel, 0.0).rg +
               textureLod(source, texCoord + vec2(1.0, 1.0) * texel, 0.0).rg;
    fragColor = vec4(sum * 0.25, 0.0, 1.0);
}
