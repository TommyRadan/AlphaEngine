// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    VkPrimitiveTopology to_vk_topology(primitive_topology topology)
    {
        switch (topology)
        {
        case primitive_topology::triangles:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case primitive_topology::lines:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case primitive_topology::points:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case primitive_topology::patches:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    VkBlendFactor to_vk_blend_factor(blend_factor factor)
    {
        switch (factor)
        {
        case blend_factor::zero:
            return VK_BLEND_FACTOR_ZERO;
        case blend_factor::one:
            return VK_BLEND_FACTOR_ONE;
        case blend_factor::src_color:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case blend_factor::one_minus_src_color:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case blend_factor::dst_color:
            return VK_BLEND_FACTOR_DST_COLOR;
        case blend_factor::one_minus_dst_color:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case blend_factor::src_alpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case blend_factor::one_minus_src_alpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case blend_factor::dst_alpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case blend_factor::one_minus_dst_alpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        }
        return VK_BLEND_FACTOR_ONE;
    }

    VkBlendOp to_vk_blend_op(blend_op op)
    {
        switch (op)
        {
        case blend_op::add:
            return VK_BLEND_OP_ADD;
        case blend_op::subtract:
            return VK_BLEND_OP_SUBTRACT;
        case blend_op::reverse_subtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case blend_op::min:
            return VK_BLEND_OP_MIN;
        case blend_op::max:
            return VK_BLEND_OP_MAX;
        }
        return VK_BLEND_OP_ADD;
    }

    VkCompareOp to_vk_compare(compare_function fn)
    {
        switch (fn)
        {
        case compare_function::never:
            return VK_COMPARE_OP_NEVER;
        case compare_function::less:
            return VK_COMPARE_OP_LESS;
        case compare_function::equal:
            return VK_COMPARE_OP_EQUAL;
        case compare_function::less_equal:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case compare_function::greater:
            return VK_COMPARE_OP_GREATER;
        case compare_function::not_equal:
            return VK_COMPARE_OP_NOT_EQUAL;
        case compare_function::greater_equal:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case compare_function::always:
            return VK_COMPARE_OP_ALWAYS;
        }
        return VK_COMPARE_OP_LESS;
    }

    VkCullModeFlags to_vk_cull_mode(cull_mode mode)
    {
        switch (mode)
        {
        case cull_mode::none:
            return VK_CULL_MODE_NONE;
        case cull_mode::front:
            return VK_CULL_MODE_FRONT_BIT;
        case cull_mode::back:
            return VK_CULL_MODE_BACK_BIT;
        }
        return VK_CULL_MODE_BACK_BIT;
    }

    VkFrontFace to_vk_front_face(front_face face)
    {
        // Direct mapping. Pipeline variants for swapchain targets
        // run through a negative-height viewport, which inverts the
        // triangle winding in screen space; that variant flips the
        // result of this function at build time. Off-screen
        // pipelines render in Vulkan-natural Y-down so winding is
        // preserved here.
        return face == front_face::clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }

    VkPolygonMode to_vk_polygon_mode(polygon_mode mode)
    {
        switch (mode)
        {
        case polygon_mode::fill:
            return VK_POLYGON_MODE_FILL;
        case polygon_mode::line:
            return VK_POLYGON_MODE_LINE;
        case polygon_mode::point:
            return VK_POLYGON_MODE_POINT;
        }
        return VK_POLYGON_MODE_FILL;
    }

    VkSamplerAddressMode to_vk_address_mode(address_mode mode)
    {
        switch (mode)
        {
        case address_mode::clamp_edge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case address_mode::clamp_border:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case address_mode::repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case address_mode::mirrored_repeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        }
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    VkFilter to_vk_filter(filter_mode mode)
    {
        return mode == filter_mode::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    }

    VkSamplerMipmapMode to_vk_mipmap_mode(mipmap_mode mode)
    {
        return mode == mipmap_mode::linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    VkIndexType to_vk_index_type(index_format format)
    {
        return format == index_format::uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    }

    VkFormat to_vk_vertex_format(scalar_type type, uint32_t components, bool normalized)
    {
        // Indexed by component count - 1. The 32-bit integer types have
        // no normalised Vulkan vertex format, so @p normalized only
        // selects the UNORM / SNORM family for the 16- and 8-bit types.
        static constexpr VkFormat float32_formats[4] = {
            VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
        static constexpr VkFormat int32_formats[4] = {
            VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT};
        static constexpr VkFormat uint32_formats[4] = {
            VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT};
        static constexpr VkFormat int16_formats[4] = {
            VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT};
        static constexpr VkFormat int16_norm_formats[4] = {
            VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM};
        static constexpr VkFormat uint16_formats[4] = {
            VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT};
        static constexpr VkFormat uint16_norm_formats[4] = {
            VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM};
        static constexpr VkFormat int8_formats[4] = {
            VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT};
        static constexpr VkFormat int8_norm_formats[4] = {
            VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM};
        static constexpr VkFormat uint8_formats[4] = {
            VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT};
        static constexpr VkFormat uint8_norm_formats[4] = {
            VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};

        if (components < 1 || components > 4)
        {
            return VK_FORMAT_R32G32B32_SFLOAT;
        }
        const uint32_t index = components - 1;
        switch (type)
        {
        case scalar_type::float32:
            return float32_formats[index];
        case scalar_type::int32:
            return int32_formats[index];
        case scalar_type::uint32:
            return uint32_formats[index];
        case scalar_type::int16:
            return normalized ? int16_norm_formats[index] : int16_formats[index];
        case scalar_type::uint16:
            return normalized ? uint16_norm_formats[index] : uint16_formats[index];
        case scalar_type::int8:
            return normalized ? int8_norm_formats[index] : int8_formats[index];
        case scalar_type::uint8:
            return normalized ? uint8_norm_formats[index] : uint8_formats[index];
        }
        return VK_FORMAT_R32G32B32_SFLOAT;
    }

    VkShaderStageFlagBits to_vk_shader_stage(shader_stage stage)
    {
        switch (stage)
        {
        case shader_stage::vertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case shader_stage::fragment:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case shader_stage::geometry:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case shader_stage::tessellation_control:
            return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case shader_stage::tessellation_evaluation:
            return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case shader_stage::compute:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        }
        return VK_SHADER_STAGE_VERTEX_BIT;
    }

    VkShaderStageFlags to_vk_stage_flags(shader_stages stages)
    {
        VkShaderStageFlags out = 0;
        if ((stages & shader_stages_vertex) != 0u)
        {
            out |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if ((stages & shader_stages_fragment) != 0u)
        {
            out |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if ((stages & shader_stages_geometry) != 0u)
        {
            out |= VK_SHADER_STAGE_GEOMETRY_BIT;
        }
        if ((stages & shader_stages_tessellation_control) != 0u)
        {
            out |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        }
        if ((stages & shader_stages_tessellation_evaluation) != 0u)
        {
            out |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        }
        if ((stages & shader_stages_compute) != 0u)
        {
            out |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        return out != 0u ? out : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    VkAttachmentLoadOp to_vk_load_op(load_op op)
    {
        switch (op)
        {
        case load_op::load:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case load_op::clear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case load_op::dont_care:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    VkAttachmentStoreOp to_vk_store_op(store_op op)
    {
        return op == store_op::store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    VkFormat to_vk_format(texture_format format)
    {
        switch (format)
        {
        case texture_format::rgba8_unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case texture_format::rgba8_srgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case texture_format::rgb8_unorm:
            // R8G8B8 isn't broadly supported as a sampled format on
            // Vulkan; widen to rgba8. The texture upload path pads a
            // 3-byte-per-texel source to RGBA8 with an opaque alpha
            // (vk_device_texture.cpp, upload_region).
            return VK_FORMAT_R8G8B8A8_UNORM;
        case texture_format::r8_unorm:
            return VK_FORMAT_R8_UNORM;
        case texture_format::rgba16_float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case texture_format::rgba32_float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        // The depth cases are the *preferred* format only. Neither
        // packed 24-bit format is a mandatory depth attachment, so the
        // device resolves each depth format once at init through the
        // fallback chains in vk_negotiate.hpp and serves the result
        // from vk_device::vk_format_for; every image and render pass
        // goes through that, not through this table.
        case texture_format::depth24:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
        case texture_format::depth32_float:
            return VK_FORMAT_D32_SFLOAT;
        case texture_format::depth24_stencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        // Block-compressed: sampleable only where format_support says so
        // (textureCompressionBC / textureCompressionASTC_LDR).
        case texture_format::bc1_rgba_unorm:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case texture_format::bc1_rgba_srgb:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case texture_format::bc3_rgba_unorm:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case texture_format::bc3_rgba_srgb:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case texture_format::bc4_r_unorm:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case texture_format::bc5_rg_unorm:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case texture_format::bc7_rgba_unorm:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case texture_format::bc7_rgba_srgb:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case texture_format::astc_4x4_unorm:
            return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case texture_format::astc_4x4_srgb:
            return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        }
        return VK_FORMAT_R8G8B8A8_UNORM;
    }

    VkStencilOp to_vk_stencil_op(stencil_op op)
    {
        switch (op)
        {
        case stencil_op::keep:
            return VK_STENCIL_OP_KEEP;
        case stencil_op::zero:
            return VK_STENCIL_OP_ZERO;
        case stencil_op::replace:
            return VK_STENCIL_OP_REPLACE;
        case stencil_op::increment_clamp:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case stencil_op::decrement_clamp:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case stencil_op::invert:
            return VK_STENCIL_OP_INVERT;
        case stencil_op::increment_wrap:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case stencil_op::decrement_wrap:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        }
        return VK_STENCIL_OP_KEEP;
    }

    VkBorderColor to_vk_border_color(border_color color)
    {
        switch (color)
        {
        case border_color::transparent_black:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case border_color::opaque_black:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case border_color::opaque_white:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        }
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }

    VkColorComponentFlags to_vk_color_write_mask(color_write_mask mask)
    {
        VkColorComponentFlags out = 0;
        if ((mask & color_write_red) != 0u)
        {
            out |= VK_COLOR_COMPONENT_R_BIT;
        }
        if ((mask & color_write_green) != 0u)
        {
            out |= VK_COLOR_COMPONENT_G_BIT;
        }
        if ((mask & color_write_blue) != 0u)
        {
            out |= VK_COLOR_COMPONENT_B_BIT;
        }
        if ((mask & color_write_alpha) != 0u)
        {
            out |= VK_COLOR_COMPONENT_A_BIT;
        }
        return out;
    }

    VkSampleCountFlagBits to_vk_sample_count(uint32_t count)
    {
        switch (count)
        {
        case 2:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16:
            return VK_SAMPLE_COUNT_16_BIT;
        case 32:
            return VK_SAMPLE_COUNT_32_BIT;
        case 64:
            return VK_SAMPLE_COUNT_64_BIT;
        default:
            return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    VkImageUsageFlags to_vk_image_usage(texture_usage usage, bool depth)
    {
        // The transfer bits are unconditional: every upload, mip blit
        // and readback the device records needs them, whatever the
        // caller asked for.
        VkImageUsageFlags out = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((usage & texture_usage_sampled) != 0u)
        {
            out |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if ((usage & texture_usage_render_attachment) != 0u)
        {
            out |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if ((usage & texture_usage_storage) != 0u)
        {
            out |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        return out;
    }

    texture_usage to_texture_usage(VkFormatFeatureFlags features, bool depth)
    {
        texture_usage out = 0;
        if ((features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0u)
        {
            out |= texture_usage_sampled;
        }
        const VkFormatFeatureFlags attachment_bit =
            depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if ((features & attachment_bit) != 0u)
        {
            out |= texture_usage_render_attachment;
        }
        if ((features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0u)
        {
            out |= texture_usage_storage;
        }
        if ((features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0u)
        {
            out |= texture_usage_copy_src;
        }
        if ((features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0u)
        {
            out |= texture_usage_copy_dst;
        }
        return out;
    }

    bool is_depth_format(texture_format format)
    {
        switch (format)
        {
        case texture_format::depth24:
        case texture_format::depth32_float:
        case texture_format::depth24_stencil8:
            return true;
        default:
            return false;
        }
    }

    VkImageAspectFlags aspect_for_format(texture_format format)
    {
        switch (format)
        {
        case texture_format::depth24:
        case texture_format::depth32_float:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case texture_format::depth24_stencil8:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    VkPipelineStageFlags to_vk_pipeline_stage(pipeline_stage stages)
    {
        // The convenience superset maps straight to its Vulkan twin; the
        // per-bit decode below would otherwise miss the unnamed high bits.
        if (stages == pipeline_stage_all_commands)
        {
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
        VkPipelineStageFlags out = 0;
        if ((stages & pipeline_stage_vertex_input) != 0u)
        {
            out |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
        if ((stages & pipeline_stage_vertex_shader) != 0u)
        {
            out |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        }
        if ((stages & pipeline_stage_tessellation_control_shader) != 0u)
        {
            out |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
        }
        if ((stages & pipeline_stage_tessellation_evaluation_shader) != 0u)
        {
            out |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
        }
        if ((stages & pipeline_stage_geometry_shader) != 0u)
        {
            out |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
        }
        if ((stages & pipeline_stage_fragment_shader) != 0u)
        {
            out |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        if ((stages & pipeline_stage_compute_shader) != 0u)
        {
            out |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        if ((stages & pipeline_stage_color_attachment_output) != 0u)
        {
            out |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        if ((stages & pipeline_stage_early_fragment_tests) != 0u)
        {
            out |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        if ((stages & pipeline_stage_late_fragment_tests) != 0u)
        {
            out |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        }
        if ((stages & pipeline_stage_transfer) != 0u)
        {
            out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        if ((stages & pipeline_stage_draw_indirect) != 0u)
        {
            out |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        }
        return out;
    }

    VkAccessFlags to_vk_access(access_flag flags)
    {
        VkAccessFlags out = 0;
        if ((flags & access_indirect_command_read) != 0u)
        {
            out |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        }
        if ((flags & access_index_read) != 0u)
        {
            out |= VK_ACCESS_INDEX_READ_BIT;
        }
        if ((flags & access_vertex_attribute_read) != 0u)
        {
            out |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        }
        if ((flags & access_uniform_read) != 0u)
        {
            out |= VK_ACCESS_UNIFORM_READ_BIT;
        }
        if ((flags & access_shader_read) != 0u)
        {
            out |= VK_ACCESS_SHADER_READ_BIT;
        }
        if ((flags & access_shader_write) != 0u)
        {
            out |= VK_ACCESS_SHADER_WRITE_BIT;
        }
        if ((flags & access_color_attachment_read) != 0u)
        {
            out |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        }
        if ((flags & access_color_attachment_write) != 0u)
        {
            out |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        if ((flags & access_depth_stencil_attachment_read) != 0u)
        {
            out |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }
        if ((flags & access_depth_stencil_attachment_write) != 0u)
        {
            out |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        if ((flags & access_transfer_read) != 0u)
        {
            out |= VK_ACCESS_TRANSFER_READ_BIT;
        }
        if ((flags & access_transfer_write) != 0u)
        {
            out |= VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        // Vulkan has no dedicated storage-buffer / storage-image access bit;
        // both read paths are SHADER_READ and both writes are SHADER_WRITE.
        if ((flags & (access_storage_buffer_read | access_storage_image_read)) != 0u)
        {
            out |= VK_ACCESS_SHADER_READ_BIT;
        }
        if ((flags & (access_storage_buffer_write | access_storage_image_write)) != 0u)
        {
            out |= VK_ACCESS_SHADER_WRITE_BIT;
        }
        return out;
    }
} // namespace rendering_engine::gpu::backend::vulkan
