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
 * @file light_component.hpp
 * @brief Component that binds a light source to a node.
 */

#pragma once

#include <memory>

#include <rendering_engine/lighting/light.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a light source.
     *
     * Owns a @ref rendering_engine::light (any kind) on the heap. A light adds
     * itself to the renderer's light registry in its own constructor and
     * removes itself in its destructor, so this component needs no attach /
     * detach plumbing — building it registers the light, destroying the node
     * (or removing the component) frees and unregisters it. The light lives
     * behind a @c unique_ptr so the registry's back-pointer stays valid as the
     * component is relocated within its pool.
     *
     * @ref on_update keeps the light's spatial fields in step with the node: a
     * point light's position follows the node's world translation, and a
     * directional light's direction follows the node's world forward (-Z). An
     * ambient light has no spatial term and is left untouched.
     */
    struct light_component
    {
        /** @brief Empty component — owns no light. */
        light_component() = default;

        /** @brief Takes ownership of @p light (already registered on construction). */
        explicit light_component(std::unique_ptr<rendering_engine::light> light);

        /** @brief Syncs the light's position / direction from @p owner's world transform. */
        void on_update(node& owner);

        /** @brief The owned light, or @c nullptr for an empty component. */
        rendering_engine::light* get() const noexcept
        {
            return m_light.get();
        }

    private:
        std::unique_ptr<rendering_engine::light> m_light;
    };
} // namespace runtime
