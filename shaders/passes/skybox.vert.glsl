#version 450

// The fullscreen triangle's clip-space xy is reused as the screen
// direction lookup. Emitting it at z = w pins every fragment to the
// far plane so the depth test rejects the sky wherever scene geometry
// already wrote a nearer depth.

layout(location = 0) in vec2 in_pos;
layout(location = 0) out vec3 viewDir;

// Pass-private set 0: the inverse view-projection at 0, the cube map at 1.
layout(set = 0, binding = 0, std140) uniform Skybox
{
    mat4 invViewProj;
} u_sky;

void main()
{
    // Unproject the far-plane clip point back to world space. The
    // view matrix had its translation stripped, so the eye sits at
    // the origin and the unprojected point doubles as the ray.
    vec4 world = u_sky.invViewProj * vec4(in_pos, 1.0, 1.0);
    viewDir = world.xyz / world.w;
    gl_Position = vec4(in_pos, 1.0, 1.0);
}
