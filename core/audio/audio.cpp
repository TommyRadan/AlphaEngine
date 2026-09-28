// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#define LOG_CATEGORY "audio"
#include <core/audio/audio.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include <core/audio/audio_clip.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <core/os/os.hpp>
#include <core/vfs/vfs.hpp>

namespace core
{
    namespace
    {
        // A source shorter than this carries no direction; treat the
        // listener and the voice as coincident rather than divide by ~0.
        constexpr float degenerate_length = 1e-6f;

        // Commands the queue holds before the main thread holds further ones
        // back: many frames' worth of voice changes and poses.
        constexpr std::size_t k_command_capacity = 4096;

        // Simple linear pan law: full signal to one side at |pan| == 1,
        // equal signal to both at pan == 0. Not equal-power, but this
        // subsystem keeps its DSP deliberately minimal.
        void pan_gains(float gain, float pan, float& left, float& right) noexcept
        {
            const float p = std::clamp(pan, -1.0f, 1.0f);
            left = gain * std::clamp(1.0f - p, 0.0f, 1.0f);
            right = gain * std::clamp(1.0f + p, 0.0f, 1.0f);
        }

        float sanitized_pitch(float pitch) noexcept
        {
            return pitch > 0.0f ? pitch : 1.0f;
        }

        bool same_pose(const core::math::vec3& a, const core::math::vec3& b) noexcept
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
    } // namespace

    audio::audio(std::unique_ptr<audio_output> output, std::unique_ptr<audio_decoder> decoder)
        : m_output{std::move(output)}, m_decoder{std::move(decoder)}, m_commands{k_command_capacity}
    {
    }

    audio::~audio()
    {
        quit();
    }

    void audio::init()
    {
        LOG_INF("Init core::audio");

        if (m_output == nullptr)
        {
            LOG_WRN("core::audio: no audio output; audio is disabled");
            return;
        }

        std::string error;
        if (!m_output->open(
                k_mixer_sample_rate,
                k_mixer_channels,
                [this](float* samples, std::size_t frame_count) { render(samples, frame_count); },
                error))
        {
            LOG_WRN("core::audio: %s; audio is disabled", error.c_str());
            return;
        }

        m_available = true;
        LOG_INF("core::audio: opened '%s' (%u Hz, %u ch), mixing on the device's thread",
                m_output->name().c_str(),
                k_mixer_sample_rate,
                k_mixer_channels);
    }

    void audio::quit()
    {
        // engine::quit() calls this explicitly and the destructor calls it
        // again as a safety net (see ~audio); only log once there is
        // something left to close, so the ordinary shutdown path prints one
        // line rather than two.
        if (m_available)
        {
            LOG_INF("Quit core::audio");
            // No render call runs once this returns: the mix is the main
            // thread's again.
            m_output->close();
        }
        m_available = false;

        command discarded;
        while (m_commands.try_pop(discarded))
        {
        }
        m_overflow.clear();
        m_mixer = mixer_state{};
        for (std::size_t index = 0; index < m_slots.size(); ++index)
        {
            if (m_slots[index].active)
            {
                free_slot(index, true);
            }
            m_finished[index].store(0, std::memory_order_relaxed);
        }
        m_retired_clips.clear();
        m_commands_posted = 0;
        m_commands_applied.store(0, std::memory_order_relaxed);
        m_posted_listener = listener_pose{};
        m_time_scale = 1.0f;
        m_listeners.clear();
        m_clips.clear();
    }

    std::shared_ptr<audio_clip> audio::decode_wav(const std::vector<std::byte>& bytes, const std::string& label)
    {
        if (m_decoder == nullptr)
        {
            LOG_ERR("core::audio: no decoder for '%s'", label.c_str());
            return nullptr;
        }

        auto clip = std::make_shared<audio_clip>();
        std::string error;
        if (!m_decoder->decode_wav(bytes, k_mixer_sample_rate, k_mixer_channels, clip->samples, error))
        {
            LOG_ERR("core::audio: could not decode '%s': %s", label.c_str(), error.c_str());
            return nullptr;
        }

        clip->frame_count = static_cast<std::uint32_t>(clip->samples.size() / k_mixer_channels);
        if (clip->frame_count == 0)
        {
            LOG_ERR("core::audio: '%s' decoded to no audio data", label.c_str());
            return nullptr;
        }

        LOG_DBG("core::audio: loaded '%s' (%u frames @ %u Hz)", label.c_str(), clip->frame_count, k_mixer_sample_rate);
        return clip;
    }

