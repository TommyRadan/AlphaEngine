#version 450

// PBR metal/roughness surface: Cook-Torrance direct lighting for the
// packed directional, point and spot lights (with the directional, omni
// and spot caster shadows), image-based ambient from the environment
// tables when one is attached, emissive, and distance fog. The
// per-frame, lights, shadow, fog and BRDF code is shared with
// phong_material through the includes.
//
// Which maps are sampled is decided at compile time by the keywords
// standard_material's template injects as defines:
//   USE_ALBEDO_MAP     base colour (and alpha) from albedoMap
//   USE_NORMAL_MAP     tangent-space normal from normalMap (needs
//                      HAS_TANGENTS, else ignored)
//   USE_METALLIC_MAP   metalness from metalnessMap.r
//   USE_ROUGHNESS_MAP  roughness from roughnessMap.r
//   USE_OCCLUSION_MAP  ambient occlusion from occlusionMap.r (overrides
//                      the packed map's R when both are set)
//   USE_ORM_MAP        roughness / metallic from one packed map (R
//                      occlusion, G roughness, B metallic, the glTF
//                      convention) bound at the metalness slot; replaces
//                      USE_METALLIC_MAP / USE_ROUGHNESS_MAP and supplies
//                      occlusion from R unless USE_OCCLUSION_MAP is set
//   USE_EMISSIVE_MAP   emissive from emissiveMap
//   HAS_TANGENTS       the vertex stage forwards a tangent frame
//   WIREFRAME          unlit base colour, for the line-rasterized view
//   NO_FOG             skip the fog blend (the material's fog flag is
//                      off); the default variant is fogged
// A variant without a keyword carries no sampling code for that map.

#include "include/bindings.glsl"
#include "include/per_frame.glsl"
#include "include/lights.glsl"
#include "include/shadows.glsl"
#include "include/fog.glsl"
#include "include/brdf.glsl"

#if defined(HAS_TANGENTS) && defined(USE_NORMAL_MAP)
#define NORMAL_MAPPED
#endif

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 cameraPosition;
#ifdef HAS_TANGENTS
layout(location = 4) in vec4 worldTangent;
#endif

layout(location = 0) out vec4 fragColor;

// std140, four vec4 rows, 64 bytes (standard_material::material_ubo_size).
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 baseColor;
    vec4 emissive;  // rgb colour, a intensity
    vec4 params;    // x metalness, y roughness, z opacity, w occlusion strength
    vec4 iblParams; // x enabled, y intensity
} u_material;

// Every map slot is declared so the per-material layout is the same for
// every variant; a variant only samples the ones its keywords name.
layout(set = 2, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D albedoMap;
layout(set = 2, binding = BINDING_MATERIAL_NORMAL_MAP) uniform sampler2D normalMap;
// The metalness slot also carries the packed ORM map (USE_ORM_MAP).
layout(set = 2, binding = BINDING_MATERIAL_METALNESS_MAP) uniform sampler2D metalnessMap;
layout(set = 2, binding = BINDING_MATERIAL_ROUGHNESS_MAP) uniform sampler2D roughnessMap;
layout(set = 2, binding = BINDING_MATERIAL_EMISSIVE_MAP) uniform sampler2D emissiveMap;
layout(set = 2, binding = BINDING_MATERIAL_OCCLUSION_MAP) uniform sampler2D occlusionMap;

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
#ifdef NORMAL_MAPPED
    // Gram-Schmidt re-orthonormalize the interpolated tangent
    // against the normal, then rebuild the bitangent with the
    // stored handedness.
    vec3 T = normalize(worldTangent.xyz - N * dot(N, worldTangent.xyz));
    vec3 B = cross(N, T) * worldTangent.w;
    vec3 sampled = texture(normalMap, texCoord).xyz * 2.0 - 1.0;
    return normalize(mat3(T, B, N) * sampled);
#else
    return N;
#endif
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
    float alpha = u_material.baseColor.a * u_material.params.z;
#ifdef USE_ALBEDO_MAP
    vec4 albedoSample = texture(albedoMap, texCoord);
    albedo *= albedoSample.rgb;
    alpha *= albedoSample.a;
#endif

#ifdef WIREFRAME
    // Edges read best flat: the tint alone, no lighting or fog.
    fragColor = vec4(albedo, alpha);
    return;
#endif

    float metalness = u_material.params.x;
    float roughness = u_material.params.y;
    float occlusion = 1.0;
#ifdef USE_ORM_MAP
    // The packed map rides the metalness slot: G roughness, B metallic,
    // and R occlusion unless a separate occlusion map overrides it below.
    vec3 orm = texture(metalnessMap, texCoord).rgb;
    roughness *= orm.g;
    metalness *= orm.b;
    occlusion = orm.r;
#else
#ifdef USE_METALLIC_MAP
    metalness *= texture(metalnessMap, texCoord).r;
#endif
#ifdef USE_ROUGHNESS_MAP
    roughness *= texture(roughnessMap, texCoord).r;
#endif
#endif
#ifdef USE_OCCLUSION_MAP
    occlusion = texture(occlusionMap, texCoord).r;
#endif
    // Occlusion strength (glTF occlusionTexture.strength): 0 ignores
    // the map, 1 applies it fully. Only the ambient term is occluded.
    occlusion = mix(1.0, occlusion, u_material.params.w);

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

    for (int i = 0; i < u_lights.counts.z; ++i)
    {
        vec3 toLight = u_lights.spot[i].position.xyz - worldPosition;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 0.0001);
        float atten = spot_attenuation(i, L, dist);
        if (atten <= 0.0)
        {
            continue;
        }
        vec3 radiance = u_lights.spot[i].color.rgb * atten;
        float shadow = spot_shadow(worldPosition, i, N, L);
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
    ambient *= occlusion;

    vec3 emissive = u_material.emissive.rgb * u_material.emissive.a;
#ifdef USE_EMISSIVE_MAP
    emissive *= texture(emissiveMap, texCoord).rgb;
#endif

    vec3 color = ambient + Lo + emissive;
#ifndef NO_FOG
    color = apply_fog(color, worldPosition, cameraPosition);
#endif
    fragColor = vec4(color, alpha);
}
