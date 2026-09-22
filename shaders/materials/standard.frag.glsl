#version 450

// PBR metal/roughness surface: Cook-Torrance direct lighting for the
// packed directional and point lights (with the directional and omni
// caster shadows), image-based ambient from the environment tables when
// one is attached, emissive, and distance fog. The per-frame, lights,
// shadow, fog and BRDF code is shared with phong_material through the
// includes.

#include "include/bindings.glsl"
#include "include/per_frame.glsl"
#include "include/lights.glsl"
#include "include/shadows.glsl"
#include "include/fog.glsl"
#include "include/brdf.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 cameraPosition;
layout(location = 4) in vec4 worldTangent;

layout(location = 0) out vec4 fragColor;

// std140, six vec4 rows, 96 bytes (standard_material::material_ubo_size).
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 baseColor;
    vec4 emissive;  // rgb colour, a intensity
    vec4 params;    // x metalness, y roughness, z opacity
    vec4 mapFlags;  // x albedo, y normal, z metalness, w roughness
    vec4 mapFlags2; // x emissive
    vec4 iblParams; // x enabled, y intensity
} u_material;

layout(set = 2, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D albedoMap;
layout(set = 2, binding = BINDING_MATERIAL_NORMAL_MAP) uniform sampler2D normalMap;
layout(set = 2, binding = BINDING_MATERIAL_METALNESS_MAP) uniform sampler2D metalnessMap;
layout(set = 2, binding = BINDING_MATERIAL_ROUGHNESS_MAP) uniform sampler2D roughnessMap;
layout(set = 2, binding = BINDING_MATERIAL_EMISSIVE_MAP) uniform sampler2D emissiveMap;

// Image-based lighting: the diffuse irradiance cube, the
// prefiltered specular cube (the skybox mip chain), and the
// split-sum environment BRDF table. Sampled only when
// u_material.iblParams.x is set.
layout(set = 2, binding = BINDING_MATERIAL_IRRADIANCE_MAP) uniform samplerCube irradianceMap;
layout(set = 2, binding = BINDING_MATERIAL_PREFILTERED_MAP) uniform samplerCube prefilteredMap;
layout(set = 2, binding = BINDING_MATERIAL_BRDF_LUT) uniform sampler2D brdfLut;

vec3 shading_normal()
{
    vec3 N = normalize(worldNormal);
    if (u_material.mapFlags.y == 0.0)
    {
        return N;
    }
    // Gram-Schmidt re-orthonormalize the interpolated tangent
    // against the normal, then rebuild the bitangent with the
    // stored handedness.
    vec3 T = normalize(worldTangent.xyz - N * dot(N, worldTangent.xyz));
    vec3 B = cross(N, T) * worldTangent.w;
    vec3 sampled = texture(normalMap, texCoord).xyz * 2.0 - 1.0;
    return normalize(mat3(T, B, N) * sampled);
}

// Geometric specular anti-aliasing (Tokuyoshi & Kaplanyan 2019,
// "Improved Geometric Specular Antialiasing"). Sub-pixel curvature
// of the shading normal turns a near-mirror surface into a
// flickering highlight as the camera moves, and MSAA cannot help
// because the aliasing is in the shading function, not the geometry
// edge. Estimate the screen-space variance of N from its
// derivatives, fold it into the GGX alpha as extra lobe width, and
// hand back a widened perceptual roughness. The result is always
// >= the input, so it acts as a per-pixel roughness floor. See #144.
float specular_aa_roughness(vec3 N, float roughness)
{
    const float screenVariance = 0.25; // SIGMA^2, kernel strength
    const float maxKernel = 0.18;      // KAPPA, ceiling on added width
    vec3 dndx = dFdx(N);
    vec3 dndy = dFdy(N);
    float variance = screenVariance * (dot(dndx, dndx) + dot(dndy, dndy));
    float kernel = min(2.0 * variance, maxKernel);
    // Filter in GGX-alpha space (alpha = perceptual^2), then map the
    // widened alpha back to a perceptual roughness for the rest of
    // the shader (the IBL LOD select and the GGX re-square).
    float alpha = roughness * roughness;
    float filteredAlpha = sqrt(alpha * alpha + kernel);
    return sqrt(filteredAlpha);
}

// Image-based ambient: diffuse irradiance plus prefiltered specular
// weighted by the split-sum environment BRDF. The prefiltered cube
// is the skybox mip chain, so perceptual roughness selects the LOD.
vec3 ibl_ambient(vec3 N, vec3 V, vec3 albedo, float metalness, float roughness, vec3 F0)
{
    float nDotV = max(dot(N, V), 0.0);
    vec3 F = fresnel_schlick_roughness(nDotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metalness);

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    vec3 R = reflect(-V, N);
    float maxLod = float(textureQueryLevels(prefilteredMap) - 1);
    vec3 prefiltered = textureLod(prefilteredMap, R, roughness * maxLod).rgb;
    vec2 envBrdf = texture(brdfLut, vec2(nDotV, roughness)).rg;
    vec3 specular = prefiltered * (F * envBrdf.x + envBrdf.y);

    return (kD * diffuse + specular) * u_material.iblParams.y;
}

void main()
{
    vec3 albedo = u_material.baseColor.rgb;
    if (u_material.mapFlags.x != 0.0)
    {
        albedo *= texture(albedoMap, texCoord).rgb;
    }
    float metalness = u_material.params.x;
    if (u_material.mapFlags.z != 0.0)
    {
        metalness *= texture(metalnessMap, texCoord).r;
    }
    float roughness = u_material.params.y;
    if (u_material.mapFlags.w != 0.0)
    {
        roughness *= texture(roughnessMap, texCoord).r;
    }
    // Clamp roughness away from zero so the GGX denominator and
    // the specular highlight stay finite.
    roughness = clamp(roughness, 0.04, 1.0);

    vec3 N = shading_normal();
    // Widen roughness by the sub-pixel normal variance so sharp
    // metals stop shimmering as the camera moves (issue #144).
    // Runs after the clamp so it can only ever roughen further.
    roughness = specular_aa_roughness(N, roughness);
    vec3 V = normalize(cameraPosition - worldPosition);
    vec3 F0 = mix(vec3(0.04), albedo, metalness);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < u_lights.counts.x; ++i)
    {
        vec3 L = normalize(-u_lights.directional[i].direction.xyz);
        vec3 radiance = u_lights.directional[i].color.rgb;
        float shadow = directional_shadow(worldPosition, i, N, L);
        Lo += shadow * brdf(N, V, L, radiance, albedo, metalness, roughness, F0);
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
        float shadow = point_shadow(worldPosition, i, N, L);
        Lo += shadow * brdf(N, V, L, radiance, albedo, metalness, roughness, F0);
    }

    vec3 ambient;
    if (u_material.iblParams.x > 0.5)
    {
        ambient = ibl_ambient(N, V, albedo, metalness, roughness, F0);
    }
    else
    {
        // Flat ambient fallback when no environment is attached;
        // pure metals reflect nothing without one, so the ambient
        // diffuse fades out as metalness rises.
        ambient = u_lights.ambient.rgb * albedo * (1.0 - metalness);
    }

    vec3 emissive = u_material.emissive.rgb * u_material.emissive.a;
    if (u_material.mapFlags2.x != 0.0)
    {
        emissive *= texture(emissiveMap, texCoord).rgb;
    }

    vec3 color = ambient + Lo + emissive;
    color = apply_fog(color, worldPosition, cameraPosition);
    fragColor = vec4(color, u_material.baseColor.a * u_material.params.z);
}
