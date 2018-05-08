#include <RenderingEngine/Renderers/BasicRenderer.hpp>
#include <RenderingEngine/Camera.hpp>

static std::string vertexShader = R"vs(
        #version 330

        layout(location=0) in vec3 position;

        uniform mat4 modelMatrix;
        uniform mat4 viewMatrix;
        uniform mat4 projectionMatrix;

        void main()
        {
            mat4 MVP = projectionMatrix * viewMatrix * modelMatrix;
            gl_Position = MVP * vec4(position, 1.0);
        }
)vs";

static std::string fragmentShader = R"fs(
        #version 330

        out vec4 fragColor;

        void main()
        {
            fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
)fs";

RenderingEngine::Renderers::BasicRenderer::BasicRenderer()
{
    ConstructProgram(vertexShader, fragmentShader);
}

RenderingEngine::Renderers::BasicRenderer* RenderingEngine::Renderers::BasicRenderer::GetInstance()
{
    static BasicRenderer* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new BasicRenderer;
    }

    return instance;
}

RenderingEngine::Renderers::BasicRenderer::~BasicRenderer()
{
    DestructProgram();
}
