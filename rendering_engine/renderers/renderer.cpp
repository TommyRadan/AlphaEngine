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
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rhi/rhi.hpp>

rendering_engine::renderer* rendering_engine::renderer::m_current_renderer = nullptr;

void rendering_engine::shader_deleter::operator()(rhi::shader* s) const noexcept
{
    if (s != nullptr)
    {
        if (auto* device = rhi::get_device(); device != nullptr)
        {
            device->destroy_shader(s);
        }
    }
}

void rendering_engine::program_deleter::operator()(rhi::program* p) const noexcept
{
    if (p != nullptr)
    {
        if (auto* device = rhi::get_device(); device != nullptr)
        {
            device->destroy_program(p);
        }
    }
}

rendering_engine::renderer::~renderer() = default;

void rendering_engine::renderer::start_renderer()
{
    m_current_renderer = this;
    rhi::get_device()->program_start(m_program.get());
}

void rendering_engine::renderer::stop_renderer()
{
    m_current_renderer = nullptr;
    rhi::get_device()->program_stop(m_program.get());
}

rendering_engine::renderer* rendering_engine::renderer::get_current_renderer()
{
    return m_current_renderer;
}

void rendering_engine::renderer::setup_camera()
{
    camera* current_camera{rendering_engine::camera::get_current_camera()};

    if (current_camera == nullptr)
    {
        LOG_WRN("Trying to setup a camera without camera attached");
        return;
    }

    this->upload_matrix4("viewMatrix", current_camera->get_view_matrix());
    this->upload_matrix4("projectionMatrix", current_camera->get_projection_matrix());
}

void rendering_engine::renderer::setup_options(const render_options& options)
{
    for (auto& element : options.coefficients)
    {
        this->upload_coefficient(element.first, element.second);
    }

    for (auto& element : options.colors)
    {
        auto vector = glm::vec4{(float)element.second.r / 255.0f,
                                (float)element.second.g / 255.0f,
                                (float)element.second.b / 255.0f,
                                (float)element.second.a / 255.0f};

        this->upload_vector4(element.first, vector);
    }

    uint8_t uploaded_texture_references = 0u;
    for (auto& element : options.textures)
    {
        this->upload_texture_reference(element.first, uploaded_texture_references);
        rhi::get_device()->bind_texture(element.second, uploaded_texture_references);
        uploaded_texture_references++;
    }
}

void rendering_engine::renderer::upload_texture_reference(const std::string& texture_name, const int position)
{
    rhi::device* device = rhi::get_device();
    int32_t location = device->program_get_uniform_location(m_program.get(), texture_name);
    if (location == -1)
    {
        LOG_WRN("Cannot find uniform: %s", texture_name.c_str());
        return;
    }
    device->program_set_uniform(m_program.get(), location, static_cast<int32_t>(position));
}

void rendering_engine::renderer::upload_coefficient(const std::string& coefficient_name, const float coefficient)
{
    rhi::device* device = rhi::get_device();
    int32_t location = device->program_get_uniform_location(m_program.get(), coefficient_name);
    if (location == -1)
    {
        LOG_WRN("Cannot find uniform: %s", coefficient_name.c_str());
        return;
    }
    device->program_set_uniform(m_program.get(), location, coefficient);
}

void rendering_engine::renderer::upload_matrix3(const std::string& mat3_name, const glm::mat3& matrix)
{
    rhi::device* device = rhi::get_device();
    int32_t location = device->program_get_uniform_location(m_program.get(), mat3_name);
    if (location == -1)
    {
        LOG_WRN("Cannot find uniform: %s", mat3_name.c_str());
        return;
    }
    device->program_set_uniform(m_program.get(), location, matrix);
}

void rendering_engine::renderer::upload_matrix4(const std::string& mat4_name, const glm::mat4& matrix)
{
    rhi::device* device = rhi::get_device();
    int32_t location = device->program_get_uniform_location(m_program.get(), mat4_name);
    if (location == -1)
    {
        LOG_WRN("Cannot find uniform: %s", mat4_name.c_str());
        return;
    }
    device->program_set_uniform(m_program.get(), location, matrix);
}

void rendering_engine::renderer::upload_vector2(const std::string& vec2_name, const glm::vec2& vector)
{
    rhi::device* device = rhi::get_device();
    int32_t location = device->program_get_uniform_location(m_program.get(), vec2_name);
    if (location == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec2_name.c_str());
        return;
    }
    device->program_set_uniform(m_program.get(), location, vector);
}

void rendering_engine::renderer::upload_vector3(const std::string& vec3_name, const glm::vec3& vector)
{
    rhi::device* device = rhi::get_device();
    int32_t location = device->program_get_uniform_location(m_program.get(), vec3_name);
    if (location == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec3_name.c_str());
        return;
    }
    device->program_set_uniform(m_program.get(), location, vector);
}

void rendering_engine::renderer::upload_vector4(const std::string& vec4_name, const glm::vec4& vector)
{
    rhi::device* device = rhi::get_device();
    int32_t location = device->program_get_uniform_location(m_program.get(), vec4_name);
    if (location == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec4_name.c_str());
        return;
    }
    device->program_set_uniform(m_program.get(), location, vector);
}

void rendering_engine::renderer::construct_program(const std::string& vs_string, const std::string& fs_string)
{
    rhi::device* device = rhi::get_device();

    m_vertex_shader.reset(device->create_shader(rhi::shader_stage::vertex, vs_string));
    m_fragment_shader.reset(device->create_shader(rhi::shader_stage::fragment, fs_string));
    m_program.reset(device->create_program());

    device->program_attach(m_program.get(), m_vertex_shader.get());
    device->program_attach(m_program.get(), m_fragment_shader.get());
    device->program_link(m_program.get());
}

void rendering_engine::renderer::destruct_program()
{
    m_vertex_shader.reset();
    m_fragment_shader.reset();
    m_program.reset();
}
