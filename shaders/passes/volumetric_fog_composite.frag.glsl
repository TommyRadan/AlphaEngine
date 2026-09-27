#version 450

// Volumetric fog, stage 3 of 3 (volumetric_fog_pass): emits the
// upsampled fog (in-scattered light in rgb, transmittance in a) over the
// HDR scene colour. The pipeline blends (one, src_alpha) and writes only
// rgb, so the target becomes scene * transmittance + in-scattered light
// and its alpha is left alone. The fog target matches the scene target's
// size, so each pixel fetches its own texel.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D fog;

void main()
{
    ivec2 size = textureSize(fog, 0);
    fragColor = texelFetch(fog, min(ivec2(texCoord * vec2(size)), size - 1), 0);
}
