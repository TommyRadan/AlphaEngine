/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file audio.hpp
 * @brief SDL3-backed audio subsystem: playback device, software mixer and
 *        clip cache.
 */

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <core/audio/audio_types.hpp>
#include <core/math/vec3.hpp>

// The concrete stream type is only named in audio.cpp — the one
// SDL-including translation unit for this subsystem, like
// core/platform/platform_sdl.cpp is for the platform layer.
struct SDL_AudioStream;

namespace core
{
    struct audio_clip;

    /**
     * @brief Owns the default playback device, a fixed-voice-cap software
     *        mixer, and a weak-ref cache of decoded @ref audio_clip assets.
     *
     * Owned by @ref runtime::engine with the usual @c init / @c quit shape.
     * @ref init opens the default playback device through SDL3 (a plain
     * @c SDL_AudioStream bound to an @c SDL_OpenAudioDevice handle, rather
     * than the callback-driven @c SDL_OpenAudioDeviceStream form, so mixing
     * stays a main-thread call): every clip is decoded and converted once,
     * up front, to one fixed mixer format (@ref k_mixer_sample_rate stereo
     * float, @ref k_mixer_channels channels), so a device that requests a
     * different native format is handled by the stream's own conversion,
     * invisibly to every voice.
     *
     * Playback is a pull model: @ref update, called once per tick from
     * @c engine::tick after the scene graph has updated every source and
     * listener's transform, tops the bound stream's queue up to a small
     * target buffer by mixing the @ref k_max_voices active voices into one
     * interleaved buffer and handing it to SDL — no audio callback, no
     * cross-thread mixer state. Only WAV is decoded (@c SDL_LoadWAV_IO);
     * compressed formats and streamed music are a follow-on (see the class
     * docs on scope in the audio subsystem's issue).
     *
     * **Graceful degradation.** A container or CI runner with no usable
     * playback device is expected, not exceptional: @ref init logs one
     * warning and leaves the subsystem with no device. Every playback call
     * still runs — clips still decode, @ref play still allocates and
     * advances a voice, listeners still attach and arbitrate — so game code
     * needs no "is audio available" branch of its own; only the final
     * hand-off to the (absent) device is skipped, so nothing is ever heard
     * and nothing hangs or throws.
     *
     * Main-thread-only, like every other subsystem: nothing here runs on a
     * background thread or an SDL audio callback, so there is no mixer
     * state to synchronise.
     */
    struct audio
    {
        audio();
        ~audio();

        audio(const audio&) = delete;
        audio& operator=(const audio&) = delete;
        audio(audio&&) = delete;
        audio& operator=(audio&&) = delete;

        /**
         * @brief Opens the default playback device and binds the mixer
         *        stream. Never throws: a missing or unusable device is
         *        logged at @c WRN and leaves @ref is_available false.
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
         * (@c SDL_LoadWAV_IO) and converts it to the mixer format once; the
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

        /** @brief Whether @p id names a voice still playing (or paused). */
        bool is_playing(audio_voice_id id) const;

        void set_gain(audio_voice_id id, float gain);
        void set_pitch(audio_voice_id id, float pitch);
        void set_pan(audio_voice_id id, float pan);
        void set_position(audio_voice_id id, const core::math::vec3& position);
        void set_attenuation(audio_voice_id id, float min_distance, float max_distance, float rolloff);

        // --- Listener arbitration --------------------------------------
        //
        // Mirrors the camera registry in spirit (rendering_engine/camera/
        // camera_registry.hpp) but simpler: there is no priority, just "the
        // most recently attached, enabled listener wins". A default (0)
        // token names no listener.
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
         * @brief Mixes and hands the device's bound stream enough audio to
         *        keep it topped up, advancing every active voice by @p
         *        delta_time worth of playback either way.
         *
         * Called once per tick by @c engine::tick, after the scene graph's
         * update so every source/listener transform pushed this frame is
         * already applied. With no device open this only advances voices
         * (so looping/finishing bookkeeping and @ref is_playing stay correct
         * without one) and mixes nothing to a device.
         */
        void update(float delta_time);

        /** @brief Fixed sample rate every clip is converted to and the mixer runs at. */
        static constexpr std::uint32_t k_mixer_sample_rate = 48000;
        /** @brief Fixed channel count (stereo) every clip is converted to and the mixer runs at. */
        static constexpr std::uint32_t k_mixer_channels = 2;
        /** @brief Maximum number of voices mixed at once; a @ref play beyond this is dropped. */
        static constexpr std::size_t k_max_voices = 32;

    private:
        struct voice
        {
            bool active{false};
            std::uint32_t generation{1};
            std::shared_ptr<audio_clip> clip;
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

        struct listener_record
        {
            listener_token token{0};
            core::math::vec3 position{};
            core::math::vec3 right{};
            bool enabled{true};
        };

        // Decodes @p bytes (a whole WAV file already read into memory) and
        // converts it to the mixer format, or returns nullptr and logs why.
        std::shared_ptr<audio_clip> decode_wav(const std::vector<std::byte>& bytes, const std::string& label);

        voice* resolve(audio_voice_id id) noexcept;
        const voice* resolve(audio_voice_id id) const noexcept;
        void free_voice(voice& v) noexcept;
        std::size_t find_free_voice() const noexcept;

        const listener_record* active_listener() const noexcept;

        // Writes exactly frame_count mixed frames (k_mixer_channels floats
        // each) to out, advancing/looping/finishing every active voice by
        // that many frames. The caller decides whether the result is worth
        // handing to a device.
        void mix_frames(float* out, std::size_t frame_count);

        std::unordered_map<std::string, std::weak_ptr<audio_clip>> m_clips;

        std::uint32_t m_device{0}; // an SDL_AudioDeviceID; 0 is SDL's "invalid" value
        SDL_AudioStream* m_stream{nullptr};
        bool m_available{false};

        std::array<voice, k_max_voices> m_voices;
        bool m_voice_cap_warned{false};

        std::vector<listener_record> m_listeners;
        listener_token m_next_listener_token{1};

        std::vector<float> m_scratch; // reused mix buffer, sized by update()
    };
} // namespace core
