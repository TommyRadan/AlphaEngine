// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/debug_draw.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <core/math/math.hpp>

namespace rendering_engine::debug_draw
{
    namespace
    {
        namespace math = core::math;

        // Segments per great circle of a sphere.
        constexpr int circle_segments = 24;

        math::vec3 transform_point(const math::mat4& transform, const math::vec3& point)
        {
            const math::vec4 moved = transform * math::vec4{point.x, point.y, point.z, 1.0f};
            return math::vec3{moved.x, moved.y, moved.z};
        }

        // Records the twelve edges of a hexahedron whose corners are
        // indexed so bit 0 picks the X side, bit 1 the Y side and bit 2 the
        // Z side — the order both a box's corners and a frustum's
        // unprojected clip-space corners come in.
        void add_hexahedron(draw_list& list,
                            const std::array<math::vec3, 8>& corners,
                            const math::vec3& color,
                            float duration,
                            bool depth_test)
        {
            static constexpr std::array<std::array<std::size_t, 2>, 12> edges{
                {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            for (const auto& edge : edges)
            {
                list.add_line(corners[edge[0]], corners[edge[1]], color, duration, depth_test);
            }
        }

        // One line of a grid along one axis: where it sits (the coordinate
        // along the other axis) and whether it is the centre line through
        // the origin.
        struct grid_line
        {
            float coord{0.0f};
            bool center{false};
        };

        // The lines a @p size wide grid with @p divisions cells per side
        // draws along each axis, in ascending order: divisions + 1 evenly
        // spaced lines, with the one at 0 flagged as the centre line. An odd
        // division count puts the origin mid-cell, so no spaced line passes
        // through it; a centre line at 0 is inserted then.
        std::vector<grid_line> grid_lines(float size, int divisions)
        {
            const float half = size * 0.5f;
            const float step = size / static_cast<float>(divisions);
            const bool even = (divisions % 2) == 0;

            std::vector<grid_line> lines;
            lines.reserve(static_cast<std::size_t>(divisions) + (even ? 1u : 2u));
            for (int i = 0; i <= divisions; ++i)
            {
                // An even count puts line divisions / 2 exactly on the
                // origin; deriving its coordinate from the index (rather
                // than testing the float against 0) keeps it exact.
                const bool center = even && i * 2 == divisions;
                const float coord = center ? 0.0f : -half + step * static_cast<float>(i);
                if (!even && i * 2 == divisions + 1)
                {
                    // The first spaced line past the origin: the centre line
                    // goes in ahead of it so the order stays ascending.
                    lines.push_back(grid_line{0.0f, true});
                }
                lines.push_back(grid_line{coord, center});
            }
            return lines;
        }
    } // namespace

    namespace detail
    {
        void add_box(draw_list& list,
                     const core::math::aabb& bounds,
                     const core::math::mat4& transform,
                     const core::math::vec3& color,
                     float duration,
                     bool depth_test)
        {
            std::array<math::vec3, 8> corners{};
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                const math::vec3 corner{(i & 1u) != 0 ? bounds.max.x : bounds.min.x,
                                        (i & 2u) != 0 ? bounds.max.y : bounds.min.y,
                                        (i & 4u) != 0 ? bounds.max.z : bounds.min.z};
                corners[i] = transform_point(transform, corner);
            }
            add_hexahedron(list, corners, color, duration, depth_test);
        }

        void add_sphere(draw_list& list,
                        const core::math::vec3& center,
                        float radius,
                        const core::math::vec3& color,
                        float duration,
                        bool depth_test)
        {
            const std::array<std::array<math::vec3, 2>, 3> planes{{{math::vec3{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                                                                   {math::vec3{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                                                                   {math::vec3{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}}};
            for (const auto& plane : planes)
            {
                math::vec3 previous = center + plane[0] * radius;
                for (int i = 1; i <= circle_segments; ++i)
                {
                    const float angle = math::two_pi * static_cast<float>(i) / static_cast<float>(circle_segments);
                    const math::vec3 next = center + (plane[0] * std::cos(angle) + plane[1] * std::sin(angle)) * radius;
                    list.add_line(previous, next, color, duration, depth_test);
                    previous = next;
                }
            }
        }

        void add_axes(draw_list& list, const core::math::mat4& transform, float size, float duration, bool depth_test)
        {
            const math::vec3 origin = transform_point(transform, math::vec3{0.0f, 0.0f, 0.0f});
            list.add_line(origin,
                          transform_point(transform, math::vec3{size, 0.0f, 0.0f}),
                          math::vec3{1.0f, 0.0f, 0.0f},
                          duration,
                          depth_test);
            list.add_line(origin,
                          transform_point(transform, math::vec3{0.0f, size, 0.0f}),
                          math::vec3{0.0f, 1.0f, 0.0f},
                          duration,
                          depth_test);
            list.add_line(origin,
                          transform_point(transform, math::vec3{0.0f, 0.0f, size}),
                          math::vec3{0.0f, 0.0f, 1.0f},
                          duration,
                          depth_test);
        }

        void add_grid(draw_list& list,
                      const core::math::mat4& transform,
                      float size,
                      int divisions,
                      const core::math::vec3& color,
                      const core::math::vec3& center_color,
                      float duration,
                      bool depth_test)
        {
            const float half = size * 0.5f;
            for (const grid_line& line : grid_lines(size, divisions < 1 ? 1 : divisions))
            {
                const math::vec3& rgb = line.center ? center_color : color;
                // A span parallel to X at this Y, and one parallel to Y at
                // this X.
                list.add_line(transform_point(transform, math::vec3{-half, line.coord, 0.0f}),
                              transform_point(transform, math::vec3{half, line.coord, 0.0f}),
                              rgb,
                              duration,
                              depth_test);
                list.add_line(transform_point(transform, math::vec3{line.coord, -half, 0.0f}),
                              transform_point(transform, math::vec3{line.coord, half, 0.0f}),
                              rgb,
                              duration,
                              depth_test);
            }
        }

        void add_frustum(draw_list& list,
                         const core::math::mat4& view_projection,
                         const core::math::vec3& color,
                         float duration,
                         bool depth_test)
        {
            // The engine's projections put the near plane at clip depth 0
            // and the far plane at 1 (core::math::perspective).
            const math::mat4 inverse_vp = math::inverse(view_projection);
            std::array<math::vec3, 8> corners{};
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                const float x = (i & 1u) != 0 ? 1.0f : -1.0f;
                const float y = (i & 2u) != 0 ? 1.0f : -1.0f;
                const float z = (i & 4u) != 0 ? 1.0f : 0.0f;
                const math::vec4 clip = inverse_vp * math::vec4{x, y, z, 1.0f};
                const float inv_w = clip.w != 0.0f ? 1.0f / clip.w : 1.0f;
                corners[i] = math::vec3{clip.x * inv_w, clip.y * inv_w, clip.z * inv_w};
            }
            add_hexahedron(list, corners, color, duration, depth_test);
        }

        void add_arrow(draw_list& list,
                       const core::math::vec3& from,
                       const core::math::vec3& to,
                       const core::math::vec3& color,
                       float duration,
                       bool depth_test)
        {
            list.add_line(from, to, color, duration, depth_test);
            const float length = math::length(to - from);
            if (length <= 0.0f)
            {
                return;
            }

            // Four barbs around the shaft, in a basis whose up axis
            // reference_up keeps usable when the arrow runs along the
            // engine's up axis.
            const math::vec3 dir = (to - from) / length;
            const math::vec3 right = math::normalize(math::cross(math::reference_up(dir), dir));
            const math::vec3 up = math::cross(dir, right);
            const float head = length * 0.2f;
            const math::vec3 base = to - dir * head;
            for (const math::vec3& side : {right, -right, up, -up})
            {
                list.add_line(to, base + side * (head * 0.5f), color, duration, depth_test);
            }
        }
    } // namespace detail
} // namespace rendering_engine::debug_draw
