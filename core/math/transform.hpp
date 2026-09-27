// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

#include <core/math/math.hpp>

namespace core
{
    struct transform
    {
        transform();

        void set_position(const core::math::vec3& position);
        void set_rotation(const core::math::vec3& rotation);
        void set_quaternion(const core::math::quat& rotation);
        void set_scale(const core::math::vec3& scale);

        core::math::vec3 get_position() const;
        core::math::vec3 get_rotation() const;
        core::math::quat get_quaternion() const;
        core::math::vec3 get_scale() const;

        // The local position, orientation and scale as one pose, and the
        // setter that replaces all three at once (the rotation normalised,
        // as @ref set_quaternion does). Two transforms blend as
        // @c core::math::lerp(a.get_trs(), b.get_trs(), t): position and
        // scale linearly, orientation by shortest-arc slerp.
        core::math::trs get_trs() const;
        void set_trs(const core::math::trs& pose);

        // Sets the orientation to @p rotation bit for bit, skipping the
        // normalisation @ref set_quaternion applies. For restoring a pose
        // read back from @ref get_quaternion (a scene file), where
        // normalising an already unit quaternion again can move its last
        // bit and the restored transform would not match the saved one.
        // @p rotation must already be of unit length.
        void set_quaternion_exact(const core::math::quat& rotation);

        // Orients the transform so its forward (+X) axis points from the
        // current position towards @p target, with @p up as the reference for
        // its up (+Z) axis — the engine convention (core/math/math.hpp). A
        // target on the axis of @p up (looking straight up or down) takes the
        // fallback up of core::math::reference_up; a target at the current
        // position leaves the orientation unchanged. Local space: under a
        // parent, @p target and @p up are read in the parent's frame.
        void look_at(const core::math::vec3& target, const core::math::vec3& up = core::math::world_up);

        // Basis vectors of the current orientation in the parent's frame
        // (world space for an unparented transform): the local +X, -Y and +Z
        // axes rotated by the quaternion. An identity transform faces +X
        // with +Z up and -Y on its right.
        core::math::vec3 get_forward() const;
        core::math::vec3 get_right() const;
        core::math::vec3 get_up() const;

        // Local transform matrix: translate * rotate * scale, ignoring any parent.
        core::math::mat4 get_transform_matrix() const;
        // World-space matrix. When a parent is set this is
        // @c parent->get_world_matrix() * local; otherwise it equals the local
        // matrix. The result is cached and only recomputed when this transform's
        // local components change or an ancestor's world matrix advances, so a
        // static hierarchy costs no repeated matrix multiplies.
        core::math::mat4 get_world_matrix() const;

        // A stamp of the world matrix: resolves the cache as
        // @ref get_world_matrix does and returns the stamp the result was
        // computed under, which changes exactly when the matrix is
        // recomputed. Stamps come from one process-wide counter, so a stamp
        // names one computed matrix on whichever transform holds it (a
        // copied transform carries the matrix along with it), and a caller
        // may key data derived from the matrix (the renderables' per-draw
        // block) on the stamp alone.
        uint64_t get_world_version() const;

        // Parents this transform under @p parent so @ref get_world_matrix composes
        // the parent chain. Non-owning; pass @c nullptr to detach back to world space.
        // The scene graph keeps these links in sync with its node hierarchy.
        void set_parent(const transform* parent);
        const transform* get_parent() const;

    private:
        // Bumps the local-change version and marks the local matrix dirty.
        void mark_local_dirty();

        mutable core::math::mat4 m_transform_matrix;
        mutable bool m_is_transform_matrix_dirty;

        core::math::vec3 m_position;
        core::math::vec3 m_rotation;
        core::math::quat m_quaternion;
        core::math::vec3 m_scale;

        const transform* m_parent;

        // World-matrix cache. @ref m_local_version bumps on any local change;
        // @ref m_world_version takes a fresh process-wide stamp whenever
        // @ref m_world_matrix is recomputed, so children can detect that this
        // transform moved. The two "seen" counters record the inputs the
        // cache was last built from.
        mutable core::math::mat4 m_world_matrix;
        mutable uint64_t m_local_version;
        mutable uint64_t m_world_version;
        mutable uint64_t m_seen_local_version;
        mutable uint64_t m_seen_parent_world_version;
    };
} // namespace core
