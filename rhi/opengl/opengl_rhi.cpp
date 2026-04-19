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

#include <stdexcept>

#include <SDL3/SDL_video.h>

#include <infrastructure/log.hpp>
#include <rhi/opengl/gl_typedef.hpp>
#include <rhi/opengl/opengl_resources.hpp>
#include <rhi/opengl/opengl_rhi.hpp>

namespace rhi
{
    namespace opengl
    {
        namespace
        {
            gl_buffer* to_gl(buffer* b)
            {
                return static_cast<gl_buffer*>(b);
            }
            const gl_buffer* to_gl(const buffer* b)
            {
                return static_cast<const gl_buffer*>(b);
            }
            gl_texture* to_gl(texture* t)
            {
                return static_cast<gl_texture*>(t);
            }
            const gl_texture* to_gl(const texture* t)
            {
                return static_cast<const gl_texture*>(t);
            }
            gl_shader* to_gl(shader* s)
            {
                return static_cast<gl_shader*>(s);
            }
            const gl_shader* to_gl(const shader* s)
            {
                return static_cast<const gl_shader*>(s);
            }
            gl_program* to_gl(program* p)
            {
                return static_cast<gl_program*>(p);
            }
            gl_vertex_array* to_gl(vertex_array* v)
            {
                return static_cast<gl_vertex_array*>(v);
            }
            const gl_vertex_array* to_gl(const vertex_array* v)
            {
                return static_cast<const gl_vertex_array*>(v);
            }
            gl_framebuffer* to_gl(framebuffer* f)
            {
                return static_cast<gl_framebuffer*>(f);
            }
            const gl_framebuffer* to_gl(const framebuffer* f)
            {
                return static_cast<const gl_framebuffer*>(f);
            }
        } // namespace

