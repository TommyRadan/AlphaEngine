#version 450

// The temporal resolve. The current jittered frame is blended with last
// frame's accumulated result; because the projection jitter moves the
// sub-pixel sample location every frame, that blend converges to a
// supersampled image. The history is first reprojected along the
// per-pixel motion vectors (history sampled at texCoord - velocity) so a
// moving camera keeps the accumulated detail glued to the surface rather
// than smearing it across the screen. It is then constrained to the 3x3
// colour box around the current texel (the standard neighbourhood clamp)
// so history that survives reprojection but no longer matches the
// present frame, at disocclusions or for the unmodelled object motion,
// is pulled back in; history that reprojects outside the frame is
// dropped entirely in favour of the current sample.
//
// u_taa.params.xy is (1/width, 1/height), the per-texel step the
// neighbourhood taps walk by, and params.z is the history feedback
// weight, baked to 0 on the first frame (history undefined) and to
// taa_feedback thereafter.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D historyColor;
layout(set = 0, binding = 2) uniform sampler2D velocity;

layout(set = 0, binding = 3, std140) uniform Taa
{
    vec4 params; // x = 1/width, y = 1/height, z = history feedback, w unused
} u_taa;

void main()
{
    vec2 inv = u_taa.params.xy;

    vec3 current = texture(currentColor, texCoord).rgb;

    // 3x3 neighbourhood min/max of the current frame: the colour
    // box the reprojected history is clamped into before blending.
    vec3 box_min = current;
    vec3 box_max = current;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }
            vec3 s = texture(currentColor, texCoord + vec2(x, y) * inv).rgb;
            box_min = min(box_min, s);
            box_max = max(box_max, s);
        }
    }

    // Reproject the history along this pixel's motion vector. A
    // sample that lands off-screen has no valid history, so fall
    // back to the current frame (feedback 0) there.
    vec2 motion = texture(velocity, texCoord).xy;
    vec2 history_uv = texCoord - motion;
    float feedback = u_taa.params.z;
    if (any(lessThan(history_uv, vec2(0.0))) || any(greaterThan(history_uv, vec2(1.0))))
    {
        feedback = 0.0;
        history_uv = texCoord;
    }

    vec3 history = texture(historyColor, history_uv).rgb;
    history = clamp(history, box_min, box_max);

    vec3 resolved = mix(current, history, feedback);
    fragColor = vec4(resolved, 1.0);
}
