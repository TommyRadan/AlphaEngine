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
    VkFormat to_vk_vertex_format(scalar_type type, uint32_t components);
    VkShaderStageFlagBits to_vk_shader_stage(shader_stage stage);
    VkAttachmentLoadOp to_vk_load_op(load_op op);
    VkAttachmentStoreOp to_vk_store_op(store_op op);
    VkFormat to_vk_format(texture_format format);

    bool is_depth_format(texture_format format);
    VkImageAspectFlags aspect_for_format(texture_format format);
} // namespace rendering_engine::gpu::backend::vulkan
