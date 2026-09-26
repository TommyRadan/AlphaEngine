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

#include <rendering_engine/camera/camera_registry.hpp>

#include <algorithm>

#include <rendering_engine/camera/camera.hpp>

namespace rendering_engine
{
    namespace
    {
        // Function-local statics so the registry outlives every camera,
        // including ones game modules create at static-init time.
        std::vector<camera*>& camera_list()
        {
            static std::vector<camera*> cameras;
            return cameras;
        }

        float& drawable_aspect_value()
        {
            static float aspect_ratio = 0.0f;
            return aspect_ratio;
        }

        // True when @p candidate beats @p incumbent under the arbitration
        // rule: higher priority, else a main tag the incumbent lacks, else
        // (walking the list in attach order) simply being the later entry.
        bool outranks(const camera& candidate, const camera* incumbent)
        {
            if (incumbent == nullptr)
            {
                return true;
            }
            if (candidate.get_priority() != incumbent->get_priority())
            {
                return candidate.get_priority() > incumbent->get_priority();
            }
            return candidate.is_main() || !incumbent->is_main();
        }
    } // namespace

    const std::vector<camera*>& registered_cameras()
    {
        return camera_list();
    }

    camera* active_camera()
    {
        camera* best = nullptr;
        for (camera* cam : camera_list())
        {
            if (cam->is_enabled() && outranks(*cam, best))
            {
                best = cam;
            }
        }
        return best;
    }

    camera* main_camera()
    {
        camera* best = nullptr;
        for (camera* cam : camera_list())
        {
            if (cam->is_enabled() && cam->is_main() && outranks(*cam, best))
            {
                best = cam;
            }
        }
        return best;
    }

    void set_drawable_aspect(float aspect_ratio)
    {
        drawable_aspect_value() = aspect_ratio > 0.0f ? aspect_ratio : 0.0f;
        if (aspect_ratio <= 0.0f)
        {
            return;
        }
        for (camera* cam : camera_list())
        {
            cam->set_aspect_ratio(aspect_ratio);
        }
    }

    float drawable_aspect()
    {
        return drawable_aspect_value();
    }

    void register_camera(camera& cam)
    {
        unregister_camera(cam);
        camera_list().push_back(&cam);
        if (const float aspect_ratio = drawable_aspect_value(); aspect_ratio > 0.0f)
        {
            cam.set_aspect_ratio(aspect_ratio);
        }
    }

    void unregister_camera(camera& cam)
    {
        auto& cameras = camera_list();
        cameras.erase(std::remove(cameras.begin(), cameras.end(), &cam), cameras.end());
    }

    bool is_registered(const camera& cam)
    {
        const auto& cameras = camera_list();
        return std::find(cameras.begin(), cameras.end(), &cam) != cameras.end();
    }
} // namespace rendering_engine
