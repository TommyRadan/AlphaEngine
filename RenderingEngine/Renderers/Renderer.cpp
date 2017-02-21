#include "Renderer.hpp"
#include <RenderingEngine/OpenGL/OpenGL.hpp>

const bool Renderer::CheckForBadInit(void)
{
    return !(!m_VertexShader || !m_FragmentShader || !m_Program || !m_IsInit);
}

void Renderer::StartRenderer(void)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::StartRenderer called before proper initialization");
    }

    static_cast<OpenGL::Program*>(m_Program)->Start();
}

void Renderer::StopRenderer(void)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::StopRenderer called before proper initialization");
    }

    static_cast<OpenGL::Program*>(m_Program)->Stop();
}

void Renderer::UploadTextureReference(const std::string& textureName, const int position)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::UploadTextureReference called before proper initialization");
    }

    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(textureName);
    if (uniform == -1) {
        throw Exception("Could not find uniform " + textureName + " in StandardRenderer");
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, position);
}

void Renderer::UploadCoefficient(const std::string& coefficientName, const float coefficient)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::UploadCoefficient called before proper initialization");
    }

    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(coefficientName);
    if (uniform == -1) {
        throw Exception("Could not find uniform " + coefficientName + " in StandardRenderer");
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, coefficient);
}

void Renderer::UploadMatrix3(const std::string& mat3Name, const glm::mat3& matrix)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::UploadMatrix3 called before proper initialization");
    }

    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(mat3Name);
    if (uniform == -1) {
        throw Exception("Could not find uniform " + mat3Name + " in StandardRenderer");
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, matrix);
}

void Renderer::UploadMatrix4(const std::string& mat4Name, const glm::mat4& matrix)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::UploadMatrix4 called before proper initialization");
    }

    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(mat4Name);
    if (uniform == -1) {
        throw Exception("Could not find uniform " + mat4Name + " in StandardRenderer");
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, matrix);
}

void Renderer::UploadVector2(const std::string& vec2Name, const glm::vec2& vector)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::UploadVector2 called before proper initialization");
    }

    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec2Name);
    if (uniform == -1) {
        throw Exception("Could not find uniform " + vec2Name + " in StandardRenderer");
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void Renderer::UploadVector3(const std::string& vec3Name, const glm::vec3& vector)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::UploadVector3 called before proper initialization");
    }

    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec3Name);
    if (uniform == -1) {
        throw Exception("Could not find uniform " + vec3Name + " in StandardRenderer");
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void Renderer::UploadVector4(const std::string& vec4Name, const glm::vec4& vector)
{
    if (!CheckForBadInit()) {
        throw Exception("Renderer::UploadVector4 called before proper initialization");
    }

    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec4Name);
    if (uniform == -1) {
        throw Exception("Could not find uniform " + vec4Name + " in StandardRenderer");
    }
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}
