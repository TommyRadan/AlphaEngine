// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio_clip.hpp
 * @brief Decoded, mixer-ready PCM audio produced by @ref core::audio::load_clip.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core
{
    /**
     * @brief One decoded sound, cached and shared like @c texture_asset /
     *        @c mesh_asset.
     *
     * Holds interleaved 32-bit float samples already converted to the
     * mixer's fixed format (@ref audio::k_mixer_sample_rate stereo,
     * @ref audio::k_mixer_channels channels): the format conversion runs
     * once in @ref audio::load_clip, so mixing a voice is a plain
     * gain-scaled, pitch-resampled read with no per-block conversion. The
     * samples are never written after the clip is built, so the mix reads
     * them on the audio device's thread without a lock.
     *
     * Unlike @c texture_asset this owns no device resource — it is host
     * memory decoded up front — so there is nothing to release beyond the
     * vector itself, and a clip stays valid even if the audio device that
     * decoded it later closes.
     */
    struct audio_clip
    {
        std::vector<float> samples;   // interleaved, k_mixer_channels floats per frame
        std::uint32_t frame_count{0}; // samples.size() / k_mixer_channels

        // The VFS canonical identity (@c vfs::canonical_key) of the file
        // @ref audio::load_clip decoded it from, or empty for a clip built
        // in memory. A scene file names the clip's file through it.
        std::string key;
    };
} // namespace core
