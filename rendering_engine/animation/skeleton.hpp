// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file skeleton.hpp
 * @brief The skeleton asset: a joint hierarchy with its bind pose, and the
 *        skins (joint palettes with inverse bind matrices) that deform
 *        meshes by it.
 */

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/math/math.hpp>

namespace rendering_engine
{
    /** @brief "No joint": a root's parent, a palette entry that names nothing. */
    inline constexpr std::size_t no_joint = static_cast<std::size_t>(-1);

    /** @brief One joint of a @ref skeleton. */
    struct skeleton_joint
    {
        std::string name;

        // Index of the parent joint, or @ref no_joint for a root.
        std::size_t parent{no_joint};

        // Rest pose relative to the parent (to the skeleton's model space
        // for a root): what the joint holds when no animation drives it.
        core::math::trs bind_pose{};
    };

    /**
     * @brief A joint palette: which joints deform a mesh, in the order its
     *        vertices' joint indices count them, and where each joint sat
     *        when the mesh was bound to it.
     */
    struct skeleton_skin
    {
        std::string name;

        // Palette entry -> skeleton joint (glTF's skin.joints).
        std::vector<std::size_t> joints;

        // One per palette entry: the inverse of the joint's model-space
        // matrix at bind time, taking a bind-pose vertex into the joint's
        // own space.
        std::vector<core::math::mat4> inverse_bind_matrices;
    };

    /**
     * @brief An immutable joint hierarchy, shared (as a
     *        @c shared_ptr<const skeleton>) by every animator that poses it.
     *
     * A pose is one local @ref core::math::trs per joint.
     * @ref model_matrices composes a pose down the hierarchy into
     * model-space matrices, and @ref skin_matrices turns those into the
     * joint palette a skinned mesh is drawn with. The joints may be listed
     * in any order (a glTF file's node order, for one): the constructor
     * works out an evaluation order with every parent ahead of its
     * children. A parent index that is out of range, names the joint itself
     * or closes a cycle is logged and the joint made a root instead; a skin
     * entry naming no joint poses its vertices by the identity, and missing
     * inverse bind matrices are identity (glTF's default).
     */
    struct skeleton
    {
        skeleton() = default;
        explicit skeleton(std::vector<skeleton_joint> joints, std::vector<skeleton_skin> skins = {});

        const std::vector<skeleton_joint>& joints() const noexcept;
        const std::vector<skeleton_skin>& skins() const noexcept;
        std::size_t joint_count() const noexcept;

        /** @brief The first joint named @p name, or @ref no_joint. */
        std::size_t find_joint(std::string_view name) const noexcept;

        /** @brief Every joint's @ref skeleton_joint::bind_pose, index-aligned with @ref joints. */
        std::vector<core::math::trs> bind_pose() const;

        /**
         * @brief Model-space matrix of every joint under the local @p pose:
         *        its own @c T * R * S composed with its ancestors'.
         *
         * @p pose is index-aligned with @ref joints; a joint past its end
         * uses its bind pose. @p out is resized to @ref joint_count.
         */
        void model_matrices(std::span<const core::math::trs> pose, std::vector<core::math::mat4>& out) const;

        /**
         * @brief The palette of skin @p skin for a pose whose
         *        @ref model_matrices are @p model.
         *
         * Entry i is @c inverse(model[mesh_joint]) * model[joint_i] *
         * inverse_bind_i: the joint's motion away from its bind pose,
         * expressed in the space of the joint the mesh hangs from, so the
         * mesh's own world matrix is applied on top unchanged. Pass
         * @ref no_joint for @p mesh_joint when the mesh sits in the
         * skeleton's model space. @p out is resized to the palette size
         * (empty for an unknown @p skin).
         */
        void skin_matrices(std::size_t skin,
                           std::span<const core::math::mat4> model,
                           std::size_t mesh_joint,
                           std::vector<core::math::mat4>& out) const;

    private:
        std::vector<skeleton_joint> m_joints;
        std::vector<skeleton_skin> m_skins;

        // Joint indices with every parent ahead of its children.
        std::vector<std::size_t> m_order;
    };
} // namespace rendering_engine
