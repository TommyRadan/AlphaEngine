// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file settings_registration.hpp
 * @brief Registers every rendering-engine-owned settings section (`graphics`, `camera`, `shadows`, `post`)
 *        against a @c core::settings_registry.
 */

#pragma once

namespace core
{
    class settings_registry;
}

namespace rendering_engine
{
    struct graphics_settings;
    struct camera_settings;
    struct shadow_settings;
    struct post_process_settings;

    /**
     * @brief Registers @ref graphics_settings, @ref camera_settings and @ref shadow_settings against
     *        @p registry. Called once, before @c core::load_settings resolves it; @ref register_post_settings
     *        is registered separately (and later) so post-processing's many options print as their own
     *        trailing `--help` block, matching the historical command-line layout.
     */
    void register_settings(core::settings_registry& registry,
                           graphics_settings& graphics,
                           camera_settings& camera,
                           shadow_settings& shadows);

    /** @brief Registers @ref post_process_settings against @p registry. See @ref register_settings. */
    void register_post_settings(core::settings_registry& registry, post_process_settings& post);
} // namespace rendering_engine
