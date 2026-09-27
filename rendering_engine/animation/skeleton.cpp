// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/animation/skeleton.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>

#include <core/log.hpp>

namespace rendering_engine
{
    skeleton::skeleton(std::vector<skeleton_joint> joints, std::vector<skeleton_skin> skins)
        : m_joints{std::move(joints)}, m_skins{std::move(skins)}
    {
        const std::size_t count = m_joints.size();

        for (std::size_t j = 0; j < count; ++j)
        {
            skeleton_joint& joint = m_joints[j];
            if (joint.parent != no_joint && (joint.parent >= count || joint.parent == j))
            {
                LOG_WRN(
                    "skeleton: joint '%s' names an invalid parent %zu; made a root", joint.name.c_str(), joint.parent);
                joint.parent = no_joint;
            }
        }

        // Walk up from every joint, marking the chain in progress. Reaching
        // a joint that is still in progress means the chain has closed a
        // cycle through its last link, which is cut (that joint becomes a
        // root); joints that merely lead into the cycle keep their parents.
        constexpr uint8_t unvisited = 0;
        constexpr uint8_t in_progress = 1;
        constexpr uint8_t done = 2;
        std::vector<uint8_t> state(count, unvisited);
        std::vector<std::size_t> chain;
        for (std::size_t j = 0; j < count; ++j)
        {
            chain.clear();
            std::size_t current = j;
            while (current != no_joint && state[current] == unvisited)
            {
                state[current] = in_progress;
                chain.push_back(current);
                current = m_joints[current].parent;
            }
            if (current != no_joint && state[current] == in_progress)
            {
                LOG_WRN("skeleton: joint '%s' closes a parent cycle; made a root", m_joints[chain.back()].name.c_str());
                m_joints[chain.back()].parent = no_joint;
            }
            for (const std::size_t visited : chain)
            {
                state[visited] = done;
            }
        }

        // Depth of each joint below its root, now that every chain ends at
        // one.
        std::vector<std::size_t> depth(count, 0);
        for (std::size_t j = 0; j < count; ++j)
        {
            std::size_t current = m_joints[j].parent;
            while (current != no_joint)
            {
                ++depth[j];
                current = m_joints[current].parent;
            }
        }

        // Sorting by depth puts every parent (strictly shallower) ahead of
        // its children; the stable sort keeps siblings in file order.
        m_order.resize(count);
        std::iota(m_order.begin(), m_order.end(), std::size_t{0});
        std::stable_sort(
            m_order.begin(), m_order.end(), [&depth](std::size_t a, std::size_t b) { return depth[a] < depth[b]; });

        for (skeleton_skin& skin : m_skins)
        {
            for (std::size_t& joint : skin.joints)
            {
                if (joint != no_joint && joint >= count)
                {
                    LOG_WRN("skeleton: skin '%s' names joint %zu of %zu; it poses its vertices by the identity",
                            skin.name.c_str(),
                            joint,
                            count);
                    joint = no_joint;
                }
            }
            if (skin.inverse_bind_matrices.size() != skin.joints.size())
            {
                if (!skin.inverse_bind_matrices.empty())
                {
                    LOG_WRN("skeleton: skin '%s' has %zu inverse bind matrices for %zu joints; the rest are identity",
                            skin.name.c_str(),
                            skin.inverse_bind_matrices.size(),
                            skin.joints.size());
                }
                skin.inverse_bind_matrices.resize(skin.joints.size(), core::math::mat4{});
            }
        }
    }

    const std::vector<skeleton_joint>& skeleton::joints() const noexcept
    {
        return m_joints;
    }

    const std::vector<skeleton_skin>& skeleton::skins() const noexcept
    {
        return m_skins;
    }

    std::size_t skeleton::joint_count() const noexcept
    {
        return m_joints.size();
    }

    std::size_t skeleton::find_joint(std::string_view name) const noexcept
    {
        for (std::size_t j = 0; j < m_joints.size(); ++j)
        {
            if (m_joints[j].name == name)
            {
                return j;
            }
        }
        return no_joint;
    }

    std::vector<core::math::trs> skeleton::bind_pose() const
    {
        std::vector<core::math::trs> pose;
        pose.reserve(m_joints.size());
        for (const skeleton_joint& joint : m_joints)
        {
            pose.push_back(joint.bind_pose);
        }
        return pose;
    }

    void skeleton::model_matrices(std::span<const core::math::trs> pose, std::vector<core::math::mat4>& out) const
    {
        out.resize(m_joints.size());
        for (const std::size_t j : m_order)
        {
            const core::math::trs& local = j < pose.size() ? pose[j] : m_joints[j].bind_pose;
            const core::math::mat4 matrix = core::math::to_mat4(local);
            const std::size_t parent = m_joints[j].parent;
            out[j] = parent == no_joint ? matrix : out[parent] * matrix;
        }
    }

    void skeleton::skin_matrices(std::size_t skin,
                                 std::span<const core::math::mat4> model,
                                 std::size_t mesh_joint,
                                 std::vector<core::math::mat4>& out) const
    {
        if (skin >= m_skins.size())
        {
            out.clear();
            return;
        }
        const skeleton_skin& palette = m_skins[skin];
        const core::math::mat4 to_mesh =
            mesh_joint < model.size() ? core::math::inverse(model[mesh_joint]) : core::math::mat4{};

        out.resize(palette.joints.size());
        for (std::size_t i = 0; i < palette.joints.size(); ++i)
        {
            const std::size_t joint = palette.joints[i];
            if (joint >= model.size())
            {
                out[i] = core::math::mat4{};
                continue;
            }
            out[i] = to_mesh * model[joint] * palette.inverse_bind_matrices[i];
        }
    }
} // namespace rendering_engine
