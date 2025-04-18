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

#include <RenderingEngine/Renderers/Renderer.hpp>
#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <RenderingEngine/Camera/Camera.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <Infrastructure/log.hpp>

RenderingEngine::Renderer *RenderingEngine::Renderer::m_CurrentRenderer = nullptr;

void RenderingEngine::Renderer::StartRenderer()
{
    m_CurrentRenderer = this;
    static_cast<OpenGL::Program*>(m_Program)->Start();
}

void RenderingEngine::Renderer::StopRenderer()
{
    m_CurrentRenderer = nullptr;
    static_cast<OpenGL::Program*>(m_Program)->Stop();
}

RenderingEngine::Renderer *RenderingEngine::Renderer::GetCurrentRenderer()
{
    return m_CurrentRenderer;
}

void RenderingEngine::Renderer::SetupCamera()
{
    Camera* currentCamera { RenderingEngine::Camera::GetCurrentCamera() };

    if (currentCamera == nullptr)
    {
        LOG_WRN("Trying to setup a camera without camera attached");
        return;
    }

    this->UploadMatrix4("viewMatrix", currentCamera->GetViewMatrix());
    this->UploadMatrix4("projectionMatrix", currentCamera->GetProjectionMatrix());
}

void RenderingEngine::Renderer::SetupOptions(const RenderOptions& options)
{
    for (auto& element : options.Coefficients)
    {
        this->UploadCoefficient(element.first, element.second);
    }

    for (auto& element : options.Colors)
    {
        auto vector = glm::vec4 {
                (float) element.second.r / 255.0f,
                (float) element.second.g / 255.0f,
                (float) element.second.b / 255.0f,
                (float) element.second.a / 255.0f
        };

        this->UploadVector4(element.first, vector);
    }

    uint8_t uploadedTextureReferences = 0u;
    for (auto& element : options.Textures)
    {
        this->UploadTextureReference(element.first, uploadedTextureReferences);
        OpenGL::Context::get_instance().BindTexture(*element.second, uploadedTextureReferences);
        uploadedTextureReferences++;
    }
}

void RenderingEngine::Renderer::UploadTextureReference(const std::string& textureName, const int position)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(textureName);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", textureName.c_str());
        return;
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, position);
}

void RenderingEngine::Renderer::UploadCoefficient(const std::string& coefficientName, const float coefficient)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(coefficientName);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", coefficientName.c_str());
        return;
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, coefficient);
}

void RenderingEngine::Renderer::UploadMatrix3(const std::string& mat3Name, const glm::mat3& matrix)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(mat3Name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", mat3Name.c_str());
        return;
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, matrix);
}

void RenderingEngine::Renderer::UploadMatrix4(const std::string& mat4Name, const glm::mat4& matrix)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(mat4Name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", mat4Name.c_str());
        return;
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, matrix);
}

void RenderingEngine::Renderer::UploadVector2(const std::string& vec2Name, const glm::vec2& vector)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec2Name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec2Name.c_str());
        return;
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void RenderingEngine::Renderer::UploadVector3(const std::string& vec3Name, const glm::vec3& vector)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec3Name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec3Name.c_str());
        return;
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void RenderingEngine::Renderer::UploadVector4(const std::string& vec4Name, const glm::vec4& vector)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec4Name);
    if (uniform == -1)
    {
        LOG_WRN("Cannot find uniform: %s", vec4Name.c_str());
        return;
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void RenderingEngine::Renderer::ConstructProgram(const std::string& vsString, const std::string& fsString)
{
    m_VertexShader = OpenGL::Context::get_instance().CreateShader(OpenGL::ShaderType::Vertex);
    m_FragmentShader = OpenGL::Context::get_instance().CreateShader(OpenGL::ShaderType::Fragment);
    m_Program = OpenGL::Context::get_instance().CreateProgram();

    auto vs = (OpenGL::Shader*) m_VertexShader;
    auto fs = (OpenGL::Shader*) m_FragmentShader;
    auto prog = (OpenGL::Program*) m_Program;

    vs->Source(vsString);
    vs->Compile();

    fs->Source(fsString);
    fs->Compile();

    prog->Attach(*vs);
    prog->Attach(*fs);
    prog->Link();
}

void RenderingEngine::Renderer::DestructProgram()
{
    OpenGL::Context::get_instance().DeleteShader((OpenGL::Shader*) m_VertexShader);
    OpenGL::Context::get_instance().DeleteShader((OpenGL::Shader*) m_FragmentShader);
    OpenGL::Context::get_instance().DeleteProgram((OpenGL::Program*) m_Program);
}
