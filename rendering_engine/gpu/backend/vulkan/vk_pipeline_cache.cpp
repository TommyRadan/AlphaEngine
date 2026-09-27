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

#include <rendering_engine/gpu/backend/vulkan/vk_pipeline_cache.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

#include <core/hash.hpp>
#include <core/log.hpp>

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
} // namespace rendering_engine::gpu::backend::vulkan