        void opengl_rhi::init()
        {
            LOG_INF("Init rhi::opengl_rhi");

            if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)))
            {
                LOG_FTL("Could not initialize OpenGL (glad failed to load GL functions)");
                throw std::runtime_error{"Could not initialize OpenGL"};
            }

            int version_major = 0;
            int version_minor = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &version_major);
            glGetIntegerv(GL_MINOR_VERSION, &version_minor);

            if (version_major < 3 || (version_major == 3 && version_minor < 3))
            {
                LOG_FTL("Could not initialize OpenGL, supported version is %i.%i", version_major, version_minor);
                throw std::runtime_error{"OpenGL version error! Unsupported hardware or driver"};
            }

            const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            const char* gl_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const char* gl_glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

            LOG_INF("OpenGL context: version=%i.%i", version_major, version_minor);
            LOG_INF("OpenGL vendor:   %s", gl_vendor != nullptr ? gl_vendor : "<unknown>");
            LOG_INF("OpenGL renderer: %s", gl_renderer != nullptr ? gl_renderer : "<unknown>");
            LOG_INF("OpenGL version:  %s", gl_version != nullptr ? gl_version : "<unknown>");
            LOG_INF("GLSL version:    %s", gl_glsl != nullptr ? gl_glsl : "<unknown>");
        }

        void opengl_rhi::quit()
        {
            LOG_INF("Quit rhi::opengl_rhi");
        }

        buffer* opengl_rhi::create_buffer(const buffer_desc& desc)
        {
            auto* b = new gl_buffer();
            b->is_index = desc.is_index_buffer;
            const GLenum target = desc.is_index_buffer ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
            glBindBuffer(target, b->id);
            glBufferData(target, static_cast<GLsizeiptr>(desc.size), desc.initial_data, to_gl(desc.usage));
            return b;
        }

        void opengl_rhi::destroy_buffer(buffer* b)
        {
            delete to_gl(b);
        }

        void opengl_rhi::buffer_sub_data(buffer* b, std::size_t offset, std::size_t length, const void* data)
        {
            auto* gb = to_gl(b);
            const GLenum target = gb->is_index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
            glBindBuffer(target, gb->id);
            glBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(length), data);
        }

        texture* opengl_rhi::create_texture(const texture_desc& desc)
        {
            auto* t = new gl_texture();
            GLint previous = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);

            glBindTexture(GL_TEXTURE_2D, t->id);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         to_gl(desc.storage_format),
                         static_cast<GLsizei>(desc.width),
                         static_cast<GLsizei>(desc.height),
                         0,
                         to_gl(desc.source_format),
                         to_gl(desc.source_type),
                         desc.initial_pixels);

            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
            return t;
        }

        void opengl_rhi::destroy_texture(texture* t)
        {
            delete to_gl(t);
        }

        void opengl_rhi::texture_set_wrap(texture* t, wrap_mode s, wrap_mode tt, wrap_mode r)
        {
            auto* gt = to_gl(t);
            GLint previous = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
            glBindTexture(GL_TEXTURE_2D, gt->id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, to_gl(s));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, to_gl(tt));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, to_gl(r));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        }

        void opengl_rhi::texture_set_filters(texture* t, filter_mode min_filter, filter_mode mag_filter)
        {
            auto* gt = to_gl(t);
            GLint previous = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
            glBindTexture(GL_TEXTURE_2D, gt->id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, to_gl(min_filter));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, to_gl(mag_filter));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        }

        void opengl_rhi::texture_generate_mipmaps(texture* t)
        {
            auto* gt = to_gl(t);
            GLint previous = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
            glBindTexture(GL_TEXTURE_2D, gt->id);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        }

        shader* opengl_rhi::create_shader(shader_stage stage, const std::string& source)
        {
            auto* s = new gl_shader();
            s->id = glCreateShader(to_gl(stage));
            if (s->id == 0)
            {
                LOG_FTL("Cannot create shader (stage=%u)", static_cast<unsigned>(stage));
                delete s;
                throw std::runtime_error{"Shader creation failed!"};
            }
            s->source_code = source;
            const char* src = s->source_code.c_str();
            const auto length = static_cast<GLint>(s->source_code.length());
            glShaderSource(s->id, 1, &src, &length);

            GLint status = GL_FALSE;
            glCompileShader(s->id);
            glGetShaderiv(s->id, GL_COMPILE_STATUS, &status);
            if (status != GL_TRUE)
            {
                GLint log_len = 0;
                glGetShaderiv(s->id, GL_INFO_LOG_LENGTH, &log_len);
                std::string info_log(static_cast<std::size_t>(log_len > 0 ? log_len : 0), '\0');
                if (log_len > 0)
                {
                    glGetShaderInfoLog(s->id, log_len, &log_len, info_log.data());
                }
                LOG_ERR("Shader compile failed (id=%u)", s->id);
                LOG_ERR("Shader code: \n%s", s->source_code.c_str());
                LOG_ERR("Info log: \n%s", info_log.c_str());
                delete s;
                throw std::runtime_error{"Shader compile failed"};
            }
            LOG_INF("Shader compiled successfully (id=%u)", s->id);
            return s;
        }

        void opengl_rhi::destroy_shader(shader* s)
        {
            delete to_gl(s);
        }

        program* opengl_rhi::create_program()
        {
            auto* p = new gl_program();
            p->id = glCreateProgram();
            if (p->id == 0)
            {
                LOG_ERR("glCreateProgram returned 0 (program creation failed)");
            }
            else
            {
                LOG_INF("Created GL program id=%u", p->id);
            }
            return p;
        }

        void opengl_rhi::destroy_program(program* p)
        {
            delete to_gl(p);
        }

        void opengl_rhi::program_attach(program* p, const shader* s)
        {
            glAttachShader(to_gl(p)->id, to_gl(s)->id);
        }

        void opengl_rhi::program_link(program* p)
        {
            auto* gp = to_gl(p);
            GLint status = GL_FALSE;
            glLinkProgram(gp->id);
            glGetProgramiv(gp->id, GL_LINK_STATUS, &status);

            if (status != GL_TRUE)
            {
                GLint log_len = 0;
                glGetProgramiv(gp->id, GL_INFO_LOG_LENGTH, &log_len);
                std::string info_log(static_cast<std::size_t>(log_len > 0 ? log_len : 0), '\0');
                if (log_len > 0)
                {
                    glGetProgramInfoLog(gp->id, log_len, &log_len, info_log.data());
                }
                LOG_ERR("Program link failed (id=%u)", gp->id);
                LOG_ERR("Info log: \n%s", info_log.c_str());
                LOG_FTL("Cannot link program");
                throw std::runtime_error{info_log};
            }

            LOG_INF("GL program linked successfully (id=%u)", gp->id);
        }

        void opengl_rhi::program_start(program* p)
        {
            glUseProgram(to_gl(p)->id);
        }

        void opengl_rhi::program_stop(program* /*p*/)
        {
            glUseProgram(0);
        }

        int32_t opengl_rhi::program_get_uniform_location(program* p, const std::string& name)
        {
            auto* gp = to_gl(p);
            auto it = gp->uniforms.find(name);
            if (it != gp->uniforms.end())
            {
                return it->second;
            }
            const GLint location = glGetUniformLocation(gp->id, name.c_str());
            gp->uniforms[name] = location;
            return location;
        }

        void opengl_rhi::program_set_uniform(program* /*p*/, int32_t location, int32_t value)
        {
            glUniform1i(location, value);
        }

        void opengl_rhi::program_set_uniform(program* /*p*/, int32_t location, float value)
        {
            glUniform1f(location, value);
        }

        void opengl_rhi::program_set_uniform(program* /*p*/, int32_t location, const glm::vec2& value)
        {
            glUniform2f(location, value.x, value.y);
        }

        void opengl_rhi::program_set_uniform(program* /*p*/, int32_t location, const glm::vec3& value)
        {
            glUniform3f(location, value.x, value.y, value.z);
        }

        void opengl_rhi::program_set_uniform(program* /*p*/, int32_t location, const glm::vec4& value)
        {
            glUniform4f(location, value.x, value.y, value.z, value.w);
        }

        void opengl_rhi::program_set_uniform(program* /*p*/, int32_t location, const glm::mat3& value)
        {
            glUniformMatrix3fv(location, 1, GL_FALSE, &value[0][0]);
        }

        void opengl_rhi::program_set_uniform(program* /*p*/, int32_t location, const glm::mat4& value)
        {
            glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
        }

        vertex_array* opengl_rhi::create_vertex_array()
        {
            return new gl_vertex_array();
        }

        void opengl_rhi::destroy_vertex_array(vertex_array* v)
        {
            delete to_gl(v);
        }

        void opengl_rhi::vertex_array_bind_attribute(vertex_array* v, const vertex_attribute_desc& desc)
        {
            glBindVertexArray(to_gl(v)->id);
            glBindBuffer(GL_ARRAY_BUFFER, to_gl(desc.source)->id);
            glEnableVertexAttribArray(desc.location);
            glVertexAttribPointer(desc.location,
                                  static_cast<GLint>(desc.component_count),
                                  to_gl(desc.type),
                                  GL_FALSE,
                                  static_cast<GLsizei>(desc.stride),
                                  reinterpret_cast<const GLvoid*>(desc.offset));
        }

        void opengl_rhi::vertex_array_bind_elements(vertex_array* v, const buffer* elements)
        {
            glBindVertexArray(to_gl(v)->id);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, to_gl(elements)->id);
        }

        framebuffer* opengl_rhi::create_framebuffer(const framebuffer_desc& desc)
        {
            auto* fb = new gl_framebuffer();

            GLint previous = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);

            internal_format color_fmt{};
            if (desc.color_bits == 24)
            {
                color_fmt = internal_format::rgb;
            }
            else if (desc.color_bits == 32)
            {
                color_fmt = internal_format::rgba;
            }
            else
            {
                LOG_ERR("Framebuffer could not be created, color size not supported (%u)", desc.color_bits);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
                delete fb;
                return nullptr;
            }

            internal_format depth_fmt{};
            if (desc.depth_bits == 8)
            {
                depth_fmt = internal_format::depth_component;
            }
            else if (desc.depth_bits == 16)
            {
                depth_fmt = internal_format::depth_component16;
            }
            else if (desc.depth_bits == 24)
            {
                depth_fmt = internal_format::depth_component24;
            }
            else if (desc.depth_bits == 32)
            {
                depth_fmt = internal_format::depth_component32f;
            }
            else if (desc.depth_bits != 0)
            {
                LOG_ERR("Framebuffer could not be created, depth size not supported (%u)", desc.depth_bits);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
                delete fb;
                return nullptr;
            }

            glGenFramebuffers(1, &fb->id);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->id);

            // Color attachment.
            {
                GLint prev_tex = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
                glBindTexture(GL_TEXTURE_2D, fb->color_tex.id);
                glTexImage2D(GL_TEXTURE_2D,
                             0,
                             to_gl(color_fmt),
                             static_cast<GLsizei>(desc.width),
                             static_cast<GLsizei>(desc.height),
                             0,
                             GL_RGBA,
                             GL_UNSIGNED_BYTE,
                             nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex));
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->color_tex.id, 0);
            }

            // Depth attachment.
            if (desc.depth_bits > 0U)
            {
                GLint prev_tex = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
                glBindTexture(GL_TEXTURE_2D, fb->depth_tex.id);
                glTexImage2D(GL_TEXTURE_2D,
                             0,
                             to_gl(depth_fmt),
                             static_cast<GLsizei>(desc.width),
                             static_cast<GLsizei>(desc.height),
                             0,
                             GL_DEPTH_COMPONENT,
                             GL_UNSIGNED_BYTE,
                             nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev_tex));
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb->depth_tex.id, 0);
            }

            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                LOG_ERR("Framebuffer could not be created, unknown reason (0x%X)",
                        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
                delete fb;
                throw std::runtime_error{"Framebuffer could not be created!"};
            }

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
            return fb;
        }

        void opengl_rhi::destroy_framebuffer(framebuffer* f)
        {
            delete to_gl(f);
        }

        const texture* opengl_rhi::framebuffer_color_texture(const framebuffer* f)
        {
            return &to_gl(f)->color_tex;
        }

        const texture* opengl_rhi::framebuffer_depth_texture(const framebuffer* f)
        {
            return &to_gl(f)->depth_tex;
        }

        void opengl_rhi::enable(capability cap)
        {
            glEnable(to_gl(cap));
        }

        void opengl_rhi::disable(capability cap)
        {
            glDisable(to_gl(cap));
        }

        void opengl_rhi::set_clear_color(float r, float g, float b, float a)
        {
            glClearColor(r, g, b, a);
        }

        void opengl_rhi::clear(clear_buffer buffers)
        {
            glClear(to_gl_clear_mask(buffers));
        }

        void opengl_rhi::set_depth_mask(bool write_enabled)
        {
            glDepthMask(write_enabled ? GL_TRUE : GL_FALSE);
        }

        void opengl_rhi::set_blend_func_alpha()
        {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        void opengl_rhi::bind_texture(const texture* t, uint8_t unit)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, t != nullptr ? to_gl(t)->id : 0);
        }

        void opengl_rhi::bind_framebuffer(const framebuffer* f)
        {
            if (f == nullptr)
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                return;
            }

            const auto* gf = to_gl(f);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gf->id);

            GLint obj = 0;
            GLint width = 0;
            GLint height = 0;
            glGetFramebufferAttachmentParameteriv(
                GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &obj);

            GLint res = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &res);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(obj));
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(res));

            glViewport(0, 0, width, height);
        }

        void opengl_rhi::bind_default_framebuffer()
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        }

        void opengl_rhi::draw(const draw_call& call)
        {
            if (call.vao == nullptr)
            {
                return;
            }
            glBindVertexArray(to_gl(call.vao)->id);
            if (call.indexed)
            {
                glDrawElements(to_gl(call.topology),
                               static_cast<GLsizei>(call.count),
                               to_gl(call.index_type),
                               reinterpret_cast<const GLvoid*>(call.offset));
            }
            else
            {
                glDrawArrays(to_gl(call.topology), static_cast<GLint>(call.offset), static_cast<GLsizei>(call.count));
            }
            glBindVertexArray(0);
        }
    } // namespace opengl
} // namespace rhi
