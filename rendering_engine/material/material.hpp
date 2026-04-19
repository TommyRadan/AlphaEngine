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

#pragma once

#include <glm.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <rendering_engine/opengl/texture.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine
{
    struct renderer;

    /**
     * Strongly-typed value any shader uniform slot can hold. New types are added
     * to this variant rather than as free-form strings, so mismatches surface at
     * compile time.
     */
    using uniform_value =
        std::variant<float, glm::vec2, glm::vec3, glm::vec4, glm::mat3, glm::mat4, rendering_engine::util::color>;

    /**
     * Binding of a texture to a named sampler uniform. The slot (texture unit)
     * is assigned automatically by material::bind() in insertion order.
     */
    struct texture_slot
    {
        std::string name;
        rendering_engine::opengl::texture* texture;
    };

    /**
     * Typed replacement for the old render_options map. A material owns a set
     * of uniform values and texture bindings, plus a non-owning reference to
     * the renderer whose shader program those uniforms target. Multiple
     * renderables can share a single material via std::shared_ptr<material>.
     */
    struct material
    {
        material() = default;
        explicit material(renderer* owning_renderer);

        /**
         * Set the renderer (and therefore the shader program) this material
         * writes uniforms into.
         */
        void set_renderer(renderer* owning_renderer);

        renderer* get_renderer() const;

        /**
         * Set a typed uniform value by name. Overwrites any existing value
         * stored under the same name (including a previously stored value of a
         * different variant alternative).
         */
        void set_uniform(const std::string& name, const uniform_value& value);

        /**
         * Set a texture binding by sampler uniform name. If a binding with the
         * same name already exists its texture is replaced; otherwise a new
         * slot is appended.
         */
        void set_texture(const std::string& name, rendering_engine::opengl::texture* texture);

        /**
         * Upload every stored uniform and bind every stored texture onto the
         * currently active renderer's shader program. Must be called while the
         * matching renderer is started (renderer::start_renderer()).
         */
        void bind() const;

        const std::unordered_map<std::string, uniform_value>& get_uniforms() const;
        const std::vector<texture_slot>& get_textures() const;

    private:
        renderer* m_renderer{nullptr};
        std::unordered_map<std::string, uniform_value> m_uniforms;
        std::vector<texture_slot> m_textures;
    };
} // namespace rendering_engine
