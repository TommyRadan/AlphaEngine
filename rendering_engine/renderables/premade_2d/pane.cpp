/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <infrastructure/log.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/premade_2d/pane.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rhi/rhi.hpp>

rendering_engine::pane::pane(const glm::vec2& size)
    : m_vertex_count{0}, m_vertex_array_object{nullptr}, m_vertex_buffer{nullptr}, m_indicies_buffer{nullptr},
      m_size{size}, m_color{0, 0, 0, 0}, m_texture{nullptr}
{
}

void rendering_engine::pane::set_color(const rendering_engine::util::color& color)
{
    m_color = color;
}

void rendering_engine::pane::set_image(const rendering_engine::util::image& image)
{
    rhi::device* device = rhi::get_device();

    rhi::texture_desc desc{};
    desc.initial_pixels = image.get_pixels();
    desc.width = image.get_width();
    desc.height = image.get_height();
    desc.source_format = rhi::pixel_format::rgba;
    desc.source_type = rhi::element_type::unsigned_byte_type;
    desc.storage_format = rhi::internal_format::rgba;
    m_texture = device->create_texture(desc);

    device->texture_set_wrap(
        m_texture, rhi::wrap_mode::clamp_edge, rhi::wrap_mode::clamp_edge, rhi::wrap_mode::clamp_edge);
    device->texture_set_filters(m_texture, rhi::filter_mode::nearest, rhi::filter_mode::nearest);
    device->texture_generate_mipmaps(m_texture);
}

void rendering_engine::pane::upload()
{
    this->m_vertex_count = 6;

    glm::vec3 position{this->transform.get_position()};

    vertex_position_uv vertex[4];
    vertex[0].pos = glm::vec3{position.x, position.y - m_size.y, -1.0f};
    vertex[0].uv = glm::vec2{0.0f, 0.0f};
    vertex[1].pos = glm::vec3{position.x + m_size.x, position.y - m_size.y, -1.0f};
    vertex[1].uv = glm::vec2{1.0f, 0.0f};
    vertex[2].pos = glm::vec3{position.x + m_size.x, position.y, -1.0f};
    vertex[2].uv = glm::vec2{1.0f, 1.0f};
    vertex[3].pos = glm::vec3{position.x, position.y, -1.0f};
    vertex[3].uv = glm::vec2{0.0f, 1.0f};

    uint32_t indicies[6] = {3, 0, 1, 3, 1, 2};

    rhi::device* device = rhi::get_device();

    rhi::buffer_desc vb_desc{};
    vb_desc.initial_data = vertex;
    vb_desc.size = sizeof(vertex);
    vb_desc.usage = rhi::buffer_usage::static_draw;
    vb_desc.is_index_buffer = false;
    m_vertex_buffer = device->create_buffer(vb_desc);

    rhi::buffer_desc ib_desc{};
    ib_desc.initial_data = indicies;
    ib_desc.size = sizeof(indicies);
    ib_desc.usage = rhi::buffer_usage::static_draw;
    ib_desc.is_index_buffer = true;
    m_indicies_buffer = device->create_buffer(ib_desc);

    m_vertex_array_object = device->create_vertex_array();

    rhi::vertex_attribute_desc attr0{};
    attr0.location = 0;
    attr0.source = m_vertex_buffer;
    attr0.type = rhi::element_type::float_type;
    attr0.component_count = 3;
    attr0.stride = sizeof(vertex_position_uv);
    attr0.offset = 0;
    device->vertex_array_bind_attribute(m_vertex_array_object, attr0);

    rhi::vertex_attribute_desc attr1{};
    attr1.location = 1;
    attr1.source = m_vertex_buffer;
    attr1.type = rhi::element_type::float_type;
    attr1.component_count = 2;
    attr1.stride = sizeof(vertex_position_uv);
    attr1.offset = sizeof(glm::vec3);
    device->vertex_array_bind_attribute(m_vertex_array_object, attr1);

    device->vertex_array_bind_elements(m_vertex_array_object, m_indicies_buffer);
}

void rendering_engine::pane::render()
{
    renderer* current_renderer{rendering_engine::renderer::get_current_renderer()};

    if (!current_renderer)
    {
        LOG_WRN("Attempted to render without renderer attached");
        return;
    }

    options.colors["color"] = this->m_color;
    if (this->m_texture)
    {
        options.textures["tex"] = this->m_texture;
        options.coefficients["useTexture"] = 1.0f;
    }
    else
    {
        options.coefficients["useTexture"] = 0.0f;
    }
    current_renderer->setup_options(options);

    rhi::draw_call call{};
    call.vao = m_vertex_array_object;
    call.topology = rhi::primitive_type::triangles;
    call.indexed = true;
    call.offset = 0;
    call.count = m_vertex_count;
    call.index_type = rhi::element_type::unsigned_int_type;
    rhi::get_device()->draw(call);
}
