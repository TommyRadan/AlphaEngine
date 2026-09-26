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

// Device-free tests for the Vulkan backend's bring-up decisions
// (rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp): the depth-format
// fallback chains, the aspect of a resolved format and the descriptor-pool
// budget growth. The format-support query the device backs with
// vkGetPhysicalDeviceFormatProperties is a table here; no loader is touched.

#include <gtest/gtest.h>

#include <initializer_list>
#include <set>

#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>

namespace
{
    namespace vk = rendering_engine::gpu::backend::vulkan;
    using rendering_engine::gpu::texture_format;

    // A support query that accepts exactly the listed formats as depth
    // attachments.
    vk::depth_attachment_query supports_only(std::initializer_list<VkFormat> formats)
    {
        std::set<VkFormat> supported(formats);
        return [supported](VkFormat format) { return supported.count(format) != 0; };
    }

    // What a conformant desktop driver reports: every depth format.
    vk::depth_attachment_query supports_everything()
    {
        return [](VkFormat) { return true; };
    }
} // namespace

// ---- depth-format chains ---------------------------------------------------

TEST(vk_format_fallback, depth24_prefers_the_packed_24_bit_format)
{
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24, supports_everything()), VK_FORMAT_X8_D24_UNORM_PACK32);
}

TEST(vk_format_fallback, depth24_falls_back_to_d32_sfloat_when_the_packed_format_is_missing)
{
    // MoltenVK and much mobile hardware: no X8_D24, but the mandatory D32.
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24, supports_only({VK_FORMAT_D32_SFLOAT})),
              VK_FORMAT_D32_SFLOAT);
    // D32 wins over the stencil formats even when those are present.
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24,
                                      supports_only({VK_FORMAT_D32_SFLOAT,
                                                     VK_FORMAT_D24_UNORM_S8_UINT,
                                                     VK_FORMAT_D32_SFLOAT_S8_UINT})),
              VK_FORMAT_D32_SFLOAT);
}

TEST(vk_format_fallback, depth24_reaches_the_stencil_formats_last)
{
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24, supports_only({VK_FORMAT_D24_UNORM_S8_UINT})),
              VK_FORMAT_D24_UNORM_S8_UINT);
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24, supports_only({VK_FORMAT_D32_SFLOAT_S8_UINT})),
              VK_FORMAT_D32_SFLOAT_S8_UINT);
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24,
                                      supports_only({VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT})),
              VK_FORMAT_D24_UNORM_S8_UINT);
}

TEST(vk_format_fallback, depth32_float_resolves_to_d32_sfloat)
{
    EXPECT_EQ(vk::select_depth_format(texture_format::depth32_float, supports_everything()), VK_FORMAT_D32_SFLOAT);
    EXPECT_EQ(vk::select_depth_format(texture_format::depth32_float, supports_only({VK_FORMAT_D32_SFLOAT})),
              VK_FORMAT_D32_SFLOAT);
    // The stencil variant only when the plain format is refused.
    EXPECT_EQ(vk::select_depth_format(texture_format::depth32_float, supports_only({VK_FORMAT_D32_SFLOAT_S8_UINT})),
              VK_FORMAT_D32_SFLOAT_S8_UINT);
}

TEST(vk_format_fallback, depth24_stencil8_prefers_d24s8_then_d32s8)
{
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24_stencil8, supports_everything()),
              VK_FORMAT_D24_UNORM_S8_UINT);
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24_stencil8, supports_only({VK_FORMAT_D32_SFLOAT_S8_UINT})),
              VK_FORMAT_D32_SFLOAT_S8_UINT);
    // A depth-only format never stands in for a stencil request.
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24_stencil8,
                                      supports_only({VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32})),
              VK_FORMAT_UNDEFINED);
}

TEST(vk_format_fallback, nothing_supported_resolves_to_undefined)
{
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24, supports_only({})), VK_FORMAT_UNDEFINED);
    EXPECT_EQ(vk::select_depth_format(texture_format::depth32_float, supports_only({})), VK_FORMAT_UNDEFINED);
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24_stencil8, supports_only({})), VK_FORMAT_UNDEFINED);
}

TEST(vk_format_fallback, a_missing_query_resolves_to_undefined)
{
    EXPECT_EQ(vk::select_depth_format(texture_format::depth24, vk::depth_attachment_query{}), VK_FORMAT_UNDEFINED);
}

TEST(vk_format_fallback, colour_formats_have_no_chain)
{
    EXPECT_TRUE(vk::depth_format_candidates(texture_format::rgba8_unorm).empty());
    EXPECT_TRUE(vk::depth_format_candidates(texture_format::rgba16_float).empty());
    EXPECT_TRUE(vk::depth_format_candidates(texture_format::r8_unorm).empty());
    EXPECT_EQ(vk::select_depth_format(texture_format::rgba8_unorm, supports_everything()), VK_FORMAT_UNDEFINED);
}