    std::shared_ptr<audio_clip> audio::load_clip(const std::filesystem::path& path)
    {
        const std::string key = core::default_vfs().canonical_key(path);
        if (auto it = m_clips.find(key); it != m_clips.end())
        {
            if (std::shared_ptr<audio_clip> existing = it->second.lock())
            {
                return existing;
            }
        }

        std::vector<std::byte> bytes;
        std::string error;
        if (!core::default_vfs().read_file(path, bytes, &error))
        {
            LOG_ERR("core::audio: could not read '%s': %s", core::os::path_to_utf8(path).c_str(), error.c_str());
            return nullptr;
        }

        std::shared_ptr<audio_clip> clip = decode_wav(bytes, core::os::path_to_utf8(path));
        if (clip == nullptr)
        {
            return nullptr;
        }

        clip->key = key;
        m_clips[key] = clip;
        return clip;
    }

    // --- Main thread --------------------------------------------------------

    void audio::post(const command& change)
    {
        ++m_commands_posted;
        if (m_overflow.empty() && m_commands.try_push(change))
        {
            return;
        }
        // The mix has not drained the queue for a long while (a device that
        // stopped pulling); hold the change back rather than drop or reorder
        // it, and hand it over as the queue frees up.
        if (!m_overflow_warned)
        {
            LOG_WRN("core::audio: the command queue is full; holding changes until the mix catches up");
            m_overflow_warned = true;
        }
        m_overflow.push_back(change);
        pump_overflow();
    }

    void audio::pump_overflow()
    {
        std::size_t handed = 0;
        while (handed < m_overflow.size() && m_commands.try_push(m_overflow[handed]))
        {
            ++handed;
        }
        m_overflow.erase(m_overflow.begin(), m_overflow.begin() + static_cast<std::ptrdiff_t>(handed));
    }

    const audio::voice_slot* audio::resolve(audio_voice_id id) const noexcept
    {
        if (!id.valid() || id.index >= m_slots.size())
        {
            return nullptr;
        }
        const voice_slot& slot = m_slots[id.index];
        if (!slot.active || slot.generation != id.generation ||
            m_finished[id.index].load(std::memory_order_acquire) == slot.generation)
        {
            return nullptr;
        }
        return &slot;
    }

    std::size_t audio::find_free_slot() const noexcept
    {
        for (std::size_t i = 0; i < m_slots.size(); ++i)
        {
            if (!m_slots[i].active)
            {
                return i;
            }
        }
        return m_slots.size();
    }

    void audio::free_slot(std::size_t index, bool mixer_released) noexcept
    {
        voice_slot& slot = m_slots[index];
        slot.active = false;
        if (mixer_released)
        {
            slot.clip.reset();
        }
        else
        {
            m_retired_clips.emplace_back(m_commands_posted, std::move(slot.clip));
            slot.clip = nullptr;
        }
        if (++slot.generation == 0)
        {
            // Skip the invalid handle value on wrap, exactly like core::pool.
            slot.generation = 1;
        }
    }

    void audio::reap_finished_voices() noexcept
    {
        for (std::size_t index = 0; index < m_slots.size(); ++index)
        {
            // The mix drops its pointer to the clip before it reports the
            // voice finished (a release store), so the clip can go at once.
            if (m_slots[index].active && m_finished[index].load(std::memory_order_acquire) == m_slots[index].generation)
            {
                free_slot(index, true);
            }
        }
    }

    void audio::release_retired_clips()
    {
        const std::uint64_t applied = m_commands_applied.load(std::memory_order_acquire);
        std::erase_if(m_retired_clips, [applied](const auto& retired) { return retired.first <= applied; });
    }

