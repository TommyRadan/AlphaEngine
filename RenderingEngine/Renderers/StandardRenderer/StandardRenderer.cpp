#include "StandardRenderer.hpp"
#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <RenderingEngine/Camera.hpp>

void StandardRenderer::Init(void)
{
    if (m_IsInit) {
        throw Exception("Called StandardRenderer::Init twice");
    }

    m_VertexShader = new OpenGL::Shader(OpenGL::ShaderType::Vertex);
    m_FragmentShader = new OpenGL::Shader(OpenGL::ShaderType::Fragment);
    m_Program = new OpenGL::Program();

    std::string vertexCode = FileToString("StandardShader.vs");
    std::string fragmentCode = FileToString("StandardShader.fs");

    OpenGL::Shader* vs = (OpenGL::Shader*) m_VertexShader;
    OpenGL::Shader* fs = (OpenGL::Shader*) m_FragmentShader;
    OpenGL::Program* prog = (OpenGL::Program*) m_Program;

    vs->Source(vertexCode);
    vs->Compile();

    fs->Source(fragmentCode);
    fs->Compile();

    prog->Attach(*vs);
    prog->Attach(*fs);
    prog->Link();

    m_IsInit = true;
}

void StandardRenderer::Quit(void)
{
    if (!m_IsInit) {
        throw Exception("Called StandardRenderer::Quit before StandardRenderer::Init");
    }

    delete static_cast<OpenGL::Shader*>(m_VertexShader);
    delete static_cast<OpenGL::Shader*>(m_FragmentShader);
    delete static_cast<OpenGL::Program*>(m_Program);

    m_IsInit = false;
}

void StandardRenderer::SetupCamera(void)
{
    this->UploadMatrix4("viewMatrix", Camera::GetInstance()->GetViewMatrix());
    this->UploadMatrix4("projectionMatrix", Camera::GetInstance()->GetProjectionMatrix());
}

void StandardRenderer::SetupMaterial(const Material& material)
{
    for (auto& element : material.Coefficients) {
        this->UploadCoefficient(element.first, element.second);
    }

    for (auto& element : material.Colors) {
        auto vector = glm::vec4(
                element.second.R / 255.0f,
                element.second.G / 255.0f,
                element.second.B / 255.0f,
                element.second.A / 255.0f
        );

        this->UploadVector4(element.first, vector);
    }

    uint8_t uploadedTextureReferences = 0u;
    for (auto& element : material.Textures) {
        this->UploadTextureReference(element.first, uploadedTextureReferences);
        OpenGL::Context::GetInstance()->BindTexture(
                *static_cast<OpenGL::Texture*>(element.second),
                uploadedTextureReferences
        );
        uploadedTextureReferences++;
    }
}
