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

    VkFormat to_vk_vertex_format(scalar_type type, uint32_t components)
    {
        switch (type)
        {
        case scalar_type::float32:
            switch (components)
            {
            case 1:
                return VK_FORMAT_R32_SFLOAT;
            case 2:
                return VK_FORMAT_R32G32_SFLOAT;
            case 3:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case 4:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            default:
                break;
            }
            break;
        case scalar_type::int32:
            return components == 4 ? VK_FORMAT_R32G32B32A32_SINT
                                   : (components == 3 ? VK_FORMAT_R32G32B32_SINT
                                                      : (components == 2 ? VK_FORMAT_R32G32_SINT : VK_FORMAT_R32_SINT));
        case scalar_type::uint32:
            return components == 4 ? VK_FORMAT_R32G32B32A32_UINT
                                   : (components == 3 ? VK_FORMAT_R32G32B32_UINT
                                                      : (components == 2 ? VK_FORMAT_R32G32_UINT : VK_FORMAT_R32_UINT));
        default:
            break;
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
        case texture_format::rgb8_unorm:
            // R8G8B8 isn't broadly supported as a sampled format on
            // Vulkan; widen to rgba8 and let the upload path pad.
            return VK_FORMAT_R8G8B8A8_UNORM;
        case texture_format::r8_unorm:
            return VK_FORMAT_R8_UNORM;
        case texture_format::rgba16_float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case texture_format::rgba32_float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case texture_format::depth24:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
        case texture_format::depth32_float:
            return VK_FORMAT_D32_SFLOAT;
        case texture_format::depth24_stencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        }
        return VK_FORMAT_R8G8B8A8_UNORM;
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
} // namespace rendering_engine::gpu::backend::vulkan
