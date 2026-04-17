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

#pragma once

#include <glm.hpp>
#include <string>

#include <rendering_engine/renderers/render_options.hpp>

namespace rendering_engine
{
    struct renderer
    {
        void start_renderer();
        void stop_renderer();

        static renderer* get_current_renderer();

        void setup_camera();
        void setup_options(const render_options& options);

        void upload_texture_reference(const std::string& texture_name, int position);
        void upload_coefficient(const std::string& coefficient_name, float coefficient);
        void upload_matrix3(const std::string& mat3_name, const glm::mat3& matrix);
        void upload_matrix4(const std::string& mat4_name, const glm::mat4& matrix);
        void upload_vector2(const std::string& vec2_name, const glm::vec2& vector);
        void upload_vector3(const std::string& vec3_name, const glm::vec3& vector);
        void upload_vector4(const std::string& vec4_name, const glm::vec4& vector);

    protected:
        renderer() = default;

        void construct_program(const std::string& vs_string, const std::string& fs_string);
        void destruct_program();

        void* m_vertex_shader;
        void* m_fragment_shader;
        void* m_program;

        static renderer* m_current_renderer;
    };
} // namespace rendering_engine