    audio_voice_id audio::play(const audio_play_params& params)
    {
        if (params.clip == nullptr || params.clip->frame_count == 0)
        {
            return {};
        }

        reap_finished_voices();
        const std::size_t index = find_free_slot();
        if (index == m_slots.size())
        {
            if (!m_voice_cap_warned)
            {
                LOG_WRN("core::audio: voice cap (%zu) reached; dropping a playback request", k_max_voices);
                m_voice_cap_warned = true;
            }
            return {};
        }

        voice_slot& slot = m_slots[index];
        slot.active = true;
        slot.clip = params.clip;

        command change;
        change.kind = command_kind::play;
        change.index = static_cast<std::uint32_t>(index);
        change.generation = slot.generation;
        change.clip = params.clip.get();
        change.gain = params.gain;
        change.pitch = sanitized_pitch(params.pitch);
        change.pan = params.pan;
        change.loop = params.loop;
        change.spatial = params.spatial;
        change.position = params.position;
        change.min_distance = std::max(params.min_distance, 0.0f);
        change.max_distance = std::max(params.max_distance, change.min_distance + 1e-3f);
        change.rolloff = std::max(params.rolloff, 0.0f);
        post(change);
        return audio_voice_id{change.index, change.generation};
    }

    void audio::play_one_shot(const std::shared_ptr<audio_clip>& clip, float gain, float pan)
    {
        audio_play_params params;
        params.clip = clip;
        params.gain = gain;
        params.pan = pan;
        play(params);
    }

    void audio::stop(audio_voice_id id)
    {
        if (resolve(id) == nullptr)
        {
            return;
        }
        command change;
        change.kind = command_kind::stop;
        change.index = id.index;
        change.generation = id.generation;
        post(change);
        // The mix may be playing the voice until it applies the stop, so the
        // clip is retired rather than dropped.
        free_slot(id.index, false);
    }

    void audio::set_paused(audio_voice_id id, bool paused)
    {
        if (resolve(id) != nullptr)
        {
            command change;
            change.kind = command_kind::set_paused;
            change.index = id.index;
            change.generation = id.generation;
            change.paused = paused;
            post(change);
        }
    }

    bool audio::is_playing(audio_voice_id id) const
    {
        return resolve(id) != nullptr;
    }

    void audio::set_gain(audio_voice_id id, float gain)
    {
        if (resolve(id) != nullptr)
        {
            command change;
            change.kind = command_kind::set_gain;
            change.index = id.index;
            change.generation = id.generation;
            change.gain = gain;
            post(change);
        }
    }

    void audio::set_pitch(audio_voice_id id, float pitch)
    {
        if (resolve(id) != nullptr)
        {
            command change;
            change.kind = command_kind::set_pitch;
            change.index = id.index;
            change.generation = id.generation;
            change.pitch = sanitized_pitch(pitch);
            post(change);
        }
    }

    void audio::set_pan(audio_voice_id id, float pan)
    {
        if (resolve(id) != nullptr)
        {
            command change;
            change.kind = command_kind::set_pan;
            change.index = id.index;
            change.generation = id.generation;
            change.pan = pan;
            post(change);
        }
    }

    void audio::set_position(audio_voice_id id, const core::math::vec3& position)
    {
        if (resolve(id) != nullptr)
        {
            command change;
            change.kind = command_kind::set_position;
            change.index = id.index;
            change.generation = id.generation;
            change.position = position;
            post(change);
        }
    }

    void audio::set_attenuation(audio_voice_id id, float min_distance, float max_distance, float rolloff)
    {
        if (resolve(id) != nullptr)
        {
            command change;
            change.kind = command_kind::set_attenuation;
            change.index = id.index;
            change.generation = id.generation;
            change.min_distance = std::max(min_distance, 0.0f);
            change.max_distance = std::max(max_distance, change.min_distance + 1e-3f);
            change.rolloff = std::max(rolloff, 0.0f);
            post(change);
        }
    }

    void audio::set_time_scale(float scale)
    {
        const float clamped = scale > 0.0f ? scale : 0.0f;
        if (clamped == m_time_scale)
        {
            return;
        }
        m_time_scale = clamped;
        command change;
        change.kind = command_kind::set_time_scale;
        change.time_scale = clamped;
        post(change);
    }

    float audio::time_scale() const noexcept
    {
        return m_time_scale;
    }

    audio::listener_token audio::attach_listener()
    {
        const listener_token token = m_next_listener_token++;
        listener_record record;
        record.token = token;
        m_listeners.push_back(record);
        return token;
    }

