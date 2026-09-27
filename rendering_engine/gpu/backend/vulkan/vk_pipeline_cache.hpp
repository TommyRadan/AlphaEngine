// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_pipeline_cache.hpp
 * @brief The on-disk form of the Vulkan backend's @c VkPipelineCache.
 *
 * @c vk_device creates one pipeline cache at init, seeded from the file
 * these helpers read, passes it to every @c vkCreateGraphicsPipelines /
 * @c vkCreateComputePipelines call (and to the Dear ImGui backend), and
 * writes @c vkGetPipelineCacheData back at quit, so a relaunch skips the
 * driver's shader compiles for every pipeline an earlier run built.
 *
 * The file lives in the shader cache directory
 * (@ref gpu::shader_cache_directory, so @c ALPHAENGINE_SHADER_CACHE turns it
 * off or moves it like the SPIR-V cache), one per GPU, and wraps the
 * driver's blob in a small envelope: a magic number, a format version,
 * the blob's size and an FNV-1a digest of it. A truncated or bit-rotted
 * file therefore fails the envelope, and a blob another GPU or driver
 * wrote fails the check of Vulkan's own cache header (header version,
 * vendor ID, device ID and @c pipelineCacheUUID) — either way it is
 * ignored and the cache starts empty, rather than handing the driver
 * data it may not validate itself. Writes go through a temporary and a
 * rename so a crash mid-write never leaves a torn file behind.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace rendering_engine::gpu::backend::vulkan
{
    // The pipeline-cache file of the GPU @p properties describes, inside
    // @p directory.
    std::filesystem::path pipeline_cache_path(const std::filesystem::path& directory,
                                              const VkPhysicalDeviceProperties& properties);

    // Whether @p data begins with a VkPipelineCacheHeaderVersionOne that
    // the GPU and driver @p properties describe would accept: a sane
    // header length, header version one, and the same vendor ID, device
    // ID and pipelineCacheUUID.
    bool pipeline_cache_header_matches(const uint8_t* data, size_t size, const VkPhysicalDeviceProperties& properties);

    // What read_pipeline_cache found.
    enum class pipeline_cache_read
    {
        // The blob is valid for this device and is in the out parameter.
        loaded,
        // No file (the first run, or the cache was cleared).
        missing,
        // The file could not be read, or its envelope or Vulkan header
        // is malformed.
        corrupt,
        // A well-formed blob for another GPU or driver version.
        foreign,
    };

    // Read the blob stored at @p file into @p data (cleared unless the
    // result is loaded).
    pipeline_cache_read read_pipeline_cache(const std::filesystem::path& file,
                                            const VkPhysicalDeviceProperties& properties,
                                            std::vector<uint8_t>& data);

    // Store @p data (vkGetPipelineCacheData output) at @p file through a
    // temporary and a rename. Returns false, with the reason logged,
    // when the file cannot be written.
    bool write_pipeline_cache(const std::filesystem::path& file, const std::vector<uint8_t>& data);

    // The digest the envelope records for @p data, so a caller can tell
    // whether the cache changed since it was read.
    uint64_t pipeline_cache_digest(const std::vector<uint8_t>& data);
} // namespace rendering_engine::gpu::backend::vulkan
