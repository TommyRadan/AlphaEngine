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
 * @file bind_group.hpp
 * @brief Typed binding tables — the per-draw resource binding model.
 *
 * Replaces the old string-keyed @c set_uniform("modelMatrix", ...) path.
 * A @ref bind_group_layout declares the shape of a binding table at
 * pipeline-create time; concrete @ref bind_group values are constructed
 * once and bound at draw time without name lookups.
 *
 * The supported value kinds are intentionally narrow: typed scalars and
 * vectors / matrices for legacy non-UBO uniforms, plus textures and
 * samplers. This is enough to host every existing renderer/renderable
 * draw without re-authoring shaders to use UBO blocks. A future pass can
 * fold the scalar/vector/matrix entries into a single @c uniform_buffer
 * binding kind once shaders are migrated.
 */

#pragma once

#include <string>
#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        // What a single binding entry stores. Value kinds map to
        // single-uniform writes on the GL backend; @c texture and
        // @c sampler resolve to a texture-unit assignment.
        enum class binding_kind
        {
            float_value,
            int_value,
            vec2_value,
            vec3_value,
            vec4_value,
            mat3_value,
            mat4_value,
            texture,
            sampler,
        };

        // One slot in a layout. @ref name is the shader-side identifier
        // the backend uses to resolve the slot at pipeline-create time
        // (e.g. @c glGetUniformLocation for the GL backend). Once the
        // pipeline is built, slot resolution is cached and string keys
        // are not consulted again.
        struct bind_group_layout_entry
        {
            uint32_t binding{0};
            binding_kind kind{binding_kind::float_value};
            std::string name;
        };

        struct bind_group_layout_descriptor
        {
            std::vector<bind_group_layout_entry> entries;
        };

        // A concrete value for one binding slot. Only the field matching
        // @ref kind is read. The non-active fields are present to keep
        // the type a plain aggregate so call sites can use designated
        // initializers without juggling a tagged union.
        struct binding_value
        {
            uint32_t binding{0};
            binding_kind kind{binding_kind::float_value};

            float float_value{0.0f};
            int int_value{0};
            infrastructure::math::vec2 vec2_value{};
            infrastructure::math::vec3 vec3_value{};
            infrastructure::math::vec4 vec4_value{};
            infrastructure::math::mat3 mat3_value{};
            infrastructure::math::mat4 mat4_value{};
            gpu::texture texture_value{};
            gpu::sampler sampler_value{};
        };

        struct bind_group_descriptor
        {
            bind_group_layout layout{};
            std::vector<binding_value> entries;
        };
    } // namespace gpu
} // namespace rendering_engine
