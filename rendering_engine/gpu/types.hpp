// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file types.hpp
 * @brief Backend-agnostic enums for the @ref rendering_engine::gpu device.
 *
 * Values are abstract names — backends translate them to their own native
 * constants (@c gpu/backend/vulkan/vk_translate.hpp). No header in this
 * directory ever names a backend-specific type.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace rendering_engine::gpu
{
    // Vertex attribute element types. The @c float32 and @c uint32 variants
    // cover everything the engine currently uploads.
    enum class scalar_type
    {
        float32,
        int32,
        uint32,
        int16,
        uint16,
        int8,
        uint8,
    };

    // Texel formats. Only the formats the engine actually creates today
    // are listed; broadening this is a one-line change in the backend
    // translation table.
    //
    // @c rgba8_srgb stores sRGB-encoded 8-bit texels: the sampler decodes
    // them to linear before filtering and mip generation blends the
    // decoded values, so a colour image authored in sRGB (albedo, base
    // colour, emissive, sprites) enters shading linear. @c rgba8_unorm is
    // for data that is already linear (normals, metalness, roughness,
    // AO) or for images that must round-trip unchanged.
    //
    // The block-compressed formats store 4x4 texel blocks (8 bytes for
    // BC1 / BC4, 16 for the rest) and are sampled only: they cannot be
    // attached, bound as storage images or given a generated mip chain,
    // so every level is uploaded (see @ref is_compressed_texture_format
    // and @ref texture_image_bytes). None is guaranteed: BCn is a desktop
    // family and ASTC a mobile one, so a loader asks
    // @c device::format_support before it picks one (the KTX2 loader in
    // the asset cache transcodes to the best sampleable one). The colour
    // families keep the unorm / srgb pairing; BC4 (one channel) and BC5
    // (two) are data formats with no sRGB form.
    enum class texture_format
    {
        rgba8_unorm,
        rgba8_srgb,
        rgb8_unorm,
        r8_unorm,
        rgba16_float,
        rgba32_float,
        depth24,
        depth32_float,
        depth24_stencil8,
        bc1_rgba_unorm,
        bc1_rgba_srgb,
        bc3_rgba_unorm,
        bc3_rgba_srgb,
        bc4_r_unorm,
        bc5_rg_unorm,
        bc7_rgba_unorm,
        bc7_rgba_srgb,
        astc_4x4_unorm,
        astc_4x4_srgb,
    };

    // Index buffer element width.
    enum class index_format
    {
        uint16,
        uint32,
    };

    // Draw-call primitive topology. @c patches feeds the
    // tessellation control stage when the pipeline binds tess
    // shaders; the per-patch vertex count comes from
    // @c pipeline_descriptor::patch_control_points.
    enum class primitive_topology
    {
        triangles,
        lines,
        points,
        patches,
    };

    enum class blend_factor
    {
        zero,
        one,
        src_color,
        one_minus_src_color,
        dst_color,
        one_minus_dst_color,
        src_alpha,
        one_minus_src_alpha,
        dst_alpha,
        one_minus_dst_alpha,
    };

    enum class blend_op
    {
        add,
        subtract,
        reverse_subtract,
        min,
        max,
    };

    enum class compare_function
    {
        never,
        less,
        equal,
        less_equal,
        greater,
        not_equal,
        greater_equal,
        always,
    };

    enum class cull_mode
    {
        none,
        front,
        back,
    };

    // Triangle winding for the front face. @c counter_clockwise matches
    // the engine's existing meshes; @c clockwise is provided for
    // imported assets that bake the opposite convention.
    enum class front_face
    {
        counter_clockwise,
        clockwise,
    };

    // Polygon rasterization mode. @c fill is the default; @c line
    // produces wireframe debug views.
    enum class polygon_mode
    {
        fill,
        line,
        point,
    };

    enum class address_mode
    {
        clamp_edge,
        clamp_border,
        repeat,
        mirrored_repeat,
    };

    enum class filter_mode
    {
        nearest,
        linear,
    };

    enum class mipmap_mode
    {
        none,
        nearest,
        linear,
    };

    enum class shader_stage
    {
        vertex,
        fragment,
        geometry,
        tessellation_control,
        tessellation_evaluation,
        compute,
    };

    // Bitmask of shader stages, used by @c bind_group_layout_entry to
    // declare which stages read a binding. The backend bakes it into
    // the descriptor-set layout. Combine with @c |.
    using shader_stages = uint32_t;
    constexpr shader_stages shader_stages_vertex = 1u << 0;
    constexpr shader_stages shader_stages_fragment = 1u << 1;
    constexpr shader_stages shader_stages_geometry = 1u << 2;
    constexpr shader_stages shader_stages_tessellation_control = 1u << 3;
    constexpr shader_stages shader_stages_tessellation_evaluation = 1u << 4;
    constexpr shader_stages shader_stages_compute = 1u << 5;
    // What a rasterisation pipeline's bindings default to: the vertex
    // and fragment stages every built-in material and pass uses. A
    // layout read by a geometry or tessellation stage widens it.
    constexpr shader_stages shader_stages_default = shader_stages_vertex | shader_stages_fragment;
    constexpr shader_stages shader_stages_all_graphics = shader_stages_vertex | shader_stages_fragment |
                                                         shader_stages_geometry | shader_stages_tessellation_control |
                                                         shader_stages_tessellation_evaluation;

    // Texture shape. @c d2_array is a stack of @c array_layers 2D
    // images sampled through @c sampler2DArray; @c cube is six faces
    // (fixed at six layers). Both are layered, so a render target
    // attaches one layer / face at a time (see @c attachment_desc).
    enum class texture_dimension
    {
        d2,
        d3,
        cube,
        d2_array,
    };

    // Bitmask of what a texture may be used for. The backend bakes it
    // into the image's usage flags, so a texture bound as a
    // render-target attachment, a storage image or a copy source must
    // have asked for it up front. Combine with @c |. @c texture_usage_default is what an uploaded,
    // sampled asset needs: sampling plus the copies that fill it and
    // derive its mip chain.
    using texture_usage = uint32_t;
    constexpr texture_usage texture_usage_sampled = 1u << 0;
    constexpr texture_usage texture_usage_render_attachment = 1u << 1;
    constexpr texture_usage texture_usage_storage = 1u << 2;
    constexpr texture_usage texture_usage_copy_src = 1u << 3;
    constexpr texture_usage texture_usage_copy_dst = 1u << 4;
    constexpr texture_usage texture_usage_default =
        texture_usage_sampled | texture_usage_copy_src | texture_usage_copy_dst;

    // Native size of one texel of @p format in bytes: the block the
    // copy commands (@c copy_texture_to_buffer, @c copy_buffer_to_texture)
    // and @c device::read_texture move, and what a tightly packed buffer
    // region of the format measures. It is the storage layout, so
    // @c rgba16_float is eight bytes (four halves) and the packed depth
    // formats are four; the upload paths (@c write_texture and friends)
    // document their own client layouts per backend. For a
    // block-compressed format it is the size of one 4x4 block (see
    // @ref texture_block_extent).
    constexpr uint32_t texel_size_bytes(texture_format format)
    {
        switch (format)
        {
        case texture_format::bc1_rgba_unorm:
        case texture_format::bc1_rgba_srgb:
        case texture_format::bc4_r_unorm:
            return 8;
        case texture_format::bc3_rgba_unorm:
        case texture_format::bc3_rgba_srgb:
        case texture_format::bc5_rg_unorm:
        case texture_format::bc7_rgba_unorm:
        case texture_format::bc7_rgba_srgb:
        case texture_format::astc_4x4_unorm:
        case texture_format::astc_4x4_srgb:
            return 16;
        case texture_format::rgba8_unorm:
        case texture_format::rgba8_srgb:
            return 4;
        case texture_format::rgb8_unorm:
            return 3;
        case texture_format::r8_unorm:
            return 1;
        case texture_format::rgba16_float:
            return 8;
        case texture_format::rgba32_float:
            return 16;
        case texture_format::depth24:
        case texture_format::depth32_float:
        case texture_format::depth24_stencil8:
            return 4;
        }
        return 4;
    }

    // True for the block-compressed formats (BCn, ASTC): sampled only,
    // uploaded a whole block row at a time, every level supplied by the
    // caller.
    constexpr bool is_compressed_texture_format(texture_format format)
    {
        switch (format)
        {
        case texture_format::bc1_rgba_unorm:
        case texture_format::bc1_rgba_srgb:
        case texture_format::bc3_rgba_unorm:
        case texture_format::bc3_rgba_srgb:
        case texture_format::bc4_r_unorm:
        case texture_format::bc5_rg_unorm:
        case texture_format::bc7_rgba_unorm:
        case texture_format::bc7_rgba_srgb:
        case texture_format::astc_4x4_unorm:
        case texture_format::astc_4x4_srgb:
            return true;
        default:
            return false;
        }
    }

    // Width and height, in texels, of the unit @ref texel_size_bytes
    // measures: 4 for the block-compressed formats, 1 otherwise.
    constexpr uint32_t texture_block_extent(texture_format format)
    {
        return is_compressed_texture_format(format) ? 4u : 1u;
    }

    // Bytes of a tightly packed @p width x @p height x @p depth image of
    // @p format: whole blocks for a compressed format (a level smaller
    // than a block still occupies one), texels otherwise. The storage
    // layout, like @ref texel_size_bytes.
    constexpr size_t texture_image_bytes(texture_format format, uint32_t width, uint32_t height, uint32_t depth = 1)
    {
        const uint32_t block = texture_block_extent(format);
        const size_t blocks_x = (static_cast<size_t>(width) + block - 1) / block;
        const size_t blocks_y = (static_cast<size_t>(height) + block - 1) / block;
        return blocks_x * blocks_y * depth * texel_size_bytes(format);
    }

    // True for the depth and depth-stencil formats: what a target's
    // depth attachment may be, never a colour attachment.
    constexpr bool is_depth_texture_format(texture_format format)
    {
        return format == texture_format::depth24 || format == texture_format::depth32_float ||
               format == texture_format::depth24_stencil8;
    }

    // True for the formats that carry a stencil plane.
    constexpr bool has_stencil_plane(texture_format format)
    {
        return format == texture_format::depth24_stencil8;
    }

    // Stencil-buffer update applied by a stencil test outcome
    // (@c stencil_face_state). Names follow Vulkan's @c VkStencilOp.
    enum class stencil_op
    {
        keep,
        zero,
        replace,
        increment_clamp,
        decrement_clamp,
        invert,
        increment_wrap,
        decrement_wrap,
    };

    // Bitmask of the colour channels a pipeline writes into a colour
    // attachment (@c blend_state::write_mask). Combine with @c |.
    using color_write_mask = uint32_t;
    constexpr color_write_mask color_write_red = 1u << 0;
    constexpr color_write_mask color_write_green = 1u << 1;
    constexpr color_write_mask color_write_blue = 1u << 2;
    constexpr color_write_mask color_write_alpha = 1u << 3;
    constexpr color_write_mask color_write_all =
        color_write_red | color_write_green | color_write_blue | color_write_alpha;

    // The colour returned by a lookup that lands outside a texture
    // addressed with @c address_mode::clamp_border. Vulkan only
    // guarantees these three (a custom border needs an extension), so
    // the abstraction stops there.
    enum class border_color
    {
        transparent_black,
        opaque_black,
        opaque_white,
    };

    // Bitmask of multisample counts, one bit per count: bit @c n set
    // means @c n samples per pixel are supported (1, 2, 4, 8, 16, ...).
    // Mirrors @c VkSampleCountFlags; @c device_limits reports one per
    // attachment class.
    using sample_count_mask = uint32_t;

    // True when @p mask allows @p count samples per pixel. A count that
    // is not a power of two is never supported.
    constexpr bool sample_count_supported(sample_count_mask mask, uint32_t count)
    {
        if (count == 0 || (count & (count - 1)) != 0)
        {
            return false;
        }
        return (mask & count) != 0;
    }

    // Shader-side access mode for a storage image binding. Maps to the
    // SPIR-V image @c NonReadable / @c NonWritable decorations.
    enum class storage_access
    {
        read_only,
        write_only,
        read_write,
    };

    // Cube-map face index, in the layer order Vulkan gives a cube
    // image's faces; the backend translates to native values.
    enum class cube_face
    {
        positive_x,
        negative_x,
        positive_y,
        negative_y,
        positive_z,
        negative_z,
    };

    // Render-pass attachment load behaviour.
    enum class load_op
    {
        load,
        clear,
        dont_care,
    };

    // Render-pass attachment store behaviour.
    enum class store_op
    {
        store,
        dont_care,
    };

    // Buffer usage hint passed at create time: where a buffer lives and
    // how the host may write it.
    //
    //   static_data   Filled once (initial data or the odd write_buffer)
    //                 and read by the GPU: device-local memory, written
    //                 through a staging copy ordered ahead of the frame.
    //   dynamic_data  Rewritten from the host, wholly or in part, at any
    //                 rate: host-visible and persistently mapped, so a
    //                 write is a memcpy. A backend with several frames in
    //                 flight keeps one copy per frame slot and carries
    //                 every write across the copies, so the caller writes
    //                 it like a single buffer and the GPU never reads a
    //                 copy the host is writing.
    //   stream_data   Host-visible and mapped like dynamic_data, but a
    //                 single copy: the caller partitions it per frame in
    //                 flight itself (one region per
    //                 device::frames_in_flight) or otherwise never writes
    //                 what a frame in flight may read.
    enum class buffer_usage_hint
    {
        static_data,
        dynamic_data,
        stream_data,
    };

    // Bitmask of allowed bind points for a buffer. A buffer can be
    // valid for multiple bind points simultaneously (e.g. a vertex
    // buffer that is also a copy destination). Combine with @c |.
    using buffer_usage = uint32_t;
    constexpr buffer_usage buffer_usage_vertex = 1u << 0;
    constexpr buffer_usage buffer_usage_index = 1u << 1;
    constexpr buffer_usage buffer_usage_uniform = 1u << 2;
    constexpr buffer_usage buffer_usage_copy_dst = 1u << 3;
    constexpr buffer_usage buffer_usage_storage = 1u << 4;
    constexpr buffer_usage buffer_usage_indirect = 1u << 5;
    constexpr buffer_usage buffer_usage_copy_src = 1u << 6;

    // Pipeline stages used by @c command_encoder::barrier. A barrier
    // orders prior @c src_stage work against subsequent @c dst_stage
    // work; the matching @c access_flag mask selects which memory
    // accesses are synchronised. Combine with @c |.
    using pipeline_stage = uint32_t;
    constexpr pipeline_stage pipeline_stage_none = 0u;
    constexpr pipeline_stage pipeline_stage_vertex_input = 1u << 0;
    constexpr pipeline_stage pipeline_stage_vertex_shader = 1u << 1;
    constexpr pipeline_stage pipeline_stage_tessellation_control_shader = 1u << 2;
    constexpr pipeline_stage pipeline_stage_tessellation_evaluation_shader = 1u << 3;
    constexpr pipeline_stage pipeline_stage_geometry_shader = 1u << 4;
    constexpr pipeline_stage pipeline_stage_fragment_shader = 1u << 5;
    constexpr pipeline_stage pipeline_stage_compute_shader = 1u << 6;
    constexpr pipeline_stage pipeline_stage_color_attachment_output = 1u << 7;
    constexpr pipeline_stage pipeline_stage_early_fragment_tests = 1u << 8;
    constexpr pipeline_stage pipeline_stage_late_fragment_tests = 1u << 9;
    constexpr pipeline_stage pipeline_stage_transfer = 1u << 10;
    constexpr pipeline_stage pipeline_stage_draw_indirect = 1u << 11;
    constexpr pipeline_stage pipeline_stage_all_graphics = 0x0FFFu;
    constexpr pipeline_stage pipeline_stage_all_commands = 0xFFFFu;

    // Memory-access categories used by @c command_encoder::barrier.
    // The Vulkan backend uses both @c src_access and @c dst_access
    // verbatim. Combine with @c |.
    using access_flag = uint32_t;
    constexpr access_flag access_none = 0u;
    constexpr access_flag access_indirect_command_read = 1u << 0;
    constexpr access_flag access_index_read = 1u << 1;
    constexpr access_flag access_vertex_attribute_read = 1u << 2;
    constexpr access_flag access_uniform_read = 1u << 3;
    constexpr access_flag access_shader_read = 1u << 4;
    constexpr access_flag access_shader_write = 1u << 5;
    constexpr access_flag access_color_attachment_read = 1u << 6;
    constexpr access_flag access_color_attachment_write = 1u << 7;
    constexpr access_flag access_depth_stencil_attachment_read = 1u << 8;
    constexpr access_flag access_depth_stencil_attachment_write = 1u << 9;
    constexpr access_flag access_transfer_read = 1u << 10;
    constexpr access_flag access_transfer_write = 1u << 11;
    constexpr access_flag access_storage_buffer_read = 1u << 12;
    constexpr access_flag access_storage_buffer_write = 1u << 13;
    constexpr access_flag access_storage_image_read = 1u << 14;
    constexpr access_flag access_storage_image_write = 1u << 15;
} // namespace rendering_engine::gpu
