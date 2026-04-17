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
#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>

rendering_engine::renderer* rendering_engine::renderer::m_current_renderer = nullptr;

void rendering_engine::renderer::start_renderer()
{
    m_current_renderer = this;
    static_cast<opengl::program*>(m_program)->start();
}

void rendering_engine::renderer::stop_renderer()
{
    m_current_renderer = nullptr;
    static_cast<opengl::program*>(m_program)->stop();
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
        opengl::context::get_instance().bind_texture(*element.second, uploaded_texture_references);
        uploaded_texture_references++;
    }
}

void rendering_engine::renderer::upload_texture_reference(const std::string& texture_name, const int position)
{
    opengl::uniform uniform = static_cast<opengl::program*>(m_program)->get_uniform(texture_name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", texture_name.c_str());
        return;
    }
    static_cast<opengl::program*>(m_program)->set_uniform(uniform, position);
}

void rendering_engine::renderer::upload_coefficient(const std::string& coefficient_name, const float coefficient)
{
    opengl::uniform uniform = static_cast<opengl::program*>(m_program)->get_uniform(coefficient_name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", coefficient_name.c_str());
        return;
    }
    static_cast<opengl::program*>(m_program)->set_uniform(uniform, coefficient);
}

void rendering_engine::renderer::upload_matrix3(const std::string& mat3_name, const glm::mat3& matrix)
{
    opengl::uniform uniform = static_cast<opengl::program*>(m_program)->get_uniform(mat3_name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", mat3_name.c_str());
        return;
    }
    static_cast<opengl::program*>(m_program)->set_uniform(uniform, matrix);
}

void rendering_engine::renderer::upload_matrix4(const std::string& mat4_name, const glm::mat4& matrix)
{
    opengl::uniform uniform = static_cast<opengl::program*>(m_program)->get_uniform(mat4_name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", mat4_name.c_str());
        return;
    }
    static_cast<opengl::program*>(m_program)->set_uniform(uniform, matrix);
}

void rendering_engine::renderer::upload_vector2(const std::string& vec2_name, const glm::vec2& vector)
{
    opengl::uniform uniform = static_cast<opengl::program*>(m_program)->get_uniform(vec2_name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec2_name.c_str());
        return;
    }
    static_cast<opengl::program*>(m_program)->set_uniform(uniform, vector);
}

void rendering_engine::renderer::upload_vector3(const std::string& vec3_name, const glm::vec3& vector)
{
    opengl::uniform uniform = static_cast<opengl::program*>(m_program)->get_uniform(vec3_name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec3_name.c_str());
        return;
    }
    static_cast<opengl::program*>(m_program)->set_uniform(uniform, vector);
}

void rendering_engine::renderer::upload_vector4(const std::string& vec4_name, const glm::vec4& vector)
{
    opengl::uniform uniform = static_cast<opengl::program*>(m_program)->get_uniform(vec4_name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec4_name.c_str());
        return;
    }
    static_cast<opengl::program*>(m_program)->set_uniform(uniform, vector);
}

void rendering_engine::renderer::construct_program(const std::string& vs_string, const std::string& fs_string)
{
    m_vertex_shader = opengl::context::get_instance().create_shader(opengl::shader_type::vertex);
    m_fragment_shader = opengl::context::get_instance().create_shader(opengl::shader_type::fragment);
    m_program = opengl::context::get_instance().create_program();

    auto vs = (opengl::shader*)m_vertex_shader;
    auto fs = (opengl::shader*)m_fragment_shader;
    auto prog = (opengl::program*)m_program;

    vs->source(vs_string);
    vs->compile();

    fs->source(fs_string);
    fs->compile();

    prog->attach(*vs);
    prog->attach(*fs);
    prog->link();
}

void rendering_engine::renderer::destruct_program()
{
    opengl::context::get_instance().delete_shader((opengl::shader*)m_vertex_shader);
    opengl::context::get_instance().delete_shader((opengl::shader*)m_fragment_shader);
    opengl::context::get_instance().delete_program((opengl::program*)m_program);
}
