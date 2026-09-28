// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio.hpp
 * @brief Audio subsystem: software mixer, voices, listeners and clip cache,
 *        over a platform playback device and decoder.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/audio/audio_backend.hpp>
#include <core/audio/audio_types.hpp>
#include <core/math/vec3.hpp>
#include <core/spsc_queue.hpp>

namespace core
{
    struct audio_clip;

    /**
     * @brief Owns a fixed-voice-cap software mixer and a weak-ref cache of
     *        decoded @ref audio_clip assets, and feeds the playback device.
     *
     * Owned by @ref runtime::engine with the usual @c init / @c quit shape.
     * The device and the decoder are platform services handed in at
     * construction (@ref audio_output, @ref audio_decoder; the engine
     * passes the platform module's, from platform/audio_device.hpp), so
     * this subsystem is plain C++. @ref init opens the default playback device:
     * every clip is decoded and converted once, up front, to one fixed
     * mixer format (@ref k_mixer_sample_rate stereo float,
     * @ref k_mixer_channels channels), so a device that requests a
     * different native format is handled by the output's own conversion,
     * invisibly to every voice.
     *
     * **Threads.** The mix runs on the audio device's own thread: the device
     * pulls every block it plays through the render function @ref init
     * hands it, and the voices, the listener pose and the time scale the mix
     * reads belong to that thread alone. The public API is main-thread only
     * and never waits for the mixer: a call that changes a voice
     * (@ref play, @ref stop, @ref set_gain, ...) or the time scale, and the
     * frame's listener pose (@ref update), is posted to a lock-free command
     * queue that the mix drains before every block, and what the main thread
     * has to answer by itself — which voice slots are taken, @ref is_playing —
     * it keeps in its own mirror of the slots. A voice that reaches its end
     * is reported back through a per-slot atomic, so its slot frees up once
     * the mix has let go of it, and a clip handed to the mix is kept alive
     * until the mix has applied the command that stops its voice. A long
     * frame on the main thread therefore neither starves the device nor
     * costs frame time: changes take effect from the next block the device
     * asks for.
     *
     * **Time scale.** Voices play at the game's time scale
     * (@ref set_time_scale): every voice's playback rate — its pitch — is
     * multiplied by it, so slowed-down game time sounds slowed down, and at
     * 0 (paused) every voice holds its place and is silent while the device
     * keeps being fed.
     *
     * **Graceful degradation.** A container or CI runner with no usable
     * playback device is expected, not exceptional: @ref init logs one
     * warning and leaves the subsystem with no device. Every playback call
     * still runs — clips still decode, @ref play still allocates and
     * advances a voice, listeners still attach and arbitrate — so game code
     * needs no "is audio available" branch of its own. With no device there
     * is no device thread either: @ref update applies the queued commands
     * and advances the voices itself, by the frame's real time, so nothing
     * is ever heard and nothing hangs or throws. Only WAV is decoded.
     */
    struct audio
    {
        /**
         * @param output  The playback device @ref init opens; null leaves the
         *                subsystem without one (see "Graceful degradation").
         * @param decoder Decodes the files @ref load_clip reads; null makes
         *                every load fail.
         */
        audio(std::unique_ptr<audio_output> output, std::unique_ptr<audio_decoder> decoder);
        ~audio();

        audio(const audio&) = delete;
        audio& operator=(const audio&) = delete;
        audio(audio&&) = delete;
        audio& operator=(audio&&) = delete;

        /**
         * @brief Opens the default playback device, which from then on pulls
         *        the mix on its own thread. Never throws: a missing or
         *        unusable device is logged at @c WRN and leaves
         *        @ref is_available false.
         */
        void init();

        /** @brief Closes the device (if one was opened) and drops every voice, listener and cached clip. */
        void quit();

        /** @brief Whether a playback device is open; false means every call below is a harmless no-op. */
        bool is_available() const noexcept
        {
            return m_available;
        }

        /**
         * @brief Returns the clip decoded from @p path, decoding it on a miss.
         *
         * Reads @p path through @c core::default_vfs, decodes it as WAV
         * (@ref audio_decoder::decode_wav) and converts it to the mixer format once; the
         * result is cached (weak-ref, like @c texture_asset) by the VFS's
         * canonical identity, so a clip requested twice is decoded once and
         * freed as soon as its last handle drops. Unlike
         * @c asset_cache::load_texture this does not throw: a missing file
         * or a decode failure is logged and returns @c nullptr, which
         * @ref play (and @ref play_one_shot) already treat as "nothing to
         * play" — the safer default for a subsystem that must degrade
         * gracefully rather than take engine start-up down with it. Runs
         * whether or not a playback device is open: decoding needs no
         * device.
         */
        std::shared_ptr<audio_clip> load_clip(const std::filesystem::path& path);

