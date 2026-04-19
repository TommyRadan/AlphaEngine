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
 * @file rhi.hpp
 * @brief Render Hardware Interface — abstract GPU backend the rendering engine talks to.
 *
 * This header has no knowledge of OpenGL, Vulkan, or any other concrete graphics API.
 * Concrete backends (e.g. @c rhi::opengl_rhi, @c rhi::null_rhi) implement @c rhi::device
 * and produce opaque resource handles that callers pass back through the same device.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include <rhi/types.hpp>

namespace rhi
{
    /**
     * @brief Opaque GPU buffer resource (vertex or index buffer).
     *
     * Created through @ref device::create_buffer and released with
     * @ref device::destroy_buffer. Ownership is held by the backend;
     * callers treat the returned pointer as an opaque token.
     */
    struct buffer
    {
        virtual ~buffer() = default;
    };

    /** @brief Opaque GPU texture resource. */
    struct texture
    {
        virtual ~texture() = default;
    };

    /** @brief Opaque compiled shader stage (vertex/fragment/geometry). */
    struct shader
    {
        virtual ~shader() = default;
    };

    /** @brief Opaque linked shader program. */
    struct program
    {
        virtual ~program() = default;
    };

    /** @brief Opaque vertex input layout (a.k.a. vertex array object). */
    struct vertex_array
    {
        virtual ~vertex_array() = default;
    };

    /** @brief Opaque off-screen render target. */
    struct framebuffer
    {
        virtual ~framebuffer() = default;
    };

    /** @brief Descriptor passed to @ref device::create_buffer. */
    struct buffer_desc
    {
        /** Optional initial data. May be @c nullptr to create an empty buffer. */
        const void* initial_data = nullptr;
        /** Size of @ref initial_data in bytes. */
        std::size_t size = 0;
        /** Usage hint for the driver (static, dynamic, stream). */
        buffer_usage usage = buffer_usage::static_draw;
        /** If @c true the buffer is created as an index/element buffer. */
        bool is_index_buffer = false;
    };

    /** @brief Descriptor passed to @ref device::create_texture. */
    struct texture_desc
    {
        /** Optional initial pixel data. May be @c nullptr to allocate storage only. */
        const void* initial_pixels = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        pixel_format source_format = pixel_format::rgba;
        element_type source_type = element_type::unsigned_byte_type;
        internal_format storage_format = internal_format::rgba;
    };

