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

#include <cstdint>

#include <infrastructure/math/math.hpp>

namespace rendering_engine::util
{
    struct transform
    {
        transform();

        void set_position(const infrastructure::math::vec3& position);
        void set_rotation(const infrastructure::math::vec3& rotation);
        void set_quaternion(const infrastructure::math::quat& rotation);
        void set_scale(const infrastructure::math::vec3& scale);

        infrastructure::math::vec3 get_position() const;
        infrastructure::math::vec3 get_rotation() const;
        infrastructure::math::quat get_quaternion() const;
        infrastructure::math::vec3 get_scale() const;

        // Orients the transform so its forward (-Z) axis points from the current position towards @p target.
        void look_at(const infrastructure::math::vec3& target,
                     const infrastructure::math::vec3& up = infrastructure::math::vec3{0.0f, 1.0f, 0.0f});

        // Basis vectors of the current orientation, in world space.
        infrastructure::math::vec3 get_forward() const;
        infrastructure::math::vec3 get_right() const;
        infrastructure::math::vec3 get_up() const;

        // Local transform matrix: translate * rotate * scale, ignoring any parent.
        infrastructure::math::mat4 get_transform_matrix() const;
        // World-space matrix. When a parent is set this is
        // @c parent->get_world_matrix() * local; otherwise it equals the local
        // matrix. The result is cached and only recomputed when this transform's
        // local components change or an ancestor's world matrix advances, so a
        // static hierarchy costs no repeated matrix multiplies.
        infrastructure::math::mat4 get_world_matrix() const;

        // Parents this transform under @p parent so @ref get_world_matrix composes
        // the parent chain. Non-owning; pass @c nullptr to detach back to world space.
        // The scene graph keeps these links in sync with its node hierarchy.
        void set_parent(const transform* parent);
        const transform* get_parent() const;

    private:
        // Bumps the local-change version and marks the local matrix dirty.
        void mark_local_dirty();

        mutable infrastructure::math::mat4 m_transform_matrix;
        mutable bool m_is_transform_matrix_dirty;

        infrastructure::math::vec3 m_position;
        infrastructure::math::vec3 m_rotation;
        infrastructure::math::quat m_quaternion;
        infrastructure::math::vec3 m_scale;

        const transform* m_parent;

        // World-matrix cache. @ref m_local_version bumps on any local change;
        // @ref m_world_version bumps whenever @ref m_world_matrix is recomputed,
        // so children can detect that this transform moved. The two "seen"
        // counters record the inputs the cache was last built from.
        mutable infrastructure::math::mat4 m_world_matrix;
        mutable uint64_t m_local_version;
        mutable uint64_t m_world_version;
        mutable uint64_t m_seen_local_version;
        mutable uint64_t m_seen_parent_world_version;
    };
} // namespace rendering_engine::util