    void audio::detach_listener(listener_token token)
    {
        auto it = std::find_if(
            m_listeners.begin(), m_listeners.end(), [token](const listener_record& r) { return r.token == token; });
        if (it != m_listeners.end())
        {
            m_listeners.erase(it);
        }
    }

    void audio::set_listener_enabled(listener_token token, bool enabled)
    {
        for (listener_record& r : m_listeners)
        {
            if (r.token == token)
            {
                r.enabled = enabled;
                return;
            }
        }
    }

    void
    audio::set_listener_transform(listener_token token, const core::math::vec3& position, const core::math::vec3& right)
    {
        for (listener_record& r : m_listeners)
        {
            if (r.token == token)
            {
                r.position = position;
                r.right = right;
                return;
            }
        }
    }

    const audio::listener_record* audio::active_listener() const noexcept
    {
        // "Most recently attached wins": walk back to front and take the
        // first enabled one, mirroring camera arbitration's spirit
        // (rendering_engine/render_world.cpp) without its priority ranking.
        for (auto it = m_listeners.rbegin(); it != m_listeners.rend(); ++it)
        {
            if (it->enabled)
            {
                return &*it;
            }
        }
        return nullptr;
    }

    void audio::post_listener_pose()
    {
        listener_pose pose;
        if (const listener_record* listener = active_listener())
        {
            pose.present = true;
            pose.position = listener->position;
            pose.right = listener->right;
        }
        if (pose.present == m_posted_listener.present && same_pose(pose.position, m_posted_listener.position) &&
            same_pose(pose.right, m_posted_listener.right))
        {
            return;
        }
        m_posted_listener = pose;
        command change;
        change.kind = command_kind::set_listener;
        change.has_listener = pose.present;
        change.position = pose.position;
        change.right = pose.right;
        post(change);
    }

    void audio::update(double delta_seconds)
    {
        pump_overflow();
        post_listener_pose();

        if (!m_available)
        {
            // No device thread, so this thread runs the mix: apply what was
            // posted, then advance the voices by wall-clock time, capped so
            // one catch-up call (a debugger pause) never mixes more than
            // half a second.
            do
            {
                apply_commands();
                pump_overflow();
            } while (!m_overflow.empty());

            constexpr std::size_t k_max_frames_per_update = k_mixer_sample_rate / 2;
            const auto frames = std::min(static_cast<std::size_t>(std::clamp(delta_seconds, 0.0, 1.0) *
                                                                  static_cast<double>(k_mixer_sample_rate)),
                                         k_max_frames_per_update);
            if (frames > 0)
            {
                m_mixer.scratch.assign(frames * k_mixer_channels, 0.0f);
                mix_frames(m_mixer.scratch.data(), frames);
            }
        }

        reap_finished_voices();
        release_retired_clips();
    }

    // --- The mix ------------------------------------------------------------

    void audio::apply(const command& change) noexcept
    {
        if (change.kind == command_kind::set_time_scale)
        {
            m_mixer.time_scale = change.time_scale;
            return;
        }
        if (change.kind == command_kind::set_listener)
        {
            m_mixer.listener.present = change.has_listener;
            m_mixer.listener.position = change.position;
            m_mixer.listener.right = change.right;
            return;
        }

        voice& v = m_mixer.voices[change.index];
        if (change.kind == command_kind::play)
        {
            v.active = true;
            v.generation = change.generation;
            v.clip = change.clip;
            v.cursor = 0.0;
            v.paused = false;
            v.gain = change.gain;
            v.pitch = change.pitch;
            v.pan = change.pan;
            v.loop = change.loop;
            v.spatial = change.spatial;
            v.position = change.position;
            v.min_distance = change.min_distance;
            v.max_distance = change.max_distance;
            v.rolloff = change.rolloff;
            return;
        }

        // Every other change names a voice that may have finished or been
        // replaced since it was posted; the generation tells.
        if (!v.active || v.generation != change.generation)
        {
            return;
        }
        switch (change.kind)
        {
        case command_kind::stop:
            v.active = false;
            v.clip = nullptr;
            break;
        case command_kind::set_paused:
            v.paused = change.paused;
            break;
        case command_kind::set_gain:
            v.gain = change.gain;
            break;
        case command_kind::set_pitch:
            v.pitch = change.pitch;
            break;
        case command_kind::set_pan:
            v.pan = change.pan;
            break;
        case command_kind::set_position:
            v.position = change.position;
            break;
        case command_kind::set_attenuation:
            v.min_distance = change.min_distance;
            v.max_distance = change.max_distance;
            v.rolloff = change.rolloff;
            break;
        case command_kind::play:
        case command_kind::set_time_scale:
        case command_kind::set_listener:
            break;
        }
    }

