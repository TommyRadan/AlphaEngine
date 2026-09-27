// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/assets/texture_decode.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <ktx.h>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/vfs/vfs.hpp>
#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    namespace
    {
        // The VkFormat values a KTX2 header can carry that the engine
        // uploads. Spelled out here (they are fixed by the Vulkan registry)
        // so the asset layer does not include the Vulkan headers.
        constexpr ktx_uint32_t k_vk_r8g8b8a8_unorm = 37;
        constexpr ktx_uint32_t k_vk_r8g8b8a8_srgb = 43;
        constexpr ktx_uint32_t k_vk_bc1_rgb_unorm = 131;
        constexpr ktx_uint32_t k_vk_bc1_rgb_srgb = 132;
        constexpr ktx_uint32_t k_vk_bc1_rgba_unorm = 133;
        constexpr ktx_uint32_t k_vk_bc1_rgba_srgb = 134;
        constexpr ktx_uint32_t k_vk_bc3_unorm = 137;
        constexpr ktx_uint32_t k_vk_bc3_srgb = 138;
        constexpr ktx_uint32_t k_vk_bc4_unorm = 139;
        constexpr ktx_uint32_t k_vk_bc5_unorm = 141;
        constexpr ktx_uint32_t k_vk_bc7_unorm = 145;
        constexpr ktx_uint32_t k_vk_bc7_srgb = 146;
        constexpr ktx_uint32_t k_vk_astc_4x4_unorm = 157;
        constexpr ktx_uint32_t k_vk_astc_4x4_srgb = 158;

        struct ktx_texture_deleter
        {
            void operator()(ktxTexture2* texture) const noexcept
            {
                ktxTexture2_Destroy(texture);
            }
        };
        using ktx_texture_ptr = std::unique_ptr<ktxTexture2, ktx_texture_deleter>;

        // libktx builds the Basis transcoder's tables on first use behind an
        // unguarded static, so concurrent first transcodes on two workers
        // would race. Transcodes (and ASTC decodes) are serialised instead;
        // they are short next to the file read and inflate around them.
        std::mutex& transcode_mutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        [[noreturn]] void fail(const std::string& label, const std::string& reason)
        {
            LOG_ERR("Could not load KTX2 texture (%s): %s", label.c_str(), reason.c_str());
            throw std::runtime_error{"Could not load KTX2 texture (" + label + ")"};
        }

        bool sampled(const gpu::device& device, gpu::texture_format format)
        {
            return (device.format_support(format) & gpu::texture_usage_sampled) != 0u;
        }

        // The engine format a KTX2 vkFormat uploads as, in the colour space
        // the caller asked for (the bytes are the same; only the view
        // decodes sRGB or not). BC4 / BC5 have no sRGB form. BC1 without
        // alpha reads as BC1 with it: the transcoder and the common
        // encoders emit no punch-through blocks for opaque content.
        std::optional<gpu::texture_format> engine_format(ktx_uint32_t vk_format, gpu::color_space space)
        {
            const bool srgb = space == gpu::color_space::srgb;
            switch (vk_format)
            {
            case k_vk_r8g8b8a8_unorm:
            case k_vk_r8g8b8a8_srgb:
                return gpu::rgba8_format(space);
            case k_vk_bc1_rgb_unorm:
            case k_vk_bc1_rgb_srgb:
            case k_vk_bc1_rgba_unorm:
            case k_vk_bc1_rgba_srgb:
                return srgb ? gpu::texture_format::bc1_rgba_srgb : gpu::texture_format::bc1_rgba_unorm;
            case k_vk_bc3_unorm:
            case k_vk_bc3_srgb:
                return srgb ? gpu::texture_format::bc3_rgba_srgb : gpu::texture_format::bc3_rgba_unorm;
            case k_vk_bc4_unorm:
                return gpu::texture_format::bc4_r_unorm;
            case k_vk_bc5_unorm:
                return gpu::texture_format::bc5_rg_unorm;
            case k_vk_bc7_unorm:
            case k_vk_bc7_srgb:
                return srgb ? gpu::texture_format::bc7_rgba_srgb : gpu::texture_format::bc7_rgba_unorm;
            case k_vk_astc_4x4_unorm:
            case k_vk_astc_4x4_srgb:
                return srgb ? gpu::texture_format::astc_4x4_srgb : gpu::texture_format::astc_4x4_unorm;
            default:
                return std::nullopt;
            }
        }

        const char* format_name(gpu::texture_format format)
        {
            switch (format)
            {
            case gpu::texture_format::rgba8_unorm:
                return "RGBA8";
            case gpu::texture_format::rgba8_srgb:
                return "RGBA8 sRGB";
            case gpu::texture_format::bc1_rgba_unorm:
                return "BC1";
            case gpu::texture_format::bc1_rgba_srgb:
                return "BC1 sRGB";
            case gpu::texture_format::bc3_rgba_unorm:
                return "BC3";
            case gpu::texture_format::bc3_rgba_srgb:
                return "BC3 sRGB";
            case gpu::texture_format::bc4_r_unorm:
                return "BC4";
            case gpu::texture_format::bc5_rg_unorm:
                return "BC5";
            case gpu::texture_format::bc7_rgba_unorm:
                return "BC7";
            case gpu::texture_format::bc7_rgba_srgb:
                return "BC7 sRGB";
            case gpu::texture_format::astc_4x4_unorm:
                return "ASTC 4x4";
            case gpu::texture_format::astc_4x4_srgb:
                return "ASTC 4x4 sRGB";
            default:
                return "other";
            }
        }

        // The Basis Universal transcode target for @p components channels
        // authored in @p space: the one-/two-channel data formats for linear
        // content that fits them, then the best general-purpose family the
        // device samples, then uncompressed RGBA8.
        ktx_transcode_fmt_e
        transcode_target(ktx_uint32_t components, gpu::color_space space, const compressed_format_support& support)
        {
            if (space == gpu::color_space::linear && components == 1 && support.bc4)
            {
                return KTX_TTF_BC4_R;
            }
            if (space == gpu::color_space::linear && components == 2 && support.bc5)
            {
                return KTX_TTF_BC5_RG;
            }
            if (support.bc7)
            {
                return KTX_TTF_BC7_RGBA;
            }
            if (support.astc_4x4)
            {
                return KTX_TTF_ASTC_4x4_RGBA;
            }
            // Two channels in a colour texture are luminance + alpha.
            const bool alpha = components == 2 || components == 4;
            if (alpha && support.bc3)
            {
                return KTX_TTF_BC3_RGBA;
            }
            if (!alpha && support.bc1)
            {
                return KTX_TTF_BC1_RGB;
            }
            return KTX_TTF_RGBA32;
        }

        std::string error_text(KTX_error_code code)
        {
            const char* text = ktxErrorString(code);
            return text != nullptr ? text : "unknown libktx error";
        }
    } // namespace

    compressed_format_support compressed_format_support::query(const gpu::device& device)
    {
        compressed_format_support support;
        support.bc1 =
            sampled(device, gpu::texture_format::bc1_rgba_unorm) && sampled(device, gpu::texture_format::bc1_rgba_srgb);
        support.bc3 =
            sampled(device, gpu::texture_format::bc3_rgba_unorm) && sampled(device, gpu::texture_format::bc3_rgba_srgb);
        support.bc4 = sampled(device, gpu::texture_format::bc4_r_unorm);
        support.bc5 = sampled(device, gpu::texture_format::bc5_rg_unorm);
        support.bc7 =
            sampled(device, gpu::texture_format::bc7_rgba_unorm) && sampled(device, gpu::texture_format::bc7_rgba_srgb);
        support.astc_4x4 =
            sampled(device, gpu::texture_format::astc_4x4_unorm) && sampled(device, gpu::texture_format::astc_4x4_srgb);
        return support;
    }

    bool compressed_format_support::supports(gpu::texture_format format) const
    {
        switch (format)
        {
        case gpu::texture_format::bc1_rgba_unorm:
        case gpu::texture_format::bc1_rgba_srgb:
            return bc1;
        case gpu::texture_format::bc3_rgba_unorm:
        case gpu::texture_format::bc3_rgba_srgb:
            return bc3;
        case gpu::texture_format::bc4_r_unorm:
            return bc4;
        case gpu::texture_format::bc5_rg_unorm:
            return bc5;
        case gpu::texture_format::bc7_rgba_unorm:
        case gpu::texture_format::bc7_rgba_srgb:
            return bc7;
        case gpu::texture_format::astc_4x4_unorm:
        case gpu::texture_format::astc_4x4_srgb:
            return astc_4x4;
        default:
            return true;
        }
    }

    bool is_ktx2_path(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(),
                       extension.end(),
                       extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".ktx2";
    }

    decoded_texture decode_texture_file(const std::filesystem::path& path,
                                        gpu::color_space space,
                                        const compressed_format_support& support)
    {
        const std::string label = core::os::path_to_utf8(path);
        if (!is_ktx2_path(path))
        {
            decoded_texture decoded;
            decoded.image = image{label};
            decoded.width = decoded.image.get_width();
            decoded.height = decoded.image.get_height();
            decoded.format = gpu::rgba8_format(space);
            return decoded;
        }

        std::vector<std::byte> bytes;
        std::string error;
        if (!core::default_vfs().read_file(path, bytes, &error))
        {
            fail(label, error);
        }
        return decode_ktx2(bytes.data(), bytes.size(), space, support, label);
    }

    decoded_texture decode_ktx2(const std::byte* bytes,
                                std::size_t size,
                                gpu::color_space space,
                                const compressed_format_support& support,
                                const std::string& label)
    {
        if (bytes == nullptr || size == 0)
        {
            fail(label, "empty file");
        }

        ktxTexture2* raw = nullptr;
        const KTX_error_code created = ktxTexture2_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t*>(bytes), size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
        if (created != KTX_SUCCESS || raw == nullptr)
        {
            fail(label, "not a readable KTX2 file (" + error_text(created) + ")");
        }
        const ktx_texture_ptr texture{raw};

        if (texture->numDimensions != 2 || texture->baseDepth > 1 || texture->numLayers > 1 || texture->numFaces != 1 ||
            texture->isArray || texture->isCubemap)
        {
            fail(label, "only single-layer 2D textures are supported (no arrays, cube maps or volumes)");
        }
        if (texture->baseWidth == 0 || texture->baseHeight == 0 || texture->numLevels == 0)
        {
            fail(label, "the file has no image data");
        }

        bool transcoded = false;
        if (ktxTexture2_NeedsTranscoding(texture.get()))
        {
            const ktx_transcode_fmt_e target =
                transcode_target(ktxTexture2_GetNumComponents(texture.get()), space, support);
            KTX_error_code result = KTX_SUCCESS;
            {
                const std::lock_guard<std::mutex> lock{transcode_mutex()};
                result = ktxTexture2_TranscodeBasis(texture.get(), target, 0);
            }
            if (result != KTX_SUCCESS)
            {
                fail(label, "Basis Universal transcode failed (" + error_text(result) + ")");
            }
            transcoded = true;
        }

        std::optional<gpu::texture_format> format = engine_format(texture->vkFormat, space);
        if (!format.has_value() || !support.supports(*format))
        {
            // ASTC the device cannot sample (most desktop GPUs), or in a
            // block size the engine has no format for, decodes to RGBA8
            // through the ASTC decoder libktx links; nothing else has a
            // fallback.
            if (ktxTexture2_GetColorModel_e(texture.get()) != KHR_DF_MODEL_ASTC)
            {
                fail(label,
                     format.has_value()
                         ? std::string{"the device cannot sample "} + format_name(*format)
                         : "VkFormat " + std::to_string(texture->vkFormat) + " is not a format the engine uploads");
            }
            KTX_error_code result = KTX_SUCCESS;
            {
                const std::lock_guard<std::mutex> lock{transcode_mutex()};
                result = ktxTexture2_DecodeAstc(texture.get());
            }
            format = engine_format(texture->vkFormat, space);
            if (result != KTX_SUCCESS || !format.has_value() || gpu::is_compressed_texture_format(*format))
            {
                fail(label, "ASTC the device cannot sample could not be decoded (" + error_text(result) + ")");
            }
        }

        decoded_texture decoded;
        decoded.format = *format;
        decoded.width = texture->baseWidth;
        decoded.height = texture->baseHeight;
        auto* base = ktxTexture(texture.get());
        const ktx_uint8_t* data = ktxTexture_GetData(base);
        const ktx_size_t data_size = ktxTexture_GetDataSize(base);
        const uint32_t level_count = texture->numLevels;
        decoded.levels.reserve(level_count);
        for (uint32_t level = 0; level < level_count; ++level)
        {
            const uint32_t width = std::max(1u, decoded.width >> level);
            const uint32_t height = std::max(1u, decoded.height >> level);
            ktx_size_t offset = 0;
            if (ktxTexture_GetImageOffset(base, level, 0, 0, &offset) != KTX_SUCCESS)
            {
                fail(label, "level " + std::to_string(level) + " has no image");
            }
            const std::size_t expected = gpu::texture_image_bytes(decoded.format, width, height);
            const ktx_size_t image_size = ktxTexture_GetImageSize(base, level);
            if (image_size != expected || offset > data_size || data_size - offset < expected)
            {
                fail(label,
                     "level " + std::to_string(level) + " holds " + std::to_string(image_size) + " bytes, expected " +
                         std::to_string(expected));
            }
            const auto* first = reinterpret_cast<const std::byte*>(data + offset);
            decoded.levels.emplace_back(first, first + expected);
        }

        // One RGBA8 level: hand it over as an image so the device derives
        // the chain, like any PNG.
        if (!gpu::is_compressed_texture_format(decoded.format) && decoded.levels.size() == 1)
        {
            const std::size_t texels = static_cast<std::size_t>(decoded.width) * decoded.height;
            auto pixels = std::make_unique<color[]>(texels);
            std::memcpy(pixels.get(), decoded.levels.front().data(), texels * sizeof(color));
            decoded.image = image{decoded.width, decoded.height, pixels.release()};
            decoded.levels.clear();
        }

        LOG_INF("Loaded KTX2 texture (%s): %ux%u, %u level%s, %s%s",
                label.c_str(),
                decoded.width,
                decoded.height,
                level_count,
                level_count == 1 ? "" : "s",
                format_name(decoded.format),
                transcoded ? " (transcoded from Basis Universal)" : "");
        return decoded;
    }
} // namespace rendering_engine
