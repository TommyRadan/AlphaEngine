#version 450

// Bright-pass: extract pixels above the threshold with a soft knee.
// params packs (threshold, knee, 2*knee, 1/(4*knee)) so the knee curve
// costs no per-pixel divides. The original colour is preserved and
// merely scaled by its contribution, so a bloomed highlight keeps its
// hue.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

layout(set = 0, binding = 1, std140) uniform Threshold
{
    vec4 params; // x threshold, y knee, z 2*knee, w 1/(4*knee)
} u_threshold;

void main()
{
    vec3 color = texture(sceneColor, texCoord).rgb;
    float brightness = max(color.r, max(color.g, color.b));

    float threshold = u_threshold.params.x;
    float knee = u_threshold.params.y;

    // Quadratic soft knee around the threshold, then the hard
    // cutoff above it; the larger of the two wins.
    float soft = clamp(brightness - threshold + knee, 0.0, u_threshold.params.z);
    soft = soft * soft * u_threshold.params.w;
    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, 0.0001);

    fragColor = vec4(color * contribution, 1.0);
}