TEST(vk_format_fallback, chains_start_with_the_nominal_translation_and_end_in_a_mandatory_format)
{
    const auto depth24 = vk::depth_format_candidates(texture_format::depth24);
    ASSERT_FALSE(depth24.empty());
    EXPECT_EQ(depth24.front(), VK_FORMAT_X8_D24_UNORM_PACK32);
    // Every depth-only chain passes through D32_SFLOAT, the one format the
    // spec guarantees as a depth attachment, before any stencil format.
    bool saw_d32 = false;
    for (const VkFormat format : depth24)
    {
        if (format == VK_FORMAT_D32_SFLOAT)
        {
            saw_d32 = true;
        }
        else if (vk::aspect_for_vk_format(format) != VK_IMAGE_ASPECT_DEPTH_BIT)
        {
            EXPECT_TRUE(saw_d32) << "a stencil format precedes D32_SFLOAT in the depth24 chain";
        }
    }
    EXPECT_TRUE(saw_d32);

    const auto depth32 = vk::depth_format_candidates(texture_format::depth32_float);
    ASSERT_FALSE(depth32.empty());
    EXPECT_EQ(depth32.front(), VK_FORMAT_D32_SFLOAT);

    const auto depth24_stencil8 = vk::depth_format_candidates(texture_format::depth24_stencil8);
    ASSERT_FALSE(depth24_stencil8.empty());
    EXPECT_EQ(depth24_stencil8.front(), VK_FORMAT_D24_UNORM_S8_UINT);
    for (const VkFormat format : depth24_stencil8)
    {
        EXPECT_EQ(vk::aspect_for_vk_format(format), VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    }
}

// ---- image aspect of a resolved format -------------------------------------

TEST(vk_format_fallback, aspect_follows_the_resolved_format)
{
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_D32_SFLOAT), VK_IMAGE_ASPECT_DEPTH_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_X8_D24_UNORM_PACK32), VK_IMAGE_ASPECT_DEPTH_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_D16_UNORM), VK_IMAGE_ASPECT_DEPTH_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_D24_UNORM_S8_UINT),
              VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_D32_SFLOAT_S8_UINT),
              VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_S8_UINT), VK_IMAGE_ASPECT_STENCIL_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_R8G8B8A8_UNORM), VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_R16G16B16A16_SFLOAT), VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_EQ(vk::aspect_for_vk_format(VK_FORMAT_UNDEFINED), VK_IMAGE_ASPECT_COLOR_BIT);
}

// ---- descriptor-pool budget ------------------------------------------------

TEST(vk_descriptor_pool_budget, the_first_pool_holds_the_base_budget)
{
    const vk::descriptor_pool_budget first = vk::descriptor_pool_budget_for(0);
    EXPECT_EQ(first.max_sets, 256u);
    EXPECT_GT(first.uniform_buffers, 0u);
    EXPECT_GT(first.combined_image_samplers, 0u);
    EXPECT_GT(first.storage_buffers, 0u);
    EXPECT_GT(first.storage_images, 0u);
}

TEST(vk_descriptor_pool_budget, every_pool_can_hold_its_sets_worth_of_material_descriptors)
{
    // A standard_material set is one uniform buffer plus up to eight
    // combined image samplers; a per-draw set is one uniform buffer. The
    // budget must not run out of descriptors before it runs out of sets.
    for (uint32_t index = 0; index < 8; ++index)
    {
        const vk::descriptor_pool_budget budget = vk::descriptor_pool_budget_for(index);
        EXPECT_GE(budget.uniform_buffers, budget.max_sets) << "pool " << index;
        EXPECT_GE(budget.combined_image_samplers, budget.max_sets * 8u) << "pool " << index;
    }
}

TEST(vk_descriptor_pool_budget, each_pool_doubles_the_previous_one)
{
    vk::descriptor_pool_budget previous = vk::descriptor_pool_budget_for(0);
    for (uint32_t index = 1; index <= 4; ++index)
    {
        const vk::descriptor_pool_budget budget = vk::descriptor_pool_budget_for(index);
        EXPECT_EQ(budget.max_sets, previous.max_sets * 2u) << "pool " << index;
        EXPECT_EQ(budget.uniform_buffers, previous.uniform_buffers * 2u) << "pool " << index;
        EXPECT_EQ(budget.combined_image_samplers, previous.combined_image_samplers * 2u) << "pool " << index;
        EXPECT_EQ(budget.storage_buffers, previous.storage_buffers * 2u) << "pool " << index;
        EXPECT_EQ(budget.storage_images, previous.storage_images * 2u) << "pool " << index;
        previous = budget;
    }
}

TEST(vk_descriptor_pool_budget, growth_is_capped)
{
    const vk::descriptor_pool_budget capped = vk::descriptor_pool_budget_for(4);
    for (const uint32_t index : {5u, 6u, 16u, 1000u})
    {
        const vk::descriptor_pool_budget budget = vk::descriptor_pool_budget_for(index);
        EXPECT_EQ(budget.max_sets, capped.max_sets) << "pool " << index;
        EXPECT_EQ(budget.uniform_buffers, capped.uniform_buffers) << "pool " << index;
        EXPECT_EQ(budget.combined_image_samplers, capped.combined_image_samplers) << "pool " << index;
        EXPECT_EQ(budget.storage_buffers, capped.storage_buffers) << "pool " << index;
        EXPECT_EQ(budget.storage_images, capped.storage_images) << "pool " << index;
    }
    // The cap is a bounded multiple of the base, so a runaway leak grows in
    // bounded steps rather than doubling without limit.
    EXPECT_EQ(capped.max_sets, vk::descriptor_pool_budget_for(0).max_sets * 16u);
}
