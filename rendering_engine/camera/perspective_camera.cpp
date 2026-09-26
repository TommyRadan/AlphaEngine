/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <core/math/math.hpp>
#include <core/settings.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/engine.hpp>

rendering_engine::perspective_camera::perspective_camera()
{
    const runtime::engine& eng = runtime::current_engine();
    const ::settings& s = *eng.settings;

    // The projection follows the drawable the swapchain and render targets
    // are sized to, measured in pixels; the settings' logical size only
    // stands in while the window has no drawable yet (a camera created
    // before window::init). The renderer keeps the attached camera in step
    // with later resizes through set_aspect_ratio.
    const window_extent drawable = (eng.window != nullptr) ? eng.window->pixel_size() : window_extent{};

    field_of_view = s.camera.field_of_view;
    aspect_ratio = drawable_aspect_ratio(drawable.width, drawable.height, s.window.aspect_ratio());
    near_clip = 0.1f;
    far_clip = 10000.0f;
}

const core::math::mat4 rendering_engine::perspective_camera::get_projection_matrix() const
{
    if (!m_is_projection_matrix_dirty)
    {
        return m_projection;
    }

    m_projection = core::math::perspective(field_of_view, aspect_ratio, near_clip, far_clip);
    m_is_projection_matrix_dirty = false;
    return m_projection;
}

void rendering_engine::perspective_camera::set_aspect_ratio(float value)
{
    if (value == aspect_ratio)
    {
        return;
    }
    aspect_ratio = value;
    invalidate_projection_matrix();
}
