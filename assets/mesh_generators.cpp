// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <assets/mesh_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <assets/cache_key.hpp>
#include <assets/tangent.hpp>
#include <assets/vertex.hpp>
#include <core/math/math.hpp>

namespace assets::mesh_generators
{
    namespace
    {
        using core::math::half_pi;
        using core::math::pi;
        using core::math::vec2;
        using core::math::vec3;

        // The record every generator emits, as the suffix of its cache key.
        std::string record_name()
        {
            return vertex_format_name(vertex_format::position_uv_normal_tangent);
        }

        // Completes a position / uv / normal mesh into the record every
        // generator emits: the tangents are derived from those channels, whose
        // offsets stay the same, so a material that ignores the tangent reads
        // the record just as well.
        mesh_data with_tangents(const std::vector<vertex_position_uv_normal>& vertices, std::vector<uint32_t> indices)
        {
            const auto tangent_vertices = generate_tangents(vertices, indices);
            return mesh_data::from_vertices(tangent_vertices, std::move(indices));
        }

        // Spherical UV from a unit-length position:
        // azimuth = atan2(z, -x), inclination = acos(-y).
        // u = azimuth / (2*pi) + 0.5, v = inclination / pi + 0.5.
        vec2 spherical_uv(const vec3& dir)
        {
            const float azimuth = std::atan2(dir.z, -dir.x);
            const float inclination = std::acos(-dir.y);
            return vec2{azimuth / (2.0f * pi) + 0.5f, inclination / pi + 0.5f};
        }

        // Per-face anchors of the cubed sphere. For each cube face, `origin`
        // is the (u=0, v=0) corner of the face on the unit cube and
        // (`u_axis`, `v_axis`) are the world-space vectors that traverse the
        // face from u=0->1 and v=0->1 respectively. Conventions match
        // standard (Vulkan) cube maps: u increases to the right when looking
        // at the face from outside, v increases downward.
        struct cube_face
        {
            vec3 origin;
            vec3 u_axis;
            vec3 v_axis;
        };

        const cube_face cube_faces[6] = {
            // +X (right)
            {vec3{+1.0f, +1.0f, +1.0f}, vec3{0.0f, 0.0f, -2.0f}, vec3{0.0f, -2.0f, 0.0f}},
            // -X (left)
            {vec3{-1.0f, +1.0f, -1.0f}, vec3{0.0f, 0.0f, +2.0f}, vec3{0.0f, -2.0f, 0.0f}},
            // +Y (top)
            {vec3{-1.0f, +1.0f, -1.0f}, vec3{+2.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, +2.0f}},
            // -Y (bottom)
            {vec3{-1.0f, -1.0f, +1.0f}, vec3{+2.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, -2.0f}},
            // +Z (back)
            {vec3{-1.0f, +1.0f, +1.0f}, vec3{+2.0f, 0.0f, 0.0f}, vec3{0.0f, -2.0f, 0.0f}},
            // -Z (front)
            {vec3{+1.0f, +1.0f, -1.0f}, vec3{-2.0f, 0.0f, 0.0f}, vec3{0.0f, -2.0f, 0.0f}},
        };

        unsigned int at_least(unsigned int value, unsigned int minimum)
        {
            return value < minimum ? minimum : value;
        }
    } // namespace

    cylinder
    cone(float radius, float height, unsigned int radial_segments, unsigned int height_segments, bool open_ended)
    {
        return cylinder{0.0f, radius, height, radial_segments, height_segments, open_ended};
    }

    polyhedron tetrahedron(float radius, unsigned int detail)
    {
        return polyhedron{
            {{1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}},
            {2, 1, 0, 0, 3, 2, 1, 3, 0, 2, 3, 1},
            radius,
            detail,
        };
    }

