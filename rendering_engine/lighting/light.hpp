// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    // Base for the settings of every light source: the colour / intensity
    // every kind shares and the enabled flag; concrete subtypes add their
    // own (attenuation, cone, shadow casting). A light carries no pose and
    // knows no world: whoever owns it (a runtime::light_component) keeps a
    // @ref light_proxy in a @ref render_world, places it from its node and
    // copies these settings into it (@ref copy_light_settings) before each
    // frame. Lights start enabled. Main-thread-only, like the rest of the
    // engine — no synchronization.
    struct light
    {
        explicit light(light_type type);
        virtual ~light() = default;

        light(const light&) = delete;
        light& operator=(const light&) = delete;

        light_type type() const noexcept;

        // Whether the light takes part in the frame. A disabled light keeps
        // its settings but neither lights the scene nor casts a shadow,
        // exactly as if it did not exist; its owner takes its proxy out of
        // the world's enabled list (@ref render_world::set_light_enabled)
        // and, on re-enabling, puts it back at the end, so its position in
        // the packed light arrays may differ from before.
        void set_enabled(bool enabled) noexcept;
        bool is_enabled() const noexcept;

        // Linear RGB radiance. Multiplied by @ref intensity before
        // upload.
        core::math::vec3 color{1.0f, 1.0f, 1.0f};

        // Scalar multiplier on @ref color.
        float intensity{1.0f};

    private:
        light_type m_type;
        bool m_enabled{true};
    };

    // Copies @p settings' kind, emission, falloff, cone angles and shadow flag
    // into @p out, leaving its position and direction untouched.
    void copy_light_settings(const light& settings, light_proxy& out);

    // Places @p out by @p world, a light's world matrix: a point or spot
    // light at its translation, a directional or spot light travelling
    // along its forward (+X) axis in the engine convention (core/math/math.hpp).
    // An axis the matrix's scale collapses to zero carries no direction, so
    // @p out keeps the direction it already has rather than taking a NaN. An
    // ambient light has no spatial term and is left untouched.
    void place_light(const core::math::mat4& world, light_proxy& out);
} // namespace rendering_engine
