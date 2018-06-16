#include <RenderingEngine/Renderer.hpp>
#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <RenderingEngine/Camera.hpp>
#include <RenderingEngine/RenderingEngine.hpp>

void RenderingEngine::Renderer::StartRenderer()
{
    RenderingEngine::Context::GetInstance()->m_CurrentRenderer = this;
    static_cast<OpenGL::Program*>(m_Program)->Start();
}

void RenderingEngine::Renderer::StopRenderer()
{
    RenderingEngine::Context::GetInstance()->m_CurrentRenderer = nullptr;
    static_cast<OpenGL::Program*>(m_Program)->Stop();
}

void RenderingEngine::Renderer::SetupCamera()
{
    this->UploadMatrix4("viewMatrix", RenderingEngine::Camera::GetInstance()->GetViewMatrix());
    this->UploadMatrix4("projectionMatrix", RenderingEngine::Camera::GetInstance()->GetProjectionMatrix());
}

void RenderingEngine::Renderer::SetupOptions(const RenderOptions& options)
{
    for (auto& element : options.Coefficients)
    {
        this->UploadCoefficient(element.first, element.second);
    }

    for (auto& element : options.Colors)
    {
        auto vector = glm::vec4(
                (float)element.second.r / 255.0f,
                (float)element.second.g / 255.0f,
                (float)element.second.b / 255.0f,
                (float)element.second.a / 255.0f
        );

        this->UploadVector4(element.first, vector);
    }

    uint8_t uploadedTextureReferences = 0u;
    for (auto& element : options.Textures)
    {
        this->UploadTextureReference(element.first, uploadedTextureReferences);
        OpenGL::Context::GetInstance()->BindTexture(*element.second, uploadedTextureReferences);
        uploadedTextureReferences++;
    }
}

void RenderingEngine::Renderer::UploadTextureReference(const std::string& textureName, const int position)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(textureName);
    if (uniform == -1) return;
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, position);
}

void RenderingEngine::Renderer::UploadCoefficient(const std::string& coefficientName, const float coefficient)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(coefficientName);
    if (uniform == -1) return;
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, coefficient);
}

void RenderingEngine::Renderer::UploadMatrix3(const std::string& mat3Name, const glm::mat3& matrix)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(mat3Name);
    if (uniform == -1) return;
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, matrix);
}

void RenderingEngine::Renderer::UploadMatrix4(const std::string& mat4Name, const glm::mat4& matrix)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(mat4Name);
    if (uniform == -1) return;
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, matrix);
}

void RenderingEngine::Renderer::UploadVector2(const std::string& vec2Name, const glm::vec2& vector)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec2Name);
    if (uniform == -1) return;
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void RenderingEngine::Renderer::UploadVector3(const std::string& vec3Name, const glm::vec3& vector)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec3Name);
    if (uniform == -1) return;
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void RenderingEngine::Renderer::UploadVector4(const std::string& vec4Name, const glm::vec4& vector)
{
    OpenGL::Uniform uniform = static_cast<OpenGL::Program*>(m_Program)->GetUniform(vec4Name);
    if (uniform == -1) return;
    static_cast<OpenGL::Program*>(m_Program)->SetUniform(uniform, vector);
}

void RenderingEngine::Renderer::ConstructProgram(const std::string& vsString, const std::string& fsString)
{
    m_VertexShader = new OpenGL::Shader(OpenGL::ShaderType::Vertex);
    m_FragmentShader = new OpenGL::Shader(OpenGL::ShaderType::Fragment);
    m_Program = new OpenGL::Program();

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
    delete static_cast<OpenGL::Shader*>(m_VertexShader);
    delete static_cast<OpenGL::Shader*>(m_FragmentShader);
    delete static_cast<OpenGL::Program*>(m_Program);
}
