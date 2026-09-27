/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rendering_engine/lighting/lights_ubo.hpp>

#include <algorithm>
#include <cmath>

#include <core/math/math.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/lighting/spot_light.hpp>

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

    void pack_lights(const std::vector<light*>& lights, gpu_lights& out)
    {
        out = gpu_lights{};

        core::math::vec3 ambient{0.0f, 0.0f, 0.0f};
        uint32_t directional_count = 0;
        uint32_t point_count = 0;
        uint32_t spot_count = 0;

        for (const light* l : lights)
        {
            switch (l->type())
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
                const auto* dl = static_cast<const directional_light*>(l);
                gpu_directional_light& slot = out.directional[directional_count];
                write_vec3(slot.direction, core::math::normalize(dl->direction), 0.0f);
                write_vec3(slot.color, dl->color * dl->intensity, 0.0f);
                ++directional_count;
                break;
            }
            case light_type::point:
            {
                if (point_count >= max_point_lights)
                {
                    break;
                }
                const auto* pl = static_cast<const point_light*>(l);
                gpu_point_light& slot = out.point[point_count];
                write_vec3(slot.position, pl->position, 0.0f);
                write_vec3(slot.color, pl->color * pl->intensity, 0.0f);
                slot.attenuation[0] = pl->range;
                slot.attenuation[1] = pl->constant_attenuation;
                slot.attenuation[2] = pl->linear_attenuation;
                slot.attenuation[3] = pl->quadratic_attenuation;
                ++point_count;
                break;
            }
            case light_type::spot:
            {
                if (spot_count >= max_spot_lights)
                {
                    break;
                }
                const auto* sl = static_cast<const spot_light*>(l);
                gpu_spot_light& slot = out.spot[spot_count];
                write_vec3(slot.position, sl->position, 0.0f);
                write_vec3(slot.direction, core::math::normalize(sl->direction), 0.0f);
                write_vec3(slot.color, sl->color * sl->intensity, 0.0f);
                slot.attenuation[0] = sl->range;
                slot.attenuation[1] = sl->constant_attenuation;
                slot.attenuation[2] = sl->linear_attenuation;
                slot.attenuation[3] = sl->quadratic_attenuation;
                // The inner cone can never be wider than the outer one, so
                // the shader's smoothstep always runs in the right order.
                const float outer = sl->outer_angle;
                const float inner = std::min(sl->inner_angle, outer);
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
