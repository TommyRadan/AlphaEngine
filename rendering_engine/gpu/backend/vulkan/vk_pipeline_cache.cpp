// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/backend/vulkan/vk_pipeline_cache.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <core/hash.hpp>
#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        // The envelope ahead of the driver's blob. Four naturally
        // aligned fields, so the struct has no padding and is written
        // and read as bytes (the engine only targets little-endian
        // hosts, like the SPIR-V cache).
        struct envelope
        {
            uint32_t magic{0};
            uint32_t version{0};
            uint64_t size{0};
            uint64_t digest{0};
        };
        static_assert(sizeof(envelope) == 24, "the envelope is written as raw bytes");

        // "AEPC" read as a little-endian word.
        constexpr uint32_t envelope_magic = 0x43504541u;
        // Bumped whenever the envelope changes shape.
        constexpr uint32_t envelope_version = 1;

        // The fixed part of VkPipelineCacheHeaderVersionOne: header
        // length, header version, vendor ID, device ID and the UUID.
        constexpr size_t vulkan_header_size = 16 + VK_UUID_SIZE;

        // Vulkan writes the header fields least significant byte first,
        // whatever the host order.
        uint32_t read_le32(const uint8_t* bytes)
        {
            return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8u) |
                   (static_cast<uint32_t>(bytes[2]) << 16u) | (static_cast<uint32_t>(bytes[3]) << 24u);
        }
    } // namespace

    std::filesystem::path pipeline_cache_path(const std::filesystem::path& directory,
                                              const VkPhysicalDeviceProperties& properties)
    {
        char name[64];
        std::snprintf(name,
                      sizeof(name),
                      "vk_pipeline_cache_%04x_%04x.bin",
                      static_cast<unsigned>(properties.vendorID),
                      static_cast<unsigned>(properties.deviceID));
        return directory / name;
    }

    bool pipeline_cache_header_matches(const uint8_t* data, size_t size, const VkPhysicalDeviceProperties& properties)
    {
        if (data == nullptr || size < vulkan_header_size)
        {
            return false;
        }
        const uint32_t header_size = read_le32(data);
        const uint32_t header_version = read_le32(data + 4);
        const uint32_t vendor_id = read_le32(data + 8);
        const uint32_t device_id = read_le32(data + 12);
        if (header_size < vulkan_header_size || header_size > size ||
            header_version != static_cast<uint32_t>(VK_PIPELINE_CACHE_HEADER_VERSION_ONE))
        {
            return false;
        }
        return vendor_id == properties.vendorID && device_id == properties.deviceID &&
               std::memcmp(data + 16, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
    }

    uint64_t pipeline_cache_digest(const std::vector<uint8_t>& data)
    {
        return core::fnv1a_64(std::string_view{reinterpret_cast<const char*>(data.data()), data.size()});
    }

    pipeline_cache_read read_pipeline_cache(const std::filesystem::path& file,
                                            const VkPhysicalDeviceProperties& properties,
                                            std::vector<uint8_t>& data)
    {
        data.clear();
        std::error_code error;
        if (!std::filesystem::exists(file, error))
        {
            return pipeline_cache_read::missing;
        }

        std::ifstream in{file, std::ios::binary | std::ios::ate};
        if (!in)
        {
            return pipeline_cache_read::corrupt;
        }
        const std::streamoff file_size = in.tellg();
        if (file_size < static_cast<std::streamoff>(sizeof(envelope)))
        {
            return pipeline_cache_read::corrupt;
        }
        in.seekg(0);
        envelope header{};
        if (!in.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.magic != envelope_magic ||
            header.version != envelope_version ||
            header.size != static_cast<uint64_t>(file_size) - static_cast<uint64_t>(sizeof(envelope)))
        {
            return pipeline_cache_read::corrupt;
        }
        std::vector<uint8_t> blob(static_cast<size_t>(header.size));
        if (!in.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(blob.size())) ||
            pipeline_cache_digest(blob) != header.digest)
        {
            return pipeline_cache_read::corrupt;
        }

        // The envelope proves the blob is the one written; its own
        // header says whether this GPU and driver can use it. A header
        // too short or of an unknown version is malformed, a
        // well-formed one naming another device is foreign.
        if (blob.size() < vulkan_header_size || read_le32(blob.data()) < vulkan_header_size ||
            read_le32(blob.data()) > blob.size() ||
            read_le32(blob.data() + 4) != static_cast<uint32_t>(VK_PIPELINE_CACHE_HEADER_VERSION_ONE))
        {
            return pipeline_cache_read::corrupt;
        }
        if (!pipeline_cache_header_matches(blob.data(), blob.size(), properties))
        {
            return pipeline_cache_read::foreign;
        }
        data = std::move(blob);
        return pipeline_cache_read::loaded;
    }

    bool write_pipeline_cache(const std::filesystem::path& file, const std::vector<uint8_t>& data)
    {
        envelope header{};
        header.magic = envelope_magic;
        header.version = envelope_version;
        header.size = data.size();
        header.digest = pipeline_cache_digest(data);

        std::filesystem::path temp = file;
        temp += ".tmp";
        {
            std::ofstream out{temp, std::ios::binary | std::ios::trunc};
            if (!out.write(reinterpret_cast<const char*>(&header), sizeof(header)) ||
                !out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))
            {
                LOG_WRN("Vulkan pipeline cache: cannot write %s", temp.string().c_str());
                std::error_code ignored;
                out.close();
                std::filesystem::remove(temp, ignored);
                return false;
            }
        }
        std::error_code error;
        std::filesystem::rename(temp, file, error);
        if (error)
        {
            LOG_WRN("Vulkan pipeline cache: cannot move %s into place (%s)",
                    temp.string().c_str(),
                    error.message().c_str());
            std::filesystem::remove(temp, error);
            return false;
        }
        return true;
    }

    void vk_pipeline_cache::create(VkPhysicalDevice physical_device, VkDevice device)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        std::vector<uint8_t> seed;
        m_pipeline_cache = VK_NULL_HANDLE;
        m_pipeline_cache_digest = 0;
        m_pipeline_cache_file.clear();
        // The shader cache switch covers this cache too: with the
        // SPIR-V cache off, every pipeline is built from scratch.
        const std::filesystem::path& directory = gpu::shader_cache_directory();
        if (directory.empty())
        {
            LOG_INF("Vulkan pipeline cache: disabled with the shader cache");
            return;
        }

        m_pipeline_cache_file = pipeline_cache_path(directory, properties);
        const std::string file = m_pipeline_cache_file.string();
        switch (read_pipeline_cache(m_pipeline_cache_file, properties, seed))
        {
        case pipeline_cache_read::loaded:
            m_pipeline_cache_digest = pipeline_cache_digest(seed);
            LOG_INF("Vulkan pipeline cache: read %zu bytes from %s", seed.size(), file.c_str());
            break;
        case pipeline_cache_read::missing:
            LOG_INF("Vulkan pipeline cache: %s does not exist yet; starting empty", file.c_str());
            break;
        case pipeline_cache_read::corrupt:
            LOG_WRN("Vulkan pipeline cache: %s is truncated or malformed; ignoring it", file.c_str());
            break;
        case pipeline_cache_read::foreign:
            LOG_INF("Vulkan pipeline cache: %s was written by another GPU or driver; ignoring it", file.c_str());
            break;
        }

        VkPipelineCacheCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        info.initialDataSize = seed.size();
        info.pInitialData = seed.empty() ? nullptr : seed.data();
        VkResult result = vkCreatePipelineCache(device, &info, nullptr, &m_pipeline_cache);
        if (result != VK_SUCCESS && !seed.empty())
        {
            // The header matched, yet the driver refused the rest; an
            // empty cache is always accepted.
            LOG_WRN("Vulkan pipeline cache: the driver refused the stored data (%s); starting empty",
                    vk_result_to_string(result));
            m_pipeline_cache_digest = 0;
            info.initialDataSize = 0;
            info.pInitialData = nullptr;
            result = vkCreatePipelineCache(device, &info, nullptr, &m_pipeline_cache);
        }
        if (result != VK_SUCCESS)
        {
            LOG_WRN("Vulkan pipeline cache: vkCreatePipelineCache failed (%s); pipelines are built without one",
                    vk_result_to_string(result));
            m_pipeline_cache = VK_NULL_HANDLE;
            m_pipeline_cache_file.clear();
        }
    }

    void vk_pipeline_cache::save_and_destroy(VkDevice device, bool device_lost)
    {
        if (m_pipeline_cache == VK_NULL_HANDLE)
        {
            return;
        }
        // After a device loss the cache may hold whatever the driver was
        // building when it died; the file from the last good run stays.
        if (!m_pipeline_cache_file.empty() && !device_lost)
        {
            const std::string file = m_pipeline_cache_file.string();
            size_t size = 0;
            VkResult result = vkGetPipelineCacheData(device, m_pipeline_cache, &size, nullptr);
            std::vector<uint8_t> data;
            if (result == VK_SUCCESS && size > 0)
            {
                data.resize(size);
                result = vkGetPipelineCacheData(device, m_pipeline_cache, &size, data.data());
                data.resize(size);
            }
            if (result != VK_SUCCESS)
            {
                LOG_WRN("Vulkan pipeline cache: vkGetPipelineCacheData failed (%s); %s not updated",
                        vk_result_to_string(result),
                        file.c_str());
            }
            else if (data.empty())
            {
                LOG_INF("Vulkan pipeline cache: the driver returned no data; %s not updated", file.c_str());
            }
            else if (m_pipeline_cache_digest != 0 && pipeline_cache_digest(data) == m_pipeline_cache_digest)
            {
                LOG_INF("Vulkan pipeline cache: unchanged (%zu bytes); %s left as it is", data.size(), file.c_str());
            }
            else if (write_pipeline_cache(m_pipeline_cache_file, data))
            {
                LOG_INF("Vulkan pipeline cache: wrote %zu bytes to %s", data.size(), file.c_str());
            }
        }
        vkDestroyPipelineCache(device, m_pipeline_cache, nullptr);
        m_pipeline_cache = VK_NULL_HANDLE;
        m_pipeline_cache_file.clear();
        m_pipeline_cache_digest = 0;
    }

    VkPipelineCache vk_pipeline_cache::handle() const noexcept
    {
        return m_pipeline_cache;
    }
} // namespace rendering_engine::gpu::backend::vulkan