        /**
         * @brief Starts playing @p params on a free voice.
         * @return A handle to the new voice, or an invalid one when
         *         @c params.clip is null/empty or every voice is busy (the
         *         request is dropped and logged once; there is no voice
         *         stealing).
         */
        audio_voice_id play(const audio_play_params& params);

        /** @brief Fire-and-forget 2D playback: @ref play with no loop and no handle kept. */
        void play_one_shot(const std::shared_ptr<audio_clip>& clip, float gain = 1.0f, float pan = 0.0f);

        /** @brief Stops and frees the voice named by @p id. No-op if invalid/already finished. */
        void stop(audio_voice_id id);

        /** @brief Pauses (true) or resumes (false) the voice named by @p id. No-op if invalid. */
        void set_paused(audio_voice_id id, bool paused);

        /**
         * @brief Whether @p id names a voice still playing (or paused). A
         *        voice that reached its end turns false once the mix has
         *        played its last frame.
         */
        bool is_playing(audio_voice_id id) const;

        void set_gain(audio_voice_id id, float gain);
        void set_pitch(audio_voice_id id, float pitch);
        void set_pan(audio_voice_id id, float pan);
        void set_position(audio_voice_id id, const core::math::vec3& position);
        void set_attenuation(audio_voice_id id, float min_distance, float max_distance, float rolloff);

        /**
         * @brief Sets the game time scale every voice plays at (see the class
         *        notes): 1 plays voices as they are, 0 pauses every one of
         *        them. A negative scale is taken as 0.
         */
        void set_time_scale(float scale);

        /** @brief The time scale set through @ref set_time_scale. */
        float time_scale() const noexcept;

        // --- Listener arbitration --------------------------------------
        //
        // Mirrors camera arbitration in spirit (rendering_engine/camera/
        // camera.hpp) but simpler: there is no priority, just "the
        // most recently attached, enabled listener wins". A default (0)
        // token names no listener. The listeners live on the main thread;
        // @ref update posts the winner's pose to the mix.
        using listener_token = std::uint32_t;

        /** @brief Registers a new listener candidate, enabled, at the origin. Always succeeds. */
        listener_token attach_listener();

        /** @brief Removes the listener named by @p token. No-op if unknown. */
        void detach_listener(listener_token token);

        /** @brief Enables/disables the listener named by @p token for arbitration. No-op if unknown. */
        void set_listener_enabled(listener_token token, bool enabled);

        /** @brief Updates the world-space position / right axis of the listener named by @p token. No-op if unknown. */
        void
        set_listener_transform(listener_token token, const core::math::vec3& position, const core::math::vec3& right);

        /**
         * @brief The main thread's once-per-frame hand-over: posts the
         *        active listener's pose to the mix, frees the slots of the
         *        voices the mix reported finished, and releases the clips
         *        the mix no longer reads.
         *
         * Called once per frame by the engine's audio stage, with the real
         * (unscaled) frame delta, after every source/listener transform of
         * the frame has been handed over. With no device open it also
         * applies the queued commands and advances every active voice by
         * @p delta_seconds worth of playback (times its pitch and the time
         * scale), so looping/finishing bookkeeping and @ref is_playing stay
         * correct without one; with a device the device's own pull
         * advances them, and @p delta_seconds is unused.
         */
        void update(double delta_seconds);

        /** @brief Fixed sample rate every clip is converted to and the mixer runs at. */
        static constexpr std::uint32_t k_mixer_sample_rate = 48000;
        /** @brief Fixed channel count (stereo) every clip is converted to and the mixer runs at. */
        static constexpr std::uint32_t k_mixer_channels = 2;
        /** @brief Maximum number of voices mixed at once; a @ref play beyond this is dropped. */
        static constexpr std::size_t k_max_voices = 32;

    private:
        // --- Commands: the main thread's side of the hand-over ---------

        enum class command_kind : std::uint8_t
        {
            play,
            stop,
            set_paused,
            set_gain,
            set_pitch,
            set_pan,
            set_position,
            set_attenuation,
            set_time_scale,
            set_listener,
        };

