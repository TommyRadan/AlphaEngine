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

#include <cstdint>

#include <infrastructure/log.hpp>
#include <rendering_engine/material/material.hpp>
#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/renderers/renderer.hpp>

rendering_engine::material::material(renderer* owning_renderer) : m_renderer{owning_renderer} {}

void rendering_engine::material::set_renderer(renderer* owning_renderer)
{
    m_renderer = owning_renderer;
}

rendering_engine::renderer* rendering_engine::material::get_renderer() const
{
    return m_renderer;
}

void rendering_engine::material::set_uniform(const std::string& name, const uniform_value& value)
{
    m_uniforms[name] = value;
}

void rendering_engine::material::set_texture(const std::string& name, rendering_engine::opengl::texture* texture)
{
    for (auto& slot : m_textures)
    {
        if (slot.name == name)
        {
            slot.texture = texture;
            return;
        }
    }
    m_textures.push_back(texture_slot{name, texture});
}

void rendering_engine::material::bind() const
{
    renderer* active_renderer = m_renderer != nullptr ? m_renderer : rendering_engine::renderer::get_current_renderer();

    if (active_renderer == nullptr)
    {
        LOG_WRN("Attempted to bind material without an active renderer");
        return;
    }

    for (const auto& [name, value] : m_uniforms)
    {
        std::visit(
            [&](const auto& v)
            {
                using type = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<type, float>)
                {
                    active_renderer->upload_coefficient(name, v);
                }
                else if constexpr (std::is_same_v<type, glm::vec2>)
                {
                    active_renderer->upload_vector2(name, v);
                }
                else if constexpr (std::is_same_v<type, glm::vec3>)
                {
                    active_renderer->upload_vector3(name, v);
                }
                else if constexpr (std::is_same_v<type, glm::vec4>)
                {
                    active_renderer->upload_vector4(name, v);
                }
                else if constexpr (std::is_same_v<type, glm::mat3>)
                {
                    active_renderer->upload_matrix3(name, v);
                }
                else if constexpr (std::is_same_v<type, glm::mat4>)
                {
                    active_renderer->upload_matrix4(name, v);
                }
                else if constexpr (std::is_same_v<type, rendering_engine::util::color>)
                {
                    const glm::vec4 rgba{static_cast<float>(v.r) / 255.0f,
                                         static_cast<float>(v.g) / 255.0f,
                                         static_cast<float>(v.b) / 255.0f,
                                         static_cast<float>(v.a) / 255.0f};
                    active_renderer->upload_vector4(name, rgba);
                }
            },
            value);
    }

    uint8_t unit = 0u;
    for (const auto& slot : m_textures)
    {
        if (slot.texture == nullptr)
        {
            continue;
        }
        active_renderer->upload_texture_reference(slot.name, unit);
        opengl::context::get_instance().bind_texture(*slot.texture, unit);
        unit++;
    }
}

const std::unordered_map<std::string, rendering_engine::uniform_value>& rendering_engine::material::get_uniforms() const
{
    return m_uniforms;
}

const std::vector<rendering_engine::texture_slot>& rendering_engine::material::get_textures() const
{
    return m_textures;
}
