// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>

namespace rendering_engine
{
    struct render_world;

    // Discriminator for the concrete light kind. The scene pass reads
    // it to route each registered light into the matching slot of the
    // packed lights UBO without a dynamic_cast.
    enum class light_type
    {
        ambient,
        directional,
        point,
        spot,
    };

    // Base for every light source. Carries the colour / intensity every
    // kind shares; concrete subtypes add their own geometry (direction,
    // position, attenuation). A light joins a @ref render_world's light
    // list through @ref attach — whoever creates it (a component bridge,
    // a game module) registers it explicitly, the light does not
    // self-register — and is taken out of it by @ref detach or the
    // destructor. set_enabled(false) takes it out of the attached world
    // without detaching or destroying it; re-enabling appends it to the
    // back of the same world's list, so its position in the packed light
    // arrays may differ from before. Lights start enabled and unattached.
    // Main-thread-only, like the rest of the engine — no synchronization.
    struct light
    {
        explicit light(light_type type);
        virtual ~light();

        light(const light&) = delete;
        light& operator=(const light&) = delete;

        light_type type() const noexcept;

        /**
         * @brief Adds the light to @p world's light list.
         *
         * Detaches from any world it is currently attached to first, so
         * re-attaching (to the same or a different world) moves it to the
         * back of @p world's list. A disabled light is recorded as
         * attached but not added to the list until it is re-enabled.
         */
        void attach(render_world& world);

        /** @brief Removes the light from the world it is attached to. No-op when not attached. */
        void detach();

        /** @brief Whether the light is attached to a world right now. */
        bool is_attached() const noexcept;

        // Adds the light to / removes it from the world it is attached to.
        // A disabled light keeps its state but neither lights the scene nor
        // casts a shadow, exactly as if it did not exist. Re-enabling appends
        // it to its world's list, so its position in the packed light arrays
        // may differ from before. Lights start enabled. A no-op while
        // unattached beyond recording the flag for the next @ref attach.
        void set_enabled(bool enabled);
        bool is_enabled() const noexcept;

        // Linear RGB radiance. Multiplied by @ref intensity before
        // upload.
        core::math::vec3 color{1.0f, 1.0f, 1.0f};

        // Scalar multiplier on @ref color.
        float intensity{1.0f};

    private:
        light_type m_type;
        bool m_enabled{true};
        render_world* m_world{nullptr};
    };
} // namespace rendering_engine
