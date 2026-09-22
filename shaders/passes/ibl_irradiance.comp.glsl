#version 450

// Diffuse irradiance: cosine-weighted hemisphere convolution per output
// texel, written into an imageCube (z = face).

#include "include/ibl_common.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform samplerCube envMap;
layout(rgba16f, set = 0, binding = 1) uniform writeonly imageCube outIrradiance;

void main()
{
    ivec2 size = imageSize(outIrradiance);
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    int face = int(gl_GlobalInvocationID.z);
    if (p.x >= size.x || p.y >= size.y)
    {
        return;
    }
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec3 N = dir_for_face(face, uv);

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    const float sampleDelta = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            vec3 tangent = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 dir = tangent.x * right + tangent.y * up + tangent.z * N;
            irradiance += texture(envMap, dir).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    }
    irradiance = PI * irradiance / max(samples, 1.0);
    imageStore(outIrradiance, ivec3(p, face), vec4(irradiance, 1.0));
}
