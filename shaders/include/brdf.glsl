// Cook-Torrance microfacet terms for direct lighting: GGX normal
// distribution, Schlick-GGX geometry with the direct-lighting remap,
// Schlick Fresnel, and the combined diffuse + specular BRDF.
#ifndef AE_BRDF_GLSL
#define AE_BRDF_GLSL

#include "include/constants.glsl"

float distribution_ggx(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(N, H), 0.0);
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometry_schlick_ggx(float nDotX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotX / (nDotX * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float nDotV = max(dot(N, V), 0.0);
    float nDotL = max(dot(N, L), 0.0);
    return geometry_schlick_ggx(nDotV, roughness) * geometry_schlick_ggx(nDotL, roughness);
}

vec3 fresnel_schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with a roughness-aware ceiling so rough surfaces do not
// over-brighten at grazing angles under image-based lighting.
vec3 fresnel_schlick_roughness(float cosTheta, vec3 F0, float roughness)
{
    vec3 Fr = max(vec3(1.0 - roughness), F0);
    return F0 + (Fr - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Outgoing radiance from one light of the given incoming radiance.
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metalness, float roughness, vec3 F0)
{
    float nDotL = max(dot(N, L), 0.0);
    if (nDotL <= 0.0)
    {
        return vec3(0.0);
    }
    vec3 H = normalize(V + L);
    float NDF = distribution_ggx(N, H, roughness);
    float G = geometry_smith(N, V, L, roughness);
    vec3 F = fresnel_schlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * nDotL + 0.0001;
    vec3 specular = numerator / denominator;

    // Energy left over after the Fresnel reflection becomes
    // diffuse; pure metals have no diffuse term.
    vec3 kD = (vec3(1.0) - F) * (1.0 - metalness);
    return (kD * albedo / PI + specular) * radiance * nDotL;
}

#endif // AE_BRDF_GLSL
