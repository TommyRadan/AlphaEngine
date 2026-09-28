// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file component_types.hpp
 * @brief Registration of the built-in components with the type registry.
 */

#pragma once

namespace runtime
{
    struct type_registry;

    /**
     * @brief Registers the built-in components, and the material
     *        descriptions a mesh component's material is saved as, with
     *        @p registry under the names scene files use.
     *
     * Called once, by @ref default_type_registry when it is first used.
     */
    void register_component_types(type_registry& registry);
} // namespace runtime
