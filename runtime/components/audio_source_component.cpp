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

#include <runtime/components/audio_source_component.hpp>

#include <utility>

#include <core/audio/audio.hpp>
#include <core/audio/audio_clip.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>

runtime::audio_source_component::audio_source_component(config cfg) : m_config{std::move(cfg)} {}

runtime::audio_source_component runtime::audio_source_component::clone() const
{
    return audio_source_component{m_config};
}

void runtime::audio_source_component::on_attach(node& owner)
{
    m_position = owner.world_position();
    if (m_config.play_on_attach)
    {
        play();
    }
}

void runtime::audio_source_component::on_update(node& owner)
{
    m_position = owner.world_position();
    if (m_config.spatial && m_voice.valid())
    {
        if (core::audio* audio = runtime::current_engine().audio.get())
        {
            audio->set_position(m_voice, m_position);
        }
    }
}

void runtime::audio_source_component::on_destroy()
{
    stop();
}

void runtime::audio_source_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (!m_voice.valid())
    {
        return;
    }
    if (core::audio* audio = runtime::current_engine().audio.get())
    {
        audio->set_paused(m_voice, !active);
    }
}

void runtime::audio_source_component::play()
{
    if (m_config.clip == nullptr)
    {
        return;
    }
    core::audio* audio = runtime::current_engine().audio.get();
    if (audio == nullptr)
    {
        return;
    }

    // Retriggering an already-playing source replaces its voice rather than
    // layering a second one.
    if (m_voice.valid())
    {
        audio->stop(m_voice);
    }

    core::audio_play_params params;
    params.clip = m_config.clip;
    params.gain = m_config.gain;
    params.pitch = m_config.pitch;
    params.loop = m_config.loop;
    params.pan = m_config.pan;
    params.spatial = m_config.spatial;
    params.position = m_position;
    params.min_distance = m_config.min_distance;
    params.max_distance = m_config.max_distance;
    params.rolloff = m_config.rolloff;
    m_voice = audio->play(params);
}

void runtime::audio_source_component::stop()
{
    if (!m_voice.valid())
    {
        return;
    }
    if (core::audio* audio = runtime::current_engine().audio.get())
    {
        audio->stop(m_voice);
    }
    m_voice = core::audio_voice_id{};
}

bool runtime::audio_source_component::is_playing() const
{
    if (!m_voice.valid())
    {
        return false;
    }
    core::audio* audio = runtime::current_engine().audio.get();
    return audio != nullptr && audio->is_playing(m_voice);
}

void runtime::audio_source_component::set_gain(float gain)
{
    m_config.gain = gain;
    if (m_voice.valid())
    {
        if (core::audio* audio = runtime::current_engine().audio.get())
        {
            audio->set_gain(m_voice, gain);
        }
    }
}

void runtime::audio_source_component::set_pitch(float pitch)
{
    m_config.pitch = pitch;
    if (m_voice.valid())
    {
        if (core::audio* audio = runtime::current_engine().audio.get())
        {
            audio->set_pitch(m_voice, pitch);
        }
    }
}

void runtime::audio_source_component::set_pan(float pan)
{
    m_config.pan = pan;
    if (m_voice.valid())
    {
        if (core::audio* audio = runtime::current_engine().audio.get())
        {
            audio->set_pan(m_voice, pan);
        }
    }
}

void runtime::audio_source_component::set_attenuation(float min_distance, float max_distance, float rolloff)
{
    m_config.min_distance = min_distance;
    m_config.max_distance = max_distance;
    m_config.rolloff = rolloff;
    if (m_voice.valid())
    {
        if (core::audio* audio = runtime::current_engine().audio.get())
        {
            audio->set_attenuation(m_voice, min_distance, max_distance, rolloff);
        }
    }
}