    polyhedron octahedron(float radius, unsigned int detail)
    {
        return polyhedron{
            {{1.0f, 0.0f, 0.0f},
             {-1.0f, 0.0f, 0.0f},
             {0.0f, 1.0f, 0.0f},
             {0.0f, -1.0f, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {0.0f, 0.0f, -1.0f}},
            {0, 2, 4, 0, 4, 3, 0, 3, 5, 0, 5, 2, 1, 2, 5, 1, 5, 3, 1, 3, 4, 1, 4, 2},
            radius,
            detail,
        };
    }

    polyhedron dodecahedron(float radius, unsigned int detail)
    {
        // Golden ratio t = (1 + sqrt(5)) / 2 and its reciprocal r = 1 / t.
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
        const float r = 1.0f / t;

        // 20 vertices: (+-1, +-1, +-1), (0, +-r, +-t), (+-r, +-t, 0) and
        // (+-t, 0, +-r); 12 pentagons, each cut into three triangles.
        return polyhedron{
            {{-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, 1.0f},
             {1.0f, -1.0f, -1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, -1.0f},  {1.0f, 1.0f, 1.0f},
             {0.0f, -r, -t},        {0.0f, -r, t},        {0.0f, r, -t},        {0.0f, r, t},
             {-r, -t, 0.0f},        {-r, t, 0.0f},        {r, -t, 0.0f},        {r, t, 0.0f},
             {-t, 0.0f, -r},        {t, 0.0f, -r},        {-t, 0.0f, r},        {t, 0.0f, r}},
            {
                3,  11, 7,  3,  7,  15, 3,  15, 13, 7,  19, 17, 7,  17, 6,  7,  6, 15, 17, 4,  8,  17,
                8,  10, 17, 10, 6,  8,  0,  16, 8,  16, 2,  8,  2,  10, 0,  12, 1, 0,  1,  18, 0,  18,
                16, 6,  10, 2,  6,  2,  13, 6,  13, 15, 2,  16, 18, 2,  18, 3,  2, 3,  13, 18, 1,  9,
                18, 9,  11, 18, 11, 3,  4,  14, 12, 4,  12, 0,  4,  0,  8,  11, 9, 5,  11, 5,  19, 11,
                19, 7,  19, 5,  14, 19, 14, 4,  19, 4,  17, 1,  12, 14, 1,  14, 5, 1,  5,  9,
            },
            radius,
            detail,
        };
    }

    polyhedron icosahedron(float radius, unsigned int detail)
    {
        // Golden ratio t = (1 + sqrt(5)) / 2.
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

        // 12 vertices, 20 triangles.
        return polyhedron{
            {{-1.0f, t, 0.0f},
             {1.0f, t, 0.0f},
             {-1.0f, -t, 0.0f},
             {1.0f, -t, 0.0f},
             {0.0f, -1.0f, t},
             {0.0f, 1.0f, t},
             {0.0f, -1.0f, -t},
             {0.0f, 1.0f, -t},
             {t, 0.0f, -1.0f},
             {t, 0.0f, 1.0f},
             {-t, 0.0f, -1.0f},
             {-t, 0.0f, 1.0f}},
            {
                0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4,  11, 10, 2,  10, 7, 6, 7, 1, 8,
                3, 9,  4, 3, 4, 2, 3, 2, 6, 3, 6, 8,  3, 8,  9,  4, 9, 5, 2, 4,  11, 6,  2,  10, 8,  6, 7, 9, 8, 1,
            },
            radius,
            detail,
        };
    }

    mesh_data generate(const box& shape)
    {
        const unsigned int width_segments = at_least(shape.width_segments, 1);
        const unsigned int height_segments = at_least(shape.height_segments, 1);
        const unsigned int depth_segments = at_least(shape.depth_segments, 1);

        std::vector<vertex_position_uv_normal> vertices;
        std::vector<uint32_t> indices;

        // Builds one tessellated, axis-aligned face as a grid of grid_u x
        // grid_v segments. @c u_axis and @c v_axis are the unit basis vectors
        // spanning the face plane; @c w_dir is the outward normal direction.
        // @c u_len / @c v_len are the face extents along those axes and
        // @c w_off the (signed) distance from the origin to the face plane.
        // The face is centered on the origin via the half extents. Winding is
        // CCW when viewed from outside (along -w_dir toward the face), matching
        // the sphere convention so backface culling treats all primitives
        // alike.
        const auto build_face = [&](const vec3& u_axis,
                                    const vec3& v_axis,
                                    const vec3& w_dir,
                                    float u_len,
                                    float v_len,
                                    float w_off,
                                    unsigned int grid_u,
                                    unsigned int grid_v)
        {
            const auto base = static_cast<uint32_t>(vertices.size());
            const float half_u = u_len * 0.5f;
            const float half_v = v_len * 0.5f;

            for (unsigned int iy = 0; iy <= grid_v; ++iy)
            {
                const float ty = static_cast<float>(iy) / static_cast<float>(grid_v);
                const float v_pos = ty * v_len - half_v;

                for (unsigned int ix = 0; ix <= grid_u; ++ix)
                {
                    const float tx = static_cast<float>(ix) / static_cast<float>(grid_u);
                    const float u_pos = tx * u_len - half_u;

                    vertex_position_uv_normal vertex;
                    vertex.pos = u_axis * u_pos + v_axis * v_pos + w_dir * w_off;
                    vertex.normal = w_dir;
                    vertex.uv = vec2{tx, 1.0f - ty};
                    vertices.push_back(vertex);
                }
            }

            const unsigned int columns = grid_u + 1;
            for (unsigned int iy = 0; iy < grid_v; ++iy)
            {
                for (unsigned int ix = 0; ix < grid_u; ++ix)
                {
                    const uint32_t a = base + iy * columns + ix;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + columns;
                    const uint32_t d = c + 1;

                    // a/b advance along +u, c/d along +v, and the outward
                    // normal is cross(u, v); the triangles must therefore
                    // wind a->d->c / a->b->d to come out CCW when viewed from
                    // outside (the a->c->d / a->d->b order is CW and would be
                    // culled, turning the box inside-out).
                    indices.push_back(a);
                    indices.push_back(d);
                    indices.push_back(c);

                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);
                }
            }
        };

        const vec3 axis_x{1.0f, 0.0f, 0.0f};
        const vec3 axis_y{0.0f, 1.0f, 0.0f};
        const vec3 axis_z{0.0f, 0.0f, 1.0f};
        const float half_w = shape.width * 0.5f;
        const float half_h = shape.height * 0.5f;
        const float half_d = shape.depth * 0.5f;

        // +Z front: u = +x, v = +y. +X right: u = -z, v = +y.
        // -Z back: u = -x, v = +y. -X left: u = +z, v = +y.
        // +Y top: u = +x, v = -z. -Y bottom: u = +x, v = +z.
        // The u/v axes are chosen so the cross(u, v) points along the outward
        // normal, which keeps the CCW-from-outside winding above consistent
        // across all six faces.
        build_face(axis_x, axis_y, axis_z, shape.width, shape.height, half_d, width_segments, height_segments);
        build_face(-axis_z, axis_y, axis_x, shape.depth, shape.height, half_w, depth_segments, height_segments);
        build_face(-axis_x, axis_y, -axis_z, shape.width, shape.height, half_d, width_segments, height_segments);
        build_face(axis_z, axis_y, -axis_x, shape.depth, shape.height, half_w, depth_segments, height_segments);
        build_face(axis_x, -axis_z, axis_y, shape.width, shape.depth, half_h, width_segments, depth_segments);
        build_face(axis_x, axis_z, -axis_y, shape.width, shape.depth, half_h, width_segments, depth_segments);

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const capsule& shape)
    {
        const float half_length = 0.5f * shape.length;

        const unsigned int columns = shape.radial_segments + 1;

        // A single seamless ring stack, generated top-to-bottom. Each row
        // carries its axial position @c y, the radial scale (distance of the
        // ring from the axis), the vertical component of the surface normal,
        // and a parametric position @c v used for the axial UV coordinate.
        // The top hemisphere, the cylindrical body, and the bottom hemisphere
        // share their boundary rings so the surface has no seams.
        struct row
        {
            float y;
            float radial;
            float nh; // horizontal (XZ) magnitude of the surface normal
            float ny; // vertical (Y) component of the surface normal
            float v;
        };

        std::vector<row> rows;
        rows.reserve(2 * (shape.cap_segments + 1) + 1);

        // Surface arc lengths used to keep the axial UV roughly proportional
        // to real surface distance: a quarter circumference per cap plus the
        // straight body.
        const float cap_arc = half_pi * shape.radius;
        const float total_arc = 2.0f * cap_arc + shape.length;
        const float inv_total_arc = total_arc > 0.0f ? 1.0f / total_arc : 0.0f;

        // Top hemisphere: latitude sweeps from +pi/2 (north pole) down to 0
        // (equator, where it meets the body).
        for (unsigned int i = 0; i <= shape.cap_segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(shape.cap_segments);
            const float lat = half_pi * (1.0f - t);
            const float sin_lat = std::sin(lat);
            const float cos_lat = std::cos(lat);

            row r{};
            r.y = half_length + shape.radius * sin_lat;
            r.radial = shape.radius * cos_lat;
            r.nh = cos_lat;
            r.ny = sin_lat;
            r.v = (t * cap_arc) * inv_total_arc;
            rows.push_back(r);
        }

        // Cylindrical body: interpolate the axial position from +half_length
        // to -half_length. Radial scale is the full radius and the normal is
        // purely radial (no vertical component). The first body ring coincides
        // with the last top-cap ring, so skip it to avoid a degenerate quad.
        for (unsigned int i = 1; i <= shape.radial_segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(shape.radial_segments);

            row r{};
            r.y = half_length - t * shape.length;
            r.radial = shape.radius;
            r.nh = 1.0f;
            r.ny = 0.0f;
            r.v = (cap_arc + t * shape.length) * inv_total_arc;
            rows.push_back(r);
        }

        // Bottom hemisphere: latitude sweeps from 0 (equator) down to -pi/2
        // (south pole). The first bottom-cap ring coincides with the last body
        // ring, so skip it as well.
        for (unsigned int i = 1; i <= shape.cap_segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(shape.cap_segments);
            const float lat = -half_pi * t;
            const float sin_lat = std::sin(lat);
            const float cos_lat = std::cos(lat);

            row r{};
            r.y = -half_length + shape.radius * sin_lat;
            r.radial = shape.radius * cos_lat;
            r.nh = cos_lat;
            r.ny = sin_lat;
            r.v = (cap_arc + shape.length + (-lat / half_pi) * cap_arc) * inv_total_arc;
            rows.push_back(r);
        }

        const unsigned int row_count = static_cast<unsigned int>(rows.size());

        std::vector<vertex_position_uv_normal> vertices;
        vertices.reserve(row_count * columns);

        for (unsigned int i = 0; i < row_count; ++i)
        {
            const row& r = rows[i];

            for (unsigned int j = 0; j < columns; ++j)
            {
                const float u = static_cast<float>(j) / static_cast<float>(shape.radial_segments);
                const float theta = 2.0f * pi * u;
                const float sin_theta = std::sin(theta);
                const float cos_theta = std::cos(theta);

                vertex_position_uv_normal vertex;
                vertex.pos = vec3{r.radial * cos_theta, r.y, r.radial * sin_theta};

                // The radial (XZ) component of the normal points outward in
                // the same direction as the ring offset; on the caps it is the
                // latitude cosine combined with the vertical (sine) component,
                // on the body it is purely radial (nh == 1, ny == 0). The
                // per-ring components are already unit-length, so the result
                // is a unit normal across every region.
                vertex.normal = vec3{r.nh * cos_theta, r.ny, r.nh * sin_theta};

                vertex.uv = vec2{u, 1.0f - r.v};
                vertices.push_back(vertex);
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve((row_count - 1) * shape.radial_segments * 6);

        for (unsigned int i = 0; i + 1 < row_count; ++i)
        {
            for (unsigned int j = 0; j < shape.radial_segments; ++j)
            {
                const uint32_t a = i * columns + j;
                const uint32_t b = a + 1;
                const uint32_t c = a + columns;
                const uint32_t d = c + 1;

                // CCW winding when viewed from outside the capsule, as for
                // the sphere: top-left -> bottom-left -> bottom-right traces a
                // CCW loop in screen space when the outward normal points
                // toward the camera.
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(d);

                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(b);
            }
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const circle& shape)
    {
        const unsigned int segments = at_least(shape.segments, 3);

        std::vector<vertex_position_uv_normal> vertices;
        vertices.reserve(segments + 2);

        const vec3 normal{0.0f, 0.0f, 1.0f};

        // Centre vertex.
        vertex_position_uv_normal centre;
        centre.pos = vec3{0.0f, 0.0f, 0.0f};
        centre.uv = vec2{0.5f, 0.5f};
        centre.normal = normal;
        vertices.push_back(centre);

        // Rim vertices, one extra so the last segment closes against a
        // distinct vertex.
        for (unsigned int s = 0; s <= segments; ++s)
        {
            const float segment =
                shape.theta_start + static_cast<float>(s) / static_cast<float>(segments) * shape.theta_length;
            const float x = shape.radius * std::cos(segment);
            const float y = shape.radius * std::sin(segment);

            vertex_position_uv_normal vertex;
            vertex.pos = vec3{x, y, 0.0f};
            vertex.uv = vec2{x / (2.0f * shape.radius) + 0.5f, y / (2.0f * shape.radius) + 0.5f};
            vertex.normal = normal;
            vertices.push_back(vertex);
        }

        std::vector<uint32_t> indices;
        indices.reserve(segments * 3);

        // Triangle fan. centre -> rim[s] -> rim[s + 1] traces a CCW loop when
        // viewed from +Z, so the +Z normal faces the camera on the front side.
        for (unsigned int s = 1; s <= segments; ++s)
        {
            indices.push_back(0);
            indices.push_back(s);
            indices.push_back(s + 1);
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const cubed_sphere& shape)
    {
        const unsigned int n = shape.subdivisions;
        const unsigned int rows = n + 1;
        const unsigned int verts_per_face = rows * rows;
        const unsigned int quads_per_face = n * n;

        std::vector<vertex_position_uv_normal> vertices;
        vertices.reserve(6 * verts_per_face);

        std::vector<uint32_t> indices;
        indices.reserve(6 * quads_per_face * 6);

        for (const cube_face& f : cube_faces)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());

            for (unsigned int j = 0; j < rows; ++j)
            {
                const float v = static_cast<float>(j) / static_cast<float>(n);
                for (unsigned int i = 0; i < rows; ++i)
                {
                    const float u = static_cast<float>(i) / static_cast<float>(n);
                    const vec3 cube_pos = f.origin + u * f.u_axis + v * f.v_axis;
                    const vec3 sphere_pos = core::math::normalize(cube_pos);

                    vertex_position_uv_normal vertex;
                    vertex.pos = sphere_pos;
                    vertex.normal = sphere_pos;
                    vertex.uv = vec2{u, v};
                    vertices.push_back(vertex);
                }
            }

            for (unsigned int j = 0; j < n; ++j)
            {
                for (unsigned int i = 0; i < n; ++i)
                {
                    const uint32_t a = base + j * rows + i; // (i,   j)   — top-left
                    const uint32_t b = a + 1;               // (i+1, j)   — top-right
                    const uint32_t c = a + rows;            // (i,   j+1) — bottom-left
                    const uint32_t d = c + 1;               // (i+1, j+1) — bottom-right

                    // CCW winding when viewed from outside: TL -> BL -> BR
                    indices.push_back(a);
                    indices.push_back(c);
                    indices.push_back(d);

                    indices.push_back(a);
                    indices.push_back(d);
                    indices.push_back(b);
                }
            }
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const cylinder& shape)
    {
        std::vector<vertex_position_uv_normal> vertices;
        std::vector<uint32_t> indices;

        const float half_height = shape.height * 0.5f;

        // Torso (side wall). Each row of vertices spans radial_segments + 1
        // columns so the seam UV reaches 1.0; the radius is interpolated
        // linearly between the two ends. The normal follows the cone slope:
        // its radial component is unit length in the xz plane and the y
        // component is the slope of the side wall.
        const float slope = (shape.radius_bottom - shape.radius_top) / shape.height;

        for (unsigned int y = 0; y <= shape.height_segments; ++y)
        {
            const float v = static_cast<float>(y) / static_cast<float>(shape.height_segments);
            const float radius = v * (shape.radius_top - shape.radius_bottom) + shape.radius_bottom;

            for (unsigned int x = 0; x <= shape.radial_segments; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(shape.radial_segments);
                const float theta = u * 2.0f * pi;
                const float sin_theta = std::sin(theta);
                const float cos_theta = std::cos(theta);

                vertex_position_uv_normal vertex;
                vertex.pos = vec3{radius * sin_theta, -v * shape.height + half_height, radius * cos_theta};
                vertex.normal = core::math::normalize(vec3{sin_theta, slope, cos_theta});
                vertex.uv = vec2{u, 1.0f - v};
                vertices.push_back(vertex);
            }
        }

        const unsigned int torso_columns = shape.radial_segments + 1;
        for (unsigned int y = 0; y < shape.height_segments; ++y)
        {
            for (unsigned int x = 0; x < shape.radial_segments; ++x)
            {
                const uint32_t a = y * torso_columns + x;
                const uint32_t b = (y + 1) * torso_columns + x;
                const uint32_t c = (y + 1) * torso_columns + (x + 1);
                const uint32_t d = y * torso_columns + (x + 1);

                // CCW winding when viewed from outside the side wall.
                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(d);

                indices.push_back(b);
                indices.push_back(c);
                indices.push_back(d);
            }
        }

        // Caps. A cap whose radius is 0 is skipped. Each cap is a triangle fan
        // around a dedicated center vertex with a flat (+/-Y) normal.
        const auto generate_cap = [&](bool top)
        {
            const float radius = top ? shape.radius_top : shape.radius_bottom;
            if (radius <= 0.0f)
            {
                return;
            }

            const float sign = top ? 1.0f : -1.0f;
            const float cap_y = half_height * sign;
            const vec3 normal{0.0f, sign, 0.0f};

            const uint32_t center_start = static_cast<uint32_t>(vertices.size());

            // A center vertex per radial segment keeps the cap UVs aligned
            // with the rim vertices.
            for (unsigned int x = 0; x < shape.radial_segments; ++x)
            {
                vertex_position_uv_normal vertex;
                vertex.pos = vec3{0.0f, cap_y, 0.0f};
                vertex.normal = normal;
                vertex.uv = vec2{0.5f, 0.5f};
                vertices.push_back(vertex);
            }

            const uint32_t rim_start = static_cast<uint32_t>(vertices.size());
            for (unsigned int x = 0; x <= shape.radial_segments; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(shape.radial_segments);
                const float theta = u * 2.0f * pi;
                const float sin_theta = std::sin(theta);
                const float cos_theta = std::cos(theta);

                vertex_position_uv_normal vertex;
                vertex.pos = vec3{radius * sin_theta, cap_y, radius * cos_theta};
                vertex.normal = normal;
                vertex.uv = vec2{cos_theta * 0.5f + 0.5f, sin_theta * 0.5f * sign + 0.5f};
                vertices.push_back(vertex);
            }

            for (unsigned int x = 0; x < shape.radial_segments; ++x)
            {
                const uint32_t center = center_start + x;
                const uint32_t r0 = rim_start + x;
                const uint32_t r1 = rim_start + x + 1;

                // Wind so the cap normal (+/-Y) points outward in CCW order.
                if (top)
                {
                    indices.push_back(r0);
                    indices.push_back(r1);
                    indices.push_back(center);
                }
                else
                {
                    indices.push_back(r1);
                    indices.push_back(r0);
                    indices.push_back(center);
                }
            }
        };

        if (!shape.open_ended)
        {
            generate_cap(true);
            generate_cap(false);
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const plane& shape)
    {
        const unsigned int columns = shape.width_segments + 1;
        const unsigned int rows = shape.height_segments + 1;

        std::vector<vertex_position_uv_normal> vertices;
        vertices.reserve(columns * rows);

        const float half_width = shape.width * 0.5f;
        const float half_height = shape.height * 0.5f;

        for (unsigned int i = 0; i < rows; ++i)
        {
            const float v = static_cast<float>(i) / static_cast<float>(shape.height_segments);
            const float y = v * shape.height - half_height;

            for (unsigned int j = 0; j < columns; ++j)
            {
                const float u = static_cast<float>(j) / static_cast<float>(shape.width_segments);
                const float x = u * shape.width - half_width;

                vertex_position_uv_normal vertex;
                vertex.pos = vec3{x, y, 0.0f};
                vertex.uv = vec2{u, 1.0f - v};
                vertex.normal = vec3{0.0f, 0.0f, 1.0f};
                vertices.push_back(vertex);
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(shape.width_segments * shape.height_segments * 6);

        for (unsigned int i = 0; i < shape.height_segments; ++i)
        {
            for (unsigned int j = 0; j < shape.width_segments; ++j)
            {
                const uint32_t a = i * columns + j;
                const uint32_t b = a + 1;
                const uint32_t c = a + columns;
                const uint32_t d = c + 1;

                // CCW winding when viewed from the +Z side (the front face),
                // so the +Z normal points toward the camera looking down -Z.
                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(d);

                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(c);
            }
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const polyhedron& shape)
    {
        // Pull the base direction (unit) for a base vertex index.
        const auto base_dir = [&shape](uint32_t i) { return core::math::normalize(shape.base_vertices[i]); };

        // Subdivision count per edge: 2^detail segments.
        const unsigned int cols = 1u << shape.detail;

        std::vector<vertex_position_uv_normal> vertices;
        std::vector<uint32_t> indices;

        // Each base triangle is subdivided into a triangular grid. A fresh
        // vertex is emitted for every triangle corner, with sequential
        // indices, so the per-triangle UV seam corrections below do not bleed
        // across shared edges.
        for (std::size_t f = 0; f < shape.base_indices.size(); f += 3)
        {
            const vec3 a = base_dir(shape.base_indices[f + 0]);
            const vec3 b = base_dir(shape.base_indices[f + 1]);
            const vec3 c = base_dir(shape.base_indices[f + 2]);

            // A grid of unit-length directions over the triangle (a, b, c),
            // by linear interpolation and renormalization:
            // v[i][j] for i in [0, cols], j in [0, cols - i].
            std::vector<std::vector<vec3>> grid(cols + 1);
            for (unsigned int i = 0; i <= cols; ++i)
            {
                const vec3 ai = core::math::normalize(a + (c - a) * (static_cast<float>(i) / static_cast<float>(cols)));
                const vec3 bi = core::math::normalize(b + (c - b) * (static_cast<float>(i) / static_cast<float>(cols)));

                const unsigned int rows = cols - i;
                grid[i].resize(rows + 1);
                for (unsigned int j = 0; j <= rows; ++j)
                {
                    if (j == 0 && i == cols)
                    {
                        grid[i][j] = ai;
                    }
                    else
                    {
                        grid[i][j] =
                            core::math::normalize(ai + (bi - ai) * (static_cast<float>(j) / static_cast<float>(rows)));
                    }
                }
            }

            // Emit triangles from the grid (two orientations per cell), wound
            // so the outward normal faces away from the centre.
            const auto push_vertex = [&](const vec3& dir)
            {
                vertex_position_uv_normal vertex;
                vertex.pos = dir * shape.radius;
                vertex.normal = dir;
                vertex.uv = spherical_uv(dir);
                vertices.push_back(vertex);
                indices.push_back(static_cast<uint32_t>(indices.size()));
            };

            for (unsigned int i = 0; i < cols; ++i)
            {
                for (unsigned int j = 0; j < 2 * (cols - i) - 1; ++j)
                {
                    const unsigned int k = j / 2;
                    if (j % 2 == 0)
                    {
                        push_vertex(grid[i][k + 1]);
                        push_vertex(grid[i + 1][k]);
                        push_vertex(grid[i][k]);
                    }
                    else
                    {
                        push_vertex(grid[i][k + 1]);
                        push_vertex(grid[i + 1][k + 1]);
                        push_vertex(grid[i + 1][k]);
                    }
                }
            }
        }

        // Seam and pole UV correction, per triangle: every triangle owns
        // three consecutive vertices, so wrap-around and pole singularities
        // are fixed without affecting its neighbours.
        for (std::size_t t = 0; t + 3 <= vertices.size(); t += 3)
        {
            vec2& uv0 = vertices[t + 0].uv;
            vec2& uv1 = vertices[t + 1].uv;
            vec2& uv2 = vertices[t + 2].uv;

            // Wrap-around seam: if a triangle straddles the u = 0/1 boundary
            // the azimuth values jump by ~1; nudge the small ones up by 1.
            const float max_u = std::max({uv0.x, uv1.x, uv2.x});
            const float min_u = std::min({uv0.x, uv1.x, uv2.x});
            if (max_u - min_u > 0.5f)
            {
                if (uv0.x < 0.5f)
                {
                    uv0.x += 1.0f;
                }
                if (uv1.x < 0.5f)
                {
                    uv1.x += 1.0f;
                }
                if (uv2.x < 0.5f)
                {
                    uv2.x += 1.0f;
                }
            }

            // Pole correction: a vertex sitting exactly on a pole has an
            // ill-defined azimuth; bias its u toward the average of the other
            // two so the texture does not pinch into a single column.
            const auto fix_pole = [&](vec3& pos, vec2& uv, const vec2& a, const vec2& b)
            {
                if (std::abs(pos.x) < 1e-5f && std::abs(pos.z) < 1e-5f)
                {
                    uv.x = (a.x + b.x) * 0.5f;
                }
            };
            fix_pole(vertices[t + 0].pos, uv0, uv1, uv2);
            fix_pole(vertices[t + 1].pos, uv1, uv0, uv2);
            fix_pole(vertices[t + 2].pos, uv2, uv0, uv1);
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const ring& shape)
    {
        const unsigned int theta_segments = at_least(shape.theta_segments, 3);
        const unsigned int phi_segments = at_least(shape.phi_segments, 1);

        const unsigned int columns = theta_segments + 1;
        const unsigned int rows = phi_segments + 1;

        std::vector<vertex_position_uv_normal> vertices;
        vertices.reserve(rows * columns);

        const vec3 normal{0.0f, 0.0f, 1.0f};

        // Radial/angular grid: row j sweeps the radius from inner to outer,
        // column i sweeps the angle from theta_start across theta_length.
        float radius = shape.inner_radius;
        const float radius_step = (shape.outer_radius - shape.inner_radius) / static_cast<float>(phi_segments);

        for (unsigned int j = 0; j < rows; ++j)
        {
            for (unsigned int i = 0; i < columns; ++i)
            {
                const float segment =
                    shape.theta_start + static_cast<float>(i) / static_cast<float>(theta_segments) * shape.theta_length;
                const float x = radius * std::cos(segment);
                const float y = radius * std::sin(segment);

                vertex_position_uv_normal vertex;
                vertex.pos = vec3{x, y, 0.0f};
                vertex.uv = vec2{(x / shape.outer_radius + 1.0f) / 2.0f, (y / shape.outer_radius + 1.0f) / 2.0f};
                vertex.normal = normal;
                vertices.push_back(vertex);
            }

            radius += radius_step;
        }

        std::vector<uint32_t> indices;
        indices.reserve(theta_segments * phi_segments * 6);

        // Two triangles per grid cell, wound CCW when viewed from +Z so the +Z
        // normal faces the camera on the front side.
        for (unsigned int j = 0; j < phi_segments; ++j)
        {
            for (unsigned int i = 0; i < theta_segments; ++i)
            {
                const uint32_t a = j * columns + i;
                const uint32_t b = a + columns;
                const uint32_t c = b + 1;
                const uint32_t d = a + 1;

                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(d);

                indices.push_back(b);
                indices.push_back(c);
                indices.push_back(d);
            }
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const sphere& shape)
    {
        const unsigned int rings = shape.stacks + 1;
        const unsigned int columns = shape.slices + 1;

        std::vector<vertex_position_uv_normal> vertices;
        vertices.reserve(rings * columns);

        for (unsigned int i = 0; i < rings; ++i)
        {
            const float v = static_cast<float>(i) / static_cast<float>(shape.stacks);
            const float phi = pi * v;
            const float sin_phi = std::sin(phi);
            const float cos_phi = std::cos(phi);

            for (unsigned int j = 0; j < columns; ++j)
            {
                const float u = static_cast<float>(j) / static_cast<float>(shape.slices);
                const float theta = 2.0f * pi * u;
                const float sin_theta = std::sin(theta);
                const float cos_theta = std::cos(theta);

                vertex_position_uv_normal vertex;
                vertex.pos = vec3{sin_phi * cos_theta, sin_phi * sin_theta, cos_phi};
                vertex.normal = vertex.pos;
                vertex.uv = vec2{u, 1.0f - v};
                vertices.push_back(vertex);
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(shape.stacks * shape.slices * 6);

        for (unsigned int i = 0; i < shape.stacks; ++i)
        {
            for (unsigned int j = 0; j < shape.slices; ++j)
            {
                const uint32_t a = i * columns + j;
                const uint32_t b = a + 1;
                const uint32_t c = a + columns;
                const uint32_t d = c + 1;

                // CCW winding when viewed from outside the sphere — going
                // top-left -> bottom-left -> bottom-right traces a CCW loop in
                // screen space when the outward normal points toward the
                // camera.
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(d);

                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(b);
            }
        }

        return with_tangents(vertices, std::move(indices));
    }

    mesh_data generate(const torus& shape)
    {
        // One extra ring/column of vertices so UVs and the seam wrap cleanly.
        const unsigned int rings = shape.radial_segments + 1;
        const unsigned int columns = shape.tubular_segments + 1;

        std::vector<vertex_position_uv_normal> vertices;
        vertices.reserve(rings * columns);

        for (unsigned int j = 0; j < rings; ++j)
        {
            const float v = static_cast<float>(j) / static_cast<float>(shape.radial_segments) * 2.0f * pi;
            const float sin_v = std::sin(v);
            const float cos_v = std::cos(v);

            for (unsigned int i = 0; i < columns; ++i)
            {
                const float u = static_cast<float>(i) / static_cast<float>(shape.tubular_segments) * shape.arc;
                const float sin_u = std::sin(u);
                const float cos_u = std::cos(u);

                vertex_position_uv_normal vertex;
                vertex.pos = vec3{(shape.radius + shape.tube * cos_v) * cos_u,
                                  (shape.radius + shape.tube * cos_v) * sin_u,
                                  shape.tube * sin_v};

                // Normal points from the centre of the tube cross-section out
                // to the surface point; for a torus this is the unit vector
                // (cos_v*cos_u, cos_v*sin_u, sin_v).
                const vec3 center{shape.radius * cos_u, shape.radius * sin_u, 0.0f};
                vertex.normal = core::math::normalize(vertex.pos - center);

                vertex.uv = vec2{static_cast<float>(i) / static_cast<float>(shape.tubular_segments),
                                 static_cast<float>(j) / static_cast<float>(shape.radial_segments)};
                vertices.push_back(vertex);
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(shape.radial_segments * shape.tubular_segments * 6);

        for (unsigned int j = 0; j < shape.radial_segments; ++j)
        {
            for (unsigned int i = 0; i < shape.tubular_segments; ++i)
            {
                const uint32_t a = j * columns + i;
                const uint32_t b = a + 1;
                const uint32_t c = a + columns;
                const uint32_t d = c + 1;

                // CCW winding when viewed from outside the torus, as for the
                // sphere: a -> c -> d then a -> d -> b traces a CCW loop in
                // screen space when the outward normal faces the camera.
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(d);

                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(b);
            }
        }

        return with_tangents(vertices, std::move(indices));
    }

    std::string cache_key(const box& shape)
    {
        return "box:" + cache_key_number(shape.width) + "x" + cache_key_number(shape.height) + "x" +
               cache_key_number(shape.depth) + ":" + cache_key_number(at_least(shape.width_segments, 1)) + "x" +
               cache_key_number(at_least(shape.height_segments, 1)) + "x" +
               cache_key_number(at_least(shape.depth_segments, 1)) + ":" + record_name();
    }

    std::string cache_key(const capsule& shape)
    {
        return "capsule:" + cache_key_number(shape.radius) + ":" + cache_key_number(shape.length) + ":" +
               cache_key_number(shape.cap_segments) + "x" + cache_key_number(shape.radial_segments) + ":" +
               record_name();
    }

    std::string cache_key(const circle& shape)
    {
        return "circle:" + cache_key_number(shape.radius) + ":" + cache_key_number(shape.segments) + ":" +
               cache_key_number(shape.theta_start) + ":" + cache_key_number(shape.theta_length) + ":" + record_name();
    }

    std::string cache_key(const cubed_sphere& shape)
    {
        return "cubed_sphere:" + std::to_string(shape.subdivisions) + ":" + record_name();
    }

    std::string cache_key(const cylinder& shape)
    {
        return "cylinder:" + cache_key_number(shape.radius_top) + ":" + cache_key_number(shape.radius_bottom) + ":" +
               cache_key_number(shape.height) + ":" + cache_key_number(shape.radial_segments) + ":" +
               cache_key_number(shape.height_segments) + ":" + cache_key_number(shape.open_ended) + ":" + record_name();
    }

    std::string cache_key(const plane& shape)
    {
        return "plane:" + cache_key_number(shape.width) + "x" + cache_key_number(shape.height) + ":" +
               cache_key_number(shape.width_segments) + "x" + cache_key_number(shape.height_segments) + ":" +
               record_name();
    }

    std::string cache_key(const polyhedron& shape)
    {
        // Content-addressed: an FNV-1a digest over the base tables
        // distinguishes the platonic solids (so a dodecahedron and an
        // icosahedron at the same radius / detail do not collide), then
        // radius and detail disambiguate within a single base table.
        std::size_t digest = 1469598103934665603ULL;
        const auto mix = [&digest](const void* data, std::size_t bytes)
        {
            const auto* p = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < bytes; ++i)
            {
                digest ^= p[i];
                digest *= 1099511628211ULL;
            }
        };
        static_assert(sizeof(vec3) == 3 * sizeof(float), "a base vertex digests as its three floats");
        mix(shape.base_vertices.data(), shape.base_vertices.size() * sizeof(vec3));
        mix(shape.base_indices.data(), shape.base_indices.size() * sizeof(uint32_t));
        return "polyhedron:" + std::to_string(digest) + ":r" + std::to_string(shape.radius) + ":d" +
               std::to_string(shape.detail) + ":" + record_name();
    }

    std::string cache_key(const ring& shape)
    {
        return "ring:" + cache_key_number(shape.inner_radius) + ":" + cache_key_number(shape.outer_radius) + ":" +
               cache_key_number(shape.theta_segments) + ":" + cache_key_number(shape.phi_segments) + ":" +
               cache_key_number(shape.theta_start) + ":" + cache_key_number(shape.theta_length) + ":" + record_name();
    }

    std::string cache_key(const sphere& shape)
    {
        return "sphere:" + std::to_string(shape.stacks) + "x" + std::to_string(shape.slices) + ":" + record_name();
    }

    std::string cache_key(const torus& shape)
    {
        return "torus:" + cache_key_number(shape.radius) + ":" + cache_key_number(shape.tube) + ":" +
               cache_key_number(shape.radial_segments) + "x" + cache_key_number(shape.tubular_segments) + ":" +
               cache_key_number(shape.arc) + ":" + record_name();
    }
} // namespace assets::mesh_generators
