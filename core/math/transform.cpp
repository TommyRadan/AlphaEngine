// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <atomic>

#include <core/math/math.hpp>
#include <core/math/transform.hpp>

namespace
{
    // Source of world-matrix stamps (see transform::get_world_version).
    // Starts at 1: a version of 0 means "never computed".
    std::atomic<uint64_t> next_world_version{1};

    uint64_t take_world_version()
    {
        return next_world_version.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace

core::transform::transform()
    : m_is_transform_matrix_dirty{true}, m_position{0.0f, 0.0f, 0.0f}, m_rotation{0.0f, 0.0f, 0.0f}, m_quaternion{},
      m_scale{1.0f, 1.0f, 1.0f}, m_parent{nullptr}, m_local_version{1}, m_world_version{0}, m_seen_local_version{0},
      m_seen_parent_world_version{0}
{
}

void core::transform::mark_local_dirty()
{
    m_is_transform_matrix_dirty = true;
    ++m_local_version;
}

void core::transform::set_position(const core::math::vec3& position)
{
    m_position = position;
    mark_local_dirty();
}

void core::transform::set_rotation(const core::math::vec3& rotation)
{
    m_rotation = rotation;
    m_quaternion = core::math::quat_from_euler(rotation);
    mark_local_dirty();
}

void core::transform::set_quaternion(const core::math::quat& rotation)
{
    m_quaternion = core::math::normalize(rotation);
    m_rotation = core::math::euler_from_quat(m_quaternion);
    mark_local_dirty();
}

void core::transform::set_scale(const core::math::vec3& scale)
{
    m_scale = scale;
    mark_local_dirty();
}

core::math::vec3 core::transform::get_position() const
{
    return m_position;
}

core::math::vec3 core::transform::get_rotation() const
{
    return m_rotation;
}

core::math::quat core::transform::get_quaternion() const
{
    return m_quaternion;
}

core::math::vec3 core::transform::get_scale() const
{
    return m_scale;
}

core::math::trs core::transform::get_trs() const
{
    return core::math::trs{m_position, m_quaternion, m_scale};
}

void core::transform::set_trs(const core::math::trs& pose)
{
    m_position = pose.translation;
    m_quaternion = core::math::normalize(pose.rotation);
    m_rotation = core::math::euler_from_quat(m_quaternion);
    m_scale = pose.scale;
    mark_local_dirty();
}

void core::transform::set_quaternion_exact(const core::math::quat& rotation)
{
    m_quaternion = rotation;
    m_rotation = core::math::euler_from_quat(m_quaternion);
    mark_local_dirty();
}

void core::transform::look_at(const core::math::vec3& target, const core::math::vec3& up)
{
    const core::math::vec3 direction = target - m_position;
    if (core::math::length(direction) <= 0.0f)
    {
        return;
    }

    // reference_up substitutes a usable up when the direction runs along
    // it, so the frame is always well-defined (never a NaN quaternion).
    m_quaternion = core::math::quat_look_at(direction, core::math::reference_up(direction, up));
    m_rotation = core::math::euler_from_quat(m_quaternion);
    mark_local_dirty();
}

core::math::vec3 core::transform::get_forward() const
{
    return core::math::normalize(m_quaternion * core::math::world_forward);
}

core::math::vec3 core::transform::get_right() const
{
    return core::math::normalize(m_quaternion * core::math::world_right);
}

core::math::vec3 core::transform::get_up() const
{
    return core::math::normalize(m_quaternion * core::math::world_up);
}

core::math::mat4 core::transform::get_transform_matrix() const
{
    if (!m_is_transform_matrix_dirty)
    {
        return m_transform_matrix;
    }

    using core::math::mat4;
    using core::math::scale;
    using core::math::to_mat4;
    using core::math::translate;

    mat4 pos_matrix = translate(m_position);
    mat4 rot_matrix = to_mat4(m_quaternion);
    mat4 scale_matrix = scale(m_scale);

    m_transform_matrix = pos_matrix * rot_matrix * scale_matrix;
    m_is_transform_matrix_dirty = false;
    return m_transform_matrix;
}

core::math::mat4 core::transform::get_world_matrix() const
{
    const core::math::mat4 local = get_transform_matrix();

    if (m_parent == nullptr)
    {
        // Recompute only when the local transform changed (or the cache was
        // last built against a parent, i.e. just detached).
        if (m_world_version == 0 || m_seen_local_version != m_local_version || m_seen_parent_world_version != 0)
        {
            m_world_matrix = local;
            m_seen_local_version = m_local_version;
            m_seen_parent_world_version = 0;
            m_world_version = take_world_version();
        }
        return m_world_matrix;
    }

    // Resolve the parent first; this advances the parent's world version if any
    // ancestor moved, so the comparison below catches the change.
    const core::math::mat4 parent_world = m_parent->get_world_matrix();
    if (m_world_version == 0 || m_seen_local_version != m_local_version ||
        m_seen_parent_world_version != m_parent->m_world_version)
    {
        m_world_matrix = parent_world * local;
        m_seen_local_version = m_local_version;
        m_seen_parent_world_version = m_parent->m_world_version;
        m_world_version = take_world_version();
    }
    return m_world_matrix;
}

uint64_t core::transform::get_world_version() const
{
    // Resolving the matrix brings the stamp up to date with the inputs.
    (void)get_world_matrix();
    return m_world_version;
}

void core::transform::set_parent(const transform* parent)
{
    m_parent = parent;
    // Re-parenting changes the world inputs; force a world recompute next query.
    ++m_local_version;
}

const core::transform* core::transform::get_parent() const
{
    return m_parent;
}
