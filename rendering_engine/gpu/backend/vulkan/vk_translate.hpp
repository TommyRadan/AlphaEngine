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
 * @file vk_translate.hpp
 * @brief Mapping between backend-agnostic @c gpu::* enums and Vulkan
 *        constants. Mirrors @c gl_translate.hpp on the OpenGL side.
 */

#pragma once

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    VkPrimitiveTopology to_vk_topology(primitive_topology topology);
    VkBlendFactor to_vk_blend_factor(blend_factor factor);
    VkBlendOp to_vk_blend_op(blend_op op);
    VkCompareOp to_vk_compare(compare_function fn);
    VkCullModeFlags to_vk_cull_mode(cull_mode mode);
    VkFrontFace to_vk_front_face(front_face face);
    VkPolygonMode to_vk_polygon_mode(polygon_mode mode);
    VkSamplerAddressMode to_vk_address_mode(address_mode mode);
    VkFilter to_vk_filter(filter_mode mode);
    VkSamplerMipmapMode to_vk_mipmap_mode(mipmap_mode mode);
    VkIndexType to_vk_index_type(index_format format);
    VkFormat to_vk_vertex_format(scalar_type type, uint32_t components, bool normalized);
    VkShaderStageFlagBits to_vk_shader_stage(shader_stage stage);
    // The stageFlags of a descriptor binding or a push-constant range:
    // the stages @p stages names rather than every stage Vulkan knows. A
    // binding flagged for a geometry or tessellation stage whose feature
    // was never granted is what the validation layer objects to, and a
    // narrower mask is also what lets a driver place the data. An empty
    // mask falls back to the vertex + fragment pair rather than producing
    // an unusable layout.
    VkShaderStageFlags to_vk_stage_flags(shader_stages stages);
    VkAttachmentLoadOp to_vk_load_op(load_op op);
    VkAttachmentStoreOp to_vk_store_op(store_op op);
    VkFormat to_vk_format(texture_format format);
    VkStencilOp to_vk_stencil_op(stencil_op op);
    VkBorderColor to_vk_border_color(border_color color);
    VkColorComponentFlags to_vk_color_write_mask(color_write_mask mask);
    // @p count must be a power of two up to 64; anything else maps to
    // one sample.
    VkSampleCountFlagBits to_vk_sample_count(uint32_t count);
    // The image usage bits for @p usage on a colour or depth image.
    // Uploads and mip generation need the transfer bits, so the copy
    // flags always translate; the attachment bit follows the format.
    VkImageUsageFlags to_vk_image_usage(texture_usage usage, bool depth);
    // Engine usage bits a format with the given optimal-tiling
    // features supports (the inverse of the above, for
    // device::format_support).
    texture_usage to_texture_usage(VkFormatFeatureFlags features, bool depth);

    bool is_depth_format(texture_format format);
    VkImageAspectFlags aspect_for_format(texture_format format);

    // Decode the backend-agnostic barrier masks into precise Vulkan masks.
    // pipeline_stage_none / access_none decode to 0; callers substitute
    // TOP_OF_PIPE / BOTTOM_OF_PIPE for an empty stage mask as appropriate.
    // The storage_buffer/image access bits fold into SHADER_READ/WRITE since
    // Vulkan has no distinct storage-access bit.
    VkPipelineStageFlags to_vk_pipeline_stage(pipeline_stage stages);
    VkAccessFlags to_vk_access(access_flag flags);
} // namespace rendering_engine::gpu::backend::vulkan