        // One change for the mix to apply. A voice command names the voice
        // by slot and generation, like an audio_voice_id; the other fields
        // are read according to the kind.
        struct command
        {
            command_kind kind{command_kind::stop};
            std::uint32_t index{0};
            std::uint32_t generation{0};
            const audio_clip* clip{nullptr}; // play
            core::math::vec3 position{};     // play, set_position, set_listener
            core::math::vec3 right{};        // set_listener
            float gain{1.0f};                // play, set_gain
            float pitch{1.0f};               // play, set_pitch
            float pan{0.0f};                 // play, set_pan
            float min_distance{1.0f};        // play, set_attenuation
            float max_distance{50.0f};       // play, set_attenuation
            float rolloff{1.0f};             // play, set_attenuation
            float time_scale{1.0f};          // set_time_scale
            bool loop{false};                // play
            bool spatial{false};             // play
            bool paused{false};              // set_paused
            bool has_listener{false};        // set_listener
        };

        // The main thread's mirror of one voice slot: whether it is taken,
        // the generation its current (or next) voice carries, and the clip
        // that voice plays, held here so the mix can read it by pointer.
        struct voice_slot
        {
            bool active{false};
            std::uint32_t generation{1};
            std::shared_ptr<audio_clip> clip;
        };

        struct listener_record
        {
            listener_token token{0};
            core::math::vec3 position{};
            core::math::vec3 right{};
            bool enabled{true};
        };

        // The listener pose last posted to the mix.
        struct listener_pose
        {
            bool present{false};
            core::math::vec3 position{};
            core::math::vec3 right{};
        };

        // --- The mix: owned by the thread that renders ------------------

        struct voice
        {
            bool active{false};
            std::uint32_t generation{0};
            const audio_clip* clip{nullptr};
            double cursor{0.0}; // fractional read position, in source frames
            bool paused{false};
            float gain{1.0f};
            float pitch{1.0f};
            float pan{0.0f};
            bool loop{false};
            bool spatial{false};
            core::math::vec3 position{};
            float min_distance{1.0f};
            float max_distance{50.0f};
            float rolloff{1.0f};
        };

        // Everything the mix reads and writes: the device's thread while a
        // device is open, the main thread otherwise.
        struct mixer_state
        {
            std::array<voice, k_max_voices> voices;
            listener_pose listener;
            float time_scale{1.0f};
            std::vector<float> scratch; // the device-less advance's mix buffer
        };

        // Decodes @p bytes (a whole WAV file already read into memory) and
        // converts it to the mixer format, or returns nullptr and logs why.
        std::shared_ptr<audio_clip> decode_wav(const std::vector<std::byte>& bytes, const std::string& label);

        // Main thread: the mirror slot @p id names, if its voice is still
        // live — not stopped, and not reported finished by the mix.
        const voice_slot* resolve(audio_voice_id id) const noexcept;
        std::size_t find_free_slot() const noexcept;
        // Main thread: frees slot @p index. Its clip is dropped at once when
        // the mix has already let go of it, else once the mix has applied
        // every command posted so far.
        void free_slot(std::size_t index, bool mixer_released) noexcept;
        void reap_finished_voices() noexcept;
        void release_retired_clips();
        void post_listener_pose();
        const listener_record* active_listener() const noexcept;

        // Main thread: queues @p change for the mix, holding it back in
        // m_overflow while the queue is full so no change is ever lost or
        // reordered.
        void post(const command& change);
        void pump_overflow();

        // The mix's side: apply every queued command, then mix a block.
        void apply_commands() noexcept;
        void apply(const command& change) noexcept;
        void render(float* out, std::size_t frame_count) noexcept;

        // Writes exactly frame_count mixed frames (k_mixer_channels floats
        // each) to out, advancing/looping/finishing every active voice by
        // that many frames.
        void mix_frames(float* out, std::size_t frame_count) noexcept;

        std::unordered_map<std::string, std::weak_ptr<audio_clip>> m_clips;

        std::unique_ptr<audio_output> m_output;
        std::unique_ptr<audio_decoder> m_decoder;
        bool m_available{false};

        // Main thread.
        std::array<voice_slot, k_max_voices> m_slots;
        bool m_voice_cap_warned{false};
        std::vector<listener_record> m_listeners;
        listener_token m_next_listener_token{1};
        listener_pose m_posted_listener;
        float m_time_scale{1.0f};
        std::uint64_t m_commands_posted{0};
        std::vector<command> m_overflow; // posted while the queue was full, oldest first
        bool m_overflow_warned{false};
        // Clips of stopped voices, each with the number of commands the mix
        // must have applied before it no longer reads it.
        std::vector<std::pair<std::uint64_t, std::shared_ptr<audio_clip>>> m_retired_clips;

        // Shared between the main thread and the mix.
        spsc_queue<command> m_commands;
        std::atomic<std::uint64_t> m_commands_applied{0};
        // Per slot, the generation of the last voice that reached its end in
        // the mix (0 for none yet).
        std::array<std::atomic<std::uint32_t>, k_max_voices> m_finished{};

        mixer_state m_mixer;
    };
} // namespace core
