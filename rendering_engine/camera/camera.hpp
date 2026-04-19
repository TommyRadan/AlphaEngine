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

#pragma once

#include <glm/glm.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct camera
    {
        camera();
        virtual ~camera() = default;

        void attach();
        void detach();

        // Non-owning: points to the currently attached camera, or nullptr.
        static camera* get_current_camera();

        rendering_engine::util::transform transform;

        void invalidate_view_matrix();
        const glm::mat4 get_view_matrix() const;

        void invalidate_projection_matrix();
        virtual const glm::mat4 get_projection_matrix() const = 0;

    protected:
        mutable glm::mat4 m_view_matrix;
        mutable bool m_is_view_matrix_dirty;
        mutable glm::mat4 m_projection;
        mutable bool m_is_projection_matrix_dirty;

        // Non-owning observer: lifetime managed elsewhere (e.g. by the creator of the camera).
        static camera* m_current_camera;
    };
} // namespace rendering_engine
