// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_target.hpp
 * @brief Render-target descriptors and per-attachment load/store ops.
 *
 * A render target is a fixed set of attachments: zero or more colour
 * attachments (up to @ref max_color_attachments, for multiple render
 * targets) and an optional depth / depth-stencil attachment. Each
 * attachment is either allocated by the device for the target — the
 * common case, released with it — or an existing texture the target
 * attaches at one mip level and one layer / cube face (see
 * @ref attachment_desc::texture), which is how six face targets share
 * one cube depth map, or a pass renders into a mip of a chain it does
 * not own. @c device::swapchain_target() is the window's backbuffer:
 * one colour attachment plus the depth plane the window system granted.
 *
 * A @ref render_pass_descriptor names the target and the load / store
 * behaviour of each attachment for one pass; the encoder sets the
 * viewport to the target's full extent when the pass begins.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    // The most colour attachments one target may carry.
    // @c device_limits::max_color_attachments reports how many the
    // device takes (Vulkan guarantees 4, desktop hardware offers 8) and
    // the backend validates against it at create time.
    constexpr uint32_t max_color_attachments = 8;

    // One attachment of a render target.
    struct attachment_desc
    {
        // Texel format of the attachment. For an allocated attachment
        // this is what the device creates; for an imported texture it
        // is ignored (the texture's own format applies).
        texture_format format{texture_format::rgba8_unorm};

        // An existing texture to attach instead of allocating one. Not
        // owned by the target: it must outlive it and is released by
        // its own owner. Must have been created with
        // @c texture_usage_render_attachment and match the target's
        // extent at @ref mip_level.
        gpu::texture texture{};

        // The mip level rendered into. Must be 0 for an allocated
        // attachment (the device allocates single-mip attachments);
        // an imported texture may name any of its levels.
        uint32_t mip_level{0};

        // The array layer or cube face rendered into. For an allocated
        // attachment it indexes the layers the target's @c dimension
        // and @c array_layers give it (a cube face is 0..5 in the
        // +X, -X, +Y, -Y, +Z, -Z order of @ref cube_face); for an
        // imported texture it indexes that texture's layers. 0 for a
        // plain 2D attachment.
        uint32_t layer{0};
    };

    struct render_target_descriptor
    {
        // Colour attachments, index-matched to the fragment shader's
        // @c layout(location = N) outputs and to
        // @c render_pass_descriptor::color. Empty for a depth-only
        // target (a shadow map).
        std::vector<attachment_desc> color;

        // Extent of every attachment at the level it is attached at.
        // For the swapchain these come from the window; for off-screen
        // targets the caller fills them in.
        uint32_t width{0};
        uint32_t height{0};

        // Shape of the attachments the target allocates: @c d2 for the
        // usual single image, @c cube or @c d2_array for a layered
        // attachment of which one layer (@c attachment_desc::layer) is
        // rendered per pass; the other layers are reached by importing
        // the same texture into further targets. Ignored for imported
        // attachments, which bring their own shape.
        texture_dimension dimension{texture_dimension::d2};

        // Layer count of an allocated @c d2_array attachment. Ignored
        // otherwise (a cube has six, a 2D image one).
        uint32_t array_layers{1};

        // Samples per pixel of every attachment (1 = single-sampled).
        // A pipeline drawn into the target must declare the same
        // @c pipeline_descriptor::sample_count. A multisampled attachment
        // is not resolved to a single-sampled texture.
        uint32_t sample_count{1};

        // Whether the target has a depth attachment, described by
        // @ref depth. The format defaults to a 24-bit depth buffer to
        // match the existing swapchain configuration.
        bool with_depth{true};
        attachment_desc depth{texture_format::depth24};

        // The usual off-screen colour target: one colour attachment of
        // @p color_format, optionally with a depth attachment of
        // @p depth_format.
        static render_target_descriptor single_color(texture_format color_format,
                                                     uint32_t width,
                                                     uint32_t height,
                                                     bool depth = false,
                                                     texture_format depth_format = texture_format::depth24)
        {
            render_target_descriptor descriptor{};
            descriptor.color.push_back({color_format});
            descriptor.width = width;
            descriptor.height = height;
            descriptor.with_depth = depth;
            descriptor.depth.format = depth_format;
            return descriptor;
        }

        // A depth-only target (a shadow map): no colour attachment, a
        // sampled depth attachment of @p depth_format.
        static render_target_descriptor depth_only(texture_format depth_format, uint32_t width, uint32_t height)
        {
            render_target_descriptor descriptor{};
            descriptor.width = width;
            descriptor.height = height;
            descriptor.with_depth = true;
            descriptor.depth.format = depth_format;
            return descriptor;
        }
    };

    // The layers an attachment allocated by @p descriptor has, so an
    // @c attachment_desc::layer can be range-checked before the device
    // sees it.
    constexpr uint32_t render_target_layer_count(const render_target_descriptor& descriptor)
    {
        switch (descriptor.dimension)
        {
        case texture_dimension::cube:
            return 6;
        case texture_dimension::d2_array:
            return descriptor.array_layers == 0 ? 1u : descriptor.array_layers;
        case texture_dimension::d2:
        case texture_dimension::d3:
            break;
        }
        return 1;
    }

    // Device-free validation of a target descriptor, run by both
    // backends before they allocate anything and usable by callers up
    // front. Returns @c nullptr when the descriptor is well formed,
    // otherwise a static string naming the first problem found. What
    // it checks: a non-zero extent; at least one attachment; at most
    // @ref max_color_attachments colour attachments; colour formats that
    // are not depth formats and a depth format that is; a power-of-two
    // sample count; a renderable shape (3D attachments are not
    // supported); and, for allocated attachments, a base mip level and
    // a layer within the shape's layer count. What an imported texture
    // allows (its own levels, layers and usage) is the backend's check.
    inline const char* validate_render_target_descriptor(const render_target_descriptor& descriptor)
    {
        if (descriptor.width == 0 || descriptor.height == 0)
        {
            return "render target extent must be non-zero";
        }
        if (descriptor.color.empty() && !descriptor.with_depth)
        {
            return "render target needs at least one attachment";
        }
        if (descriptor.color.size() > max_color_attachments)
        {
            return "render target has more colour attachments than max_color_attachments";
        }
        if (descriptor.dimension == texture_dimension::d3)
        {
            return "render target attachments cannot be 3D textures";
        }
        if (!sample_count_supported(~0u, descriptor.sample_count))
        {
            return "render target sample count must be a power of two";
        }
        const uint32_t layers = render_target_layer_count(descriptor);
        for (const attachment_desc& attachment : descriptor.color)
        {
            if (attachment.texture.valid())
            {
                continue;
            }
            if (is_depth_texture_format(attachment.format))
            {
                return "colour attachment has a depth format";
            }
            if (attachment.mip_level != 0)
            {
                return "an allocated attachment is single-mip; import a texture to render into a mip";
            }
            if (attachment.layer >= layers)
            {
                return "attachment layer is outside the target's layer count";
            }
        }
        if (descriptor.with_depth && !descriptor.depth.texture.valid())
        {
            if (!is_depth_texture_format(descriptor.depth.format))
            {
                return "depth attachment has a colour format";
            }
            if (descriptor.depth.mip_level != 0)
            {
                return "an allocated attachment is single-mip; import a texture to render into a mip";
            }
            if (descriptor.depth.layer >= layers)
            {
                return "attachment layer is outside the target's layer count";
            }
        }
        return nullptr;
    }

    struct color_attachment_op
    {
        load_op load{load_op::clear};
        store_op store{store_op::store};
        std::array<float, 4> clear_color{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct depth_attachment_op
    {
        load_op load{load_op::clear};
        store_op store{store_op::store};
        float clear_depth{1.0f};
        // The stencil plane, when the attachment has one, is cleared
        // and stored together with depth; this is its clear value.
        uint32_t clear_stencil{0};
    };

    struct render_pass_descriptor
    {
        render_target target{};

        // Per colour attachment load / store / clear, index-matched to
        // the target's colour attachments; entries past the target's
        // attachment count are ignored. A pass over the usual
        // single-colour target fills @c color[0]. Defaults clear to
        // opaque black and store.
        std::array<color_attachment_op, max_color_attachments> color{};
        depth_attachment_op depth;

        // When false the pass skips depth state changes regardless
        // of the target's depth attachment (e.g. UI overlay pass).
        bool use_depth{true};

        // The pass's draws come from secondary encoders recorded in
        // parallel (@c render_pass_encoder::begin_secondary /
        // @c execute_secondary) rather than from calls on the encoder
        // @c begin_render_pass returns, which then only opens and closes
        // the pass and splices the secondaries in; a draw recorded on it
        // directly is reported and dropped.
        bool parallel{false};
    };
} // namespace rendering_engine::gpu
