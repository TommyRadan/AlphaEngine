// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/lighting/lights_ubo.hpp>

#include <algorithm>
#include <cmath>

#include <core/math/math.hpp>

namespace rendering_engine
{
    namespace
    {
        void write_vec3(float (&dst)[4], const core::math::vec3& v, float w)
        {
            dst[0] = v.x;
            dst[1] = v.y;
            dst[2] = v.z;
            dst[3] = w;
        }
    } // namespace

    void pack_lights(std::span<const light_proxy* const> lights, gpu_lights& out)
    {
        out = gpu_lights{};

        core::math::vec3 ambient{0.0f, 0.0f, 0.0f};
        uint32_t directional_count = 0;
        uint32_t point_count = 0;
        uint32_t spot_count = 0;

        for (const light_proxy* l : lights)
        {
            switch (l->type)
            {
            case light_type::ambient:
            {
                ambient += l->color * l->intensity;
                break;
            }
            case light_type::directional:
            {
                if (directional_count >= max_directional_lights)
                {
                    break;
                }
                gpu_directional_light& slot = out.directional[directional_count];
                write_vec3(slot.direction, core::math::normalize(l->direction), 0.0f);
                write_vec3(slot.color, l->color * l->intensity, 0.0f);
                ++directional_count;
                break;
            }
            case light_type::point:
            {
                if (point_count >= max_point_lights)
                {
                    break;
                }
                gpu_point_light& slot = out.point[point_count];
                write_vec3(slot.position, l->position, 0.0f);
                write_vec3(slot.color, l->color * l->intensity, 0.0f);
                slot.attenuation[0] = l->range;
                slot.attenuation[1] = l->constant_attenuation;
                slot.attenuation[2] = l->linear_attenuation;
                slot.attenuation[3] = l->quadratic_attenuation;
                ++point_count;
                break;
            }
            case light_type::spot:
            {
                if (spot_count >= max_spot_lights)
                {
                    break;
                }
                gpu_spot_light& slot = out.spot[spot_count];
                write_vec3(slot.position, l->position, 0.0f);
                write_vec3(slot.direction, core::math::normalize(l->direction), 0.0f);
                write_vec3(slot.color, l->color * l->intensity, 0.0f);
                slot.attenuation[0] = l->range;
                slot.attenuation[1] = l->constant_attenuation;
                slot.attenuation[2] = l->linear_attenuation;
                slot.attenuation[3] = l->quadratic_attenuation;
                // The inner cone can never be wider than the outer one, so
                // the shader's smoothstep always runs in the right order.
                const float outer = l->outer_angle;
                const float inner = std::min(l->inner_angle, outer);
                slot.cone[0] = std::cos(outer);
                slot.cone[1] = std::cos(inner);
                slot.cone[2] = 0.0f;
                slot.cone[3] = 0.0f;
                ++spot_count;
                break;
            }
            }
        }

        out.ambient[0] = ambient.x;
        out.ambient[1] = ambient.y;
        out.ambient[2] = ambient.z;
        out.ambient[3] = 0.0f;
        out.directional_count = static_cast<int32_t>(directional_count);
        out.point_count = static_cast<int32_t>(point_count);
        out.spot_count = static_cast<int32_t>(spot_count);
    }
} // namespace rendering_engine
