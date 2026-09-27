// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio_source_component.hpp
 * @brief Component that plays a clip at its node's position.
 */

#pragma once

#include <memory>

#include <core/audio/audio_types.hpp>
#include <core/math/vec3.hpp>

namespace core
{
    struct audio_clip;
}

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a sound source: a clip plus how it plays.
     *
     * Wraps one @c core::audio voice (@ref core::audio_voice_id), started by
     * @ref play (directly, or automatically on attach when
     * @ref config::play_on_attach is set) and stopped by @ref stop or
     * @ref on_destroy. @ref on_update tracks the node's world position, which
     * a @ref config::spatial source's voice is mixed against every tick
     * (distance attenuation and stereo panning from the active listener, see
     * @c audio_listener_component); a non-spatial source ignores the node's
     * position and plays at a fixed 2D @ref config::pan instead.
     *
     * The voice handle needs no heap indirection for address stability (it
     * is a plain value, like @c core::pool_handle), so unlike
     * @c camera_component / @c mesh_component this component owns no
     * @c unique_ptr.
     */
    struct audio_source_component
    {
        /** @brief How a source plays; see the field comments. */
        struct config
        {
            std::shared_ptr<core::audio_clip> clip;
            float gain{1.0f};
            float pitch{1.0f}; // playback-rate multiplier; 1 = the clip's original speed and pitch
            bool loop{false};
            bool play_on_attach{false};

            float pan{0.0f}; // -1 (left) .. 1 (right); only used when !spatial

            bool spatial{false};
            float min_distance{1.0f};  // full volume at or inside this distance
            float max_distance{50.0f}; // silent at or beyond this distance
            float rolloff{1.0f};       // shapes the falloff curve between the two distances; 1 is linear
        };

        /** @brief Empty component — plays nothing until @ref set_clip / a new @ref config is applied. */
        audio_source_component() = default;

        explicit audio_source_component(config cfg);

        /** @brief Records the node's starting position and, if @ref config::play_on_attach, starts playback. */
        void on_attach(node& owner);

        /** @brief Tracks the node's world position for a spatial source's next mix. */
        void on_update(node& owner);

        /** @brief Stops playback. */
        void on_destroy();

        /**
         * @brief Pauses playback when the owning node is disabled and
         *        resumes it when re-enabled. A source with nothing playing
         *        is unaffected either way.
         *
         * Called by @ref node::set_active.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief A new component with the same @ref config, for @c scene::clone.
         *
         * The clone starts with nothing playing (its own @c on_attach applies
         * @ref config::play_on_attach again) rather than sharing this
         * source's live voice.
         */
        audio_source_component clone() const;

        /** @brief Starts (or restarts) playback of @ref config::clip. No-op if it has none or none is set. */
        void play();

        /** @brief Stops playback. No-op if nothing is playing. */
        void stop();

        /** @brief Whether this source's voice is still playing (or paused). */
        bool is_playing() const;

        void set_clip(std::shared_ptr<core::audio_clip> clip) noexcept
        {
            m_config.clip = std::move(clip);
        }
        const std::shared_ptr<core::audio_clip>& clip() const noexcept
        {
            return m_config.clip;
        }

        /** @brief Changes gain, live if a voice is currently playing. */
        void set_gain(float gain);
        float gain() const noexcept
        {
            return m_config.gain;
        }

        /** @brief Changes the playback-rate multiplier, live if a voice is currently playing. */
        void set_pitch(float pitch);
        float pitch() const noexcept
        {
            return m_config.pitch;
        }

        /** @brief Changes the 2D pan (unused while @ref is_spatial), live if a voice is currently playing. */
        void set_pan(float pan);
        float pan() const noexcept
        {
            return m_config.pan;
        }

        void set_loop(bool loop) noexcept
        {
            m_config.loop = loop;
        }
        bool loop() const noexcept
        {
            return m_config.loop;
        }

        bool is_spatial() const noexcept
        {
            return m_config.spatial;
        }

        /** @brief Changes the distance-attenuation curve, live if a voice is currently playing. */
        void set_attenuation(float min_distance, float max_distance, float rolloff);

        /** @brief Every setting at once, as the component was built with it and changed since. */
        const config& configuration() const noexcept
        {
            return m_config;
        }

    private:
        config m_config;
        core::audio_voice_id m_voice;
        core::math::vec3 m_position;
    };
} // namespace runtime