    /** @brief Descriptor passed to @ref device::create_framebuffer. */
    struct framebuffer_desc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        /** Color attachment size in bits (24 = RGB, 32 = RGBA). */
        uint8_t color_bits = 32;
        /** Depth attachment size in bits (0 disables depth). */
        uint8_t depth_bits = 24;
    };

    /** @brief Describes one attribute inside a vertex layout. */
    struct vertex_attribute_desc
    {
        uint32_t location = 0;
        const buffer* source = nullptr;
        element_type type = element_type::float_type;
        uint32_t component_count = 0;
        uint32_t stride = 0;
        std::size_t offset = 0;
    };

    /** @brief Arguments of a single @ref device::draw call. */
    struct draw_call
    {
        const vertex_array* vao = nullptr;
        primitive_type topology = primitive_type::triangles;

        /** If @c true, issues an indexed draw; @ref vao must have an element buffer bound. */
        bool indexed = false;

        /** Starting vertex for non-indexed draws, or byte offset into the index buffer for indexed draws. */
        std::size_t offset = 0;

        /** Number of vertices (non-indexed) or number of indices (indexed). */
        std::size_t count = 0;

        /** Index element type for indexed draws. Ignored for non-indexed draws. */
        element_type index_type = element_type::unsigned_int_type;
    };

    /**
     * @brief Abstract rendering device — the entry point every renderer goes through.
     *
     * A concrete backend subclass (e.g. @c opengl_rhi, @c null_rhi) is constructed
     * once by @c rendering_engine::context and handed to every renderer via
     * @ref set_device / @ref get_device.
     *
     * All member functions must be called on the thread that owns the graphics
     * context (the main thread for the GL backend).
     */
    class device
    {
    public:
        virtual ~device() = default;

        /** @brief Initializes the backend (load entry points, query caps, …). */
        virtual void init() = 0;

        /** @brief Shuts the backend down. Must be called before destruction. */
        virtual void quit() = 0;

        // Resource creation
        virtual buffer* create_buffer(const buffer_desc& desc) = 0;
        virtual void destroy_buffer(buffer* b) = 0;
        virtual void buffer_sub_data(buffer* b, std::size_t offset, std::size_t length, const void* data) = 0;

        virtual texture* create_texture(const texture_desc& desc) = 0;
        virtual void destroy_texture(texture* t) = 0;
        virtual void texture_set_wrap(texture* t, wrap_mode s, wrap_mode tt, wrap_mode r) = 0;
        virtual void texture_set_filters(texture* t, filter_mode min_filter, filter_mode mag_filter) = 0;
        virtual void texture_generate_mipmaps(texture* t) = 0;

        virtual shader* create_shader(shader_stage stage, const std::string& source) = 0;
        virtual void destroy_shader(shader* s) = 0;

        virtual program* create_program() = 0;
        virtual void destroy_program(program* p) = 0;
        virtual void program_attach(program* p, const shader* s) = 0;
        virtual void program_link(program* p) = 0;
        virtual void program_start(program* p) = 0;
        virtual void program_stop(program* p) = 0;

        /** @brief Returns the integer uniform location, or -1 if not found. */
        virtual int32_t program_get_uniform_location(program* p, const std::string& name) = 0;
        virtual void program_set_uniform(program* p, int32_t location, int32_t value) = 0;
        virtual void program_set_uniform(program* p, int32_t location, float value) = 0;
        virtual void program_set_uniform(program* p, int32_t location, const glm::vec2& value) = 0;
        virtual void program_set_uniform(program* p, int32_t location, const glm::vec3& value) = 0;
        virtual void program_set_uniform(program* p, int32_t location, const glm::vec4& value) = 0;
        virtual void program_set_uniform(program* p, int32_t location, const glm::mat3& value) = 0;
        virtual void program_set_uniform(program* p, int32_t location, const glm::mat4& value) = 0;

        virtual vertex_array* create_vertex_array() = 0;
        virtual void destroy_vertex_array(vertex_array* v) = 0;
        virtual void vertex_array_bind_attribute(vertex_array* v, const vertex_attribute_desc& desc) = 0;
        virtual void vertex_array_bind_elements(vertex_array* v, const buffer* elements) = 0;

        virtual framebuffer* create_framebuffer(const framebuffer_desc& desc) = 0;
        virtual void destroy_framebuffer(framebuffer* f) = 0;
        virtual const texture* framebuffer_color_texture(const framebuffer* f) = 0;
        virtual const texture* framebuffer_depth_texture(const framebuffer* f) = 0;

        // State
        virtual void enable(capability cap) = 0;
        virtual void disable(capability cap) = 0;
        virtual void set_clear_color(float r, float g, float b, float a) = 0;
        virtual void clear(clear_buffer buffers) = 0;
        virtual void set_depth_mask(bool write_enabled) = 0;
        virtual void set_blend_func_alpha() = 0;

        virtual void bind_texture(const texture* t, uint8_t unit) = 0;
        virtual void bind_framebuffer(const framebuffer* f) = 0;
        virtual void bind_default_framebuffer() = 0;

        // Draw
        virtual void draw(const draw_call& call) = 0;
    };

    /**
     * @brief Installs the active RHI device for the process. Must be called
     *        once during startup before any renderer begins using the device.
     *
     * Does not take ownership — the caller must keep the device alive at
     * least until @ref set_device(nullptr) or process shutdown.
     */
    void set_device(device* d);

    /**
     * @brief Returns the active RHI device, or @c nullptr if none is installed.
     */
    device* get_device();
} // namespace rhi
