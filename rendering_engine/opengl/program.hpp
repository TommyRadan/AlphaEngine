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

#include <rendering_engine/opengl/shader.hpp>
#include <rendering_engine/opengl/typedef.hpp>

#include <glm/glm.hpp>
#include <map>
#include <string>

namespace rendering_engine
{
    namespace opengl
    {
        class program
        {
            friend class context;
            program();
            ~program();

            program(const program&) = delete;
            program(const program&&) = delete;
            const program& operator=(const program&) = delete;
            const program&& operator=(const program&&) = delete;

        public:
            const uint32_t handle() const;

            void start();
            void stop();

            void attach(const shader& shader);
            void link();

            std::string get_info_log();

            attribute get_attribute(const std::string& name);
            uniform get_uniform(const std::string& name);

            template<typename T>
            void set_uniform(const std::string& name, const T& value)
            {
                this->set_uniform(this->get_uniform(name), value);
            }

            template<typename T>
            void set_uniform(const std::string& name, const T* values, unsigned int count)
            {
                this->set_uniform(this->get_uniform(name), values, count);
            }

            void set_uniform(const uniform& uniform, int value);
            void set_uniform(const uniform& uniform, float value);
            void set_uniform(const uniform& uniform, const glm::vec2& value);
            void set_uniform(const uniform& uniform, const glm::vec3& value);
            void set_uniform(const uniform& uniform, const glm::vec4& value);
            void set_uniform(const uniform& uniform, const float* values, unsigned int count);
            void set_uniform(const uniform& uniform, const glm::vec2* values, unsigned int count);
            void set_uniform(const uniform& uniform, const glm::vec3* values, unsigned int count);
            void set_uniform(const uniform& uniform, const glm::vec4* values, unsigned int count);
            void set_uniform(const uniform& uniform, const glm::mat3& value);
            void set_uniform(const uniform& uniform, const glm::mat4& value);

        private:
            unsigned int m_object_id;
            std::map<std::string, uniform> m_uniforms;
            std::map<std::string, attribute> m_attributes;
        };
    } // namespace opengl
} // namespace rendering_engine
