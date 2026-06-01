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

/**
 * @file camera_component.hpp
 * @brief Component that binds a camera to a node.
 */

#pragma once

#include <memory>

#include <rendering_engine/camera/camera.hpp>

namespace scene_graph
{
    struct node;

    /**
     * @brief Gives a node a camera.
     *
     * Owns a @ref rendering_engine::camera (perspective or orthographic) on the
     * heap. @ref on_attach makes it the renderer's active camera; @ref on_destroy
     * releases it if it is still active, so destroying the node (or removing the
     * component) detaches cleanly. @ref on_update drives the camera's position
     * from the node's world translation, so a camera parented under an animated
     * node follows it — a turntable rig, a chase cam — without per-frame glue in
     * the game module. Orientation is left to the camera (e.g. via
     * @c camera::look_at), which the owner can set up after attaching.
     *
     * The camera lives behind a @c unique_ptr so its address — held by the
     * renderer's active-camera pointer — survives the component being relocated
     * within its pool.
     */
    struct camera_component
    {
        /** @brief Empty component — owns no camera. */
        camera_component() = default;

        /** @brief Takes ownership of @p camera. */
        explicit camera_component(std::unique_ptr<rendering_engine::camera> camera);

        /** @brief Makes the owned camera the renderer's active camera. */
        void on_attach(node& owner);

        /** @brief Detaches the owned camera if it is still the active one. */
        void on_destroy();

        /** @brief Drives the camera's position from @p owner's world translation. */
        void on_update(node& owner);

        /** @brief The owned camera, or @c nullptr for an empty component. */
        rendering_engine::camera* get() const noexcept
        {
            return m_camera.get();
        }

    private:
        std::unique_ptr<rendering_engine::camera> m_camera;
        bool m_attached{false};
    };
} // namespace scene_graph
