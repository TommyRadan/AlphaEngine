// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gl_resources.hpp
 * @brief Per-resource GL-side record types.
 *
 * Each @c handle_pool<T> in @ref gl_device stores values of one of these
 * structs. They live in their own header so individual resource impl
 * files (gl_device_buffer.cpp, gl_device_texture.cpp, ...) can consume
 * them without pulling in the full @ref gl_device declaration.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <glad/gl.h>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    struct gl_buffer
    {
        // Created, filled and updated through the named (DSA) entry
        // points, so a buffer never has to be bound to a target to be
        // written — in particular GL_ELEMENT_ARRAY_BUFFER, which is
        // vertex-array state, is only ever touched by the encoder.
        GLuint object_id{0};
        size_t size{0};
        buffer_usage usage{0};
    };

    struct gl_texture
    {
        GLuint object_id{0};
        // @c GL_TEXTURE_2D, @c GL_TEXTURE_2D_ARRAY, @c GL_TEXTURE_3D,
        // @c GL_TEXTURE_CUBE_MAP, or the multisample twins of the 2D
        // and array targets.
        GLenum target{0};
        texture_format format{texture_format::rgba8_unorm};
        uint32_t width{0};
        uint32_t height{0};
        // Slice count for 3D textures; 1 otherwise.
        uint32_t depth{1};
        // Layers the storage holds: six for a cube, the descriptor's
        // count for an array, 1 for a 2D / 3D texture.
        uint32_t array_layers{1};
        // Levels allocated by the immutable storage: the full chain
        // when the descriptor asked for mipmaps, the explicit count
        // when it gave one, otherwise 1.
        uint32_t mip_levels{1};
        // Samples per texel; above 1 the target is a multisample one
        // and the texture has no sampler state and no uploads.
        uint32_t samples{1};
        texture_usage usage{texture_usage_default};
        bool mipmaps{false};
        // Cube maps and arrays attach to a framebuffer one layer at a
        // time (glNamedFramebufferTextureLayer); the others whole.
        bool layered{false};
    };

    struct gl_sampler
    {
        // A real sampler object (glCreateSamplers). Bound to the texture
        // unit named by its binding number, where it overrides the
        // sampler state baked onto whichever texture sits on that unit.
        GLuint object_id{0};
        sampler_descriptor descriptor;
    };

    struct gl_shader_module
    {
        GLuint object_id{0};
        shader_stage stage{shader_stage::vertex};
        // FNV-1a digest of the SPIR-V the object was specialised from
        // (entry point "main", no specialisation constants): part of the
        // program-binary cache key of every program linked from it.
        uint64_t spirv_digest{0};
    };

    struct gl_bind_group_layout
    {
        bind_group_layout_descriptor descriptor;
    };

    // Shadow of one vertex-buffer binding point of a pipeline's VAO:
    // what glVertexArrayVertexBuffer was last given for that slot.
    struct gl_vertex_binding_shadow
    {
        GLuint buffer{0};
        GLintptr offset{0};
        GLsizei stride{0};
        bool known{false};
    };

    struct gl_pipeline
    {
        GLuint program_id{0};
        // The modules the program was linked from, in attach order, so
        // the debug hot reload can relink it when one of them changes.
        std::vector<shader_module> shader_modules;
        // The vertex array with the pipeline's vertex format baked in
        // (attribute formats, binding slots, divisors); the encoder only
        // attaches buffers to its binding points. 0 for compute
        // pipelines — compute dispatches don't need a vertex array.
        GLuint vao_id{0};

        // True for pipelines built via
        // @c gl_device::create_compute_pipeline. These are bound
        // through @c gl_compute_pass_encoder, never through
        // @c gl_render_pass_encoder.
        bool is_compute{false};

        primitive_topology topology{primitive_topology::triangles};
        // Per-patch vertex count for tessellation pipelines; 0 if
        // no tessellation stage is bound.
        uint32_t patch_control_points{0};

        blend_state blend;
        // Per colour attachment overrides of @c blend (see
        // pipeline_descriptor::attachment_blend).
        std::vector<blend_state> attachment_blend;
        depth_state depth;
        stencil_state stencil;
        depth_bias_state depth_bias;
        rasterizer_state rasterizer;
        uint32_t sample_count{1};

        std::vector<vertex_buffer_layout> vertex_buffers;
        std::vector<bind_group_layout> bind_group_layouts;

        // Per slot, the narrowest record the layout can be bound over
        // (the furthest attribute's end). Used as the binding stride
        // when neither the layout nor the draw supplies one: a DSA
        // binding stride of 0 means every vertex reads the same record,
        // not "tightly packed" as it did for glVertexAttribPointer.
        std::vector<uint32_t> min_strides;

        // Binding shadows for @c set_vertex_buffer / @c set_index_buffer,
        // valid while @c shadow_epoch matches the device's state-cache
        // epoch (see @c gl_device::invalidate_state_cache).
        std::vector<gl_vertex_binding_shadow> vertex_binding_shadows;
        GLuint element_buffer_shadow{0};
        bool element_buffer_known{false};
        uint64_t shadow_epoch{0};

        // Set once a push_constants call against this pipeline has been
        // reported (OpenGL has none), so it logs once per pipeline.
        bool push_constants_reported{false};
    };

    struct gl_bind_group
    {
        bind_group_layout layout{};
        std::vector<binding_value> entries;

        // Per entry, which of the bind-time dynamic offsets applies to
        // it (its rank, by binding number, among the layout's dynamic
        // uniform-buffer slots), or @ref no_dynamic_offset. Resolved at
        // creation so a bind reads no layout; @ref dynamic_count is how
        // many offsets every @c set_bind_group of the group must pass.
        static constexpr uint32_t no_dynamic_offset = UINT32_MAX;
        std::vector<uint32_t> dynamic_index;
        uint32_t dynamic_count{0};

        // Set once a bind with the wrong number of dynamic offsets has
        // been reported, so a broken call site logs once, not per draw.
        bool offset_mismatch_reported{false};
    };

    // One attachment of an off-screen target: the texture it renders
    // into (allocated by the device and released with the target when
    // @c owned, otherwise imported and left to its owner) and the
    // level / layer it is attached at.
    struct gl_attachment
    {
        texture tex{};
        bool owned{false};
        uint32_t mip_level{0};
        uint32_t layer{0};
    };

    struct gl_render_target
    {
        GLuint framebuffer_id{0}; // 0 == default swapchain
        uint32_t width{0};
        uint32_t height{0};
        uint32_t samples{1};
        bool has_depth{true};
        // True when the depth attachment is a packed depth-stencil
        // format (or the window backbuffer carries stencil bits), so a
        // depth clear / invalidate covers the stencil plane too.
        bool has_stencil{false};

        // Colour attachments in draw-buffer order (GL_COLOR_ATTACHMENT0
        // + i), empty for the swapchain (FBO 0, whose one colour plane
        // has no texture) and for a depth-only target. The textures are
        // exposed to callers via @ref device::render_target_color_texture
        // so the next pass can sample them as input.
        std::vector<gl_attachment> color;
        gl_attachment depth;
    };

    // Timestamp query objects, one per slot of the set.
    struct gl_query_set
    {
        std::vector<GLuint> ids;
    };
} // namespace rendering_engine::gpu::backend::opengl
