// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/drawn_bounds.hpp>

#include <runtime/components/mesh_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/node.hpp>

namespace runtime
{
    std::optional<core::math::aabb> drawn_bounds(node& owner)
    {
        if (const mesh_component* mesh = owner.get_component<mesh_component>(); mesh != nullptr)
        {
            if (const std::optional<core::math::aabb> bounds = mesh->local_bounds(); bounds.has_value())
            {
                return bounds;
            }
        }
        const renderable_component* drawn = owner.get_component<renderable_component>();
        core::math::aabb bounds;
        if (drawn != nullptr && drawn->get() != nullptr && drawn->get()->local_bounds(bounds))
        {
            return bounds;
        }
        return std::nullopt;
    }
} // namespace runtime