    void audio::apply_commands() noexcept
    {
        std::uint64_t applied = 0;
        command change;
        while (m_commands.try_pop(change))
        {
            apply(change);
            ++applied;
        }
        if (applied > 0)
        {
            // Only the mix writes the count; the release store lets the main
            // thread drop the clips of the voices the commands stopped.
            m_commands_applied.store(m_commands_applied.load(std::memory_order_relaxed) + applied,
                                     std::memory_order_release);
        }
    }

    void audio::render(float* out, std::size_t frame_count) noexcept
    {
        apply_commands();
        mix_frames(out, frame_count);
    }

    void audio::mix_frames(float* out, std::size_t frame_count) noexcept
    {
        std::fill(out, out + frame_count * k_mixer_channels, 0.0f);
        const listener_pose& listener = m_mixer.listener;

        for (std::size_t index = 0; index < m_mixer.voices.size(); ++index)
        {
            voice& v = m_mixer.voices[index];
            // The voice's playback rate: its pitch at the game's time scale.
            // At 0 it holds its place and contributes nothing.
            const double rate = static_cast<double>(v.pitch) * static_cast<double>(m_mixer.time_scale);
            if (!v.active || v.paused || v.clip == nullptr || !(rate > 0.0))
            {
                continue;
            }

            float left_gain = 0.0f;
            float right_gain = 0.0f;
            if (v.spatial)
            {
                // No listener: keep the voice advancing (see the class docs
                // on update) but contribute nothing audible.
                if (listener.present)
                {
                    const core::math::vec3 offset = v.position - listener.position;
                    const float distance = core::math::length(offset);

                    float pan = 0.0f;
                    if (distance > degenerate_length)
                    {
                        pan = std::clamp(core::math::dot(offset / distance, listener.right), -1.0f, 1.0f);
                    }

                    const float span = std::max(v.max_distance - v.min_distance, 1e-4f);
                    const float t = std::clamp((distance - v.min_distance) / span, 0.0f, 1.0f);
                    const float attenuation = std::pow(1.0f - t, v.rolloff);
                    pan_gains(v.gain * attenuation, pan, left_gain, right_gain);
                }
            }
            else
            {
                pan_gains(v.gain, v.pan, left_gain, right_gain);
            }

            const std::vector<float>& samples = v.clip->samples;
            const std::size_t clip_frames = v.clip->frame_count;

            for (std::size_t i = 0; i < frame_count; ++i)
            {
                const auto base = static_cast<std::size_t>(v.cursor);
                if (base >= clip_frames)
                {
                    break;
                }
                const float frac = static_cast<float>(v.cursor - static_cast<double>(base));
                const std::size_t next = base + 1 < clip_frames ? base + 1 : (v.loop ? 0 : base);

                const float l = samples[base * 2 + 0] + (samples[next * 2 + 0] - samples[base * 2 + 0]) * frac;
                const float r = samples[base * 2 + 1] + (samples[next * 2 + 1] - samples[base * 2 + 1]) * frac;

                out[i * 2 + 0] += l * left_gain;
                out[i * 2 + 1] += r * right_gain;

                v.cursor += rate;
                if (v.cursor >= static_cast<double>(clip_frames))
                {
                    if (v.loop)
                    {
                        v.cursor = std::fmod(v.cursor, static_cast<double>(clip_frames));
                    }
                    else
                    {
                        // Let go of the clip before reporting the end: the
                        // main thread may drop it as soon as it sees it.
                        v.active = false;
                        v.clip = nullptr;
                        m_finished[index].store(v.generation, std::memory_order_release);
                        break;
                    }
                }
            }
        }

        // A hard clip guards against many overlapping voices summing past full scale.
        for (std::size_t i = 0; i < frame_count * k_mixer_channels; ++i)
        {
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
        }
    }
} // namespace core
