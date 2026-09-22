#version 450

// Blinn-Phong lit surface: ambient + per-light diffuse and specular for
// the packed directional and point lights, the directional caster's
// shadow, and distance fog. The per-frame, lights, shadow and fog code
// is shared with standard_material through the includes.

#include "include/bindings.glsl"
#include "include/per_frame.glsl"
#include "include/lights.glsl"
#include "include/shadows.glsl"
#include "include/fog.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 cameraPosition;

layout(location = 0) out vec4 fragColor;

// std140: vec4 diffuseColor at 0, vec4 specular (rgb colour, a
// shininess) at 16, vec4 misc (x useTexture) at 32; 48 bytes.
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 diffuseColor;
    vec4 specular; // rgb specular colour, a shininess
    vec4 misc;     // x useTexture
} u_material;

layout(set = 2, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D diffuseMap;

void main()
{
    vec3 N = normalize(worldNormal);
    vec3 V = normalize(cameraPosition - worldPosition);

    vec3 diffuseAlbedo = u_material.diffuseColor.rgb;
    if (u_material.misc.x != 0.0)
    {
        diffuseAlbedo *= texture(diffuseMap, texCoord).rgb;
    }
    vec3 specularColor = u_material.specular.rgb;
    float shininess = max(u_material.specular.a, 1.0);

    vec3 result = u_lights.ambient.rgb * diffuseAlbedo;

    for (int i = 0; i < u_lights.counts.x; ++i)
    {
        vec3 L = normalize(-u_lights.directional[i].direction.xyz);
        float nDotL = max(dot(N, L), 0.0);
        vec3 radiance = u_lights.directional[i].color.rgb;
        float shadow = directional_shadow(worldPosition, i, N, L);
        result += shadow * nDotL * radiance * diffuseAlbedo;
        if (nDotL > 0.0)
        {
            vec3 H = normalize(L + V);
            float nDotH = max(dot(N, H), 0.0);
            result += shadow * pow(nDotH, shininess) * radiance * specularColor;
        }
    }

    for (int i = 0; i < u_lights.counts.y; ++i)
    {
        vec3 toLight = u_lights.point[i].position.xyz - worldPosition;
        float dist = length(toLight);
        float range = u_lights.point[i].attenuation.x;
        if (range > 0.0 && dist > range)
        {
            continue;
        }
        vec3 L = toLight / max(dist, 0.0001);
        float constant = u_lights.point[i].attenuation.y;
        float linear = u_lights.point[i].attenuation.z;
        float quadratic = u_lights.point[i].attenuation.w;
        float atten = 1.0 / (constant + linear * dist + quadratic * dist * dist);
        vec3 radiance = u_lights.point[i].color.rgb * atten;
        float nDotL = max(dot(N, L), 0.0);
        result += nDotL * radiance * diffuseAlbedo;
        if (nDotL > 0.0)
        {
            vec3 H = normalize(L + V);
            float nDotH = max(dot(N, H), 0.0);
            result += pow(nDotH, shininess) * radiance * specularColor;
        }
    }

    result = apply_fog(result, worldPosition, cameraPosition);
    fragColor = vec4(result, u_material.diffuseColor.a);
}
