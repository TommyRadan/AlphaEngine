#pragma once

#include <Mathematics/glm.hpp>
#include <string>

#include <RenderingEngine/RenderOptions.hpp>
#include <Infrastructure/Subsystem.hpp>

namespace RenderingEngine
{
    struct Renderer
    {
        void StartRenderer();
        void StopRenderer();

        void SetupCamera();
        void SetupOptions(const RenderOptions& options);

        void UploadTextureReference(const std::string& textureName, int position);
        void UploadCoefficient(const std::string& coefficientName, float coefficient);
        void UploadMatrix3(const std::string& mat3Name, const glm::mat3& matrix);
        void UploadMatrix4(const std::string& mat4Name, const glm::mat4& matrix);
        void UploadVector2(const std::string& vec2Name, const glm::vec2& vector);
        void UploadVector3(const std::string& vec3Name, const glm::vec3& vector);
        void UploadVector4(const std::string& vec4Name, const glm::vec4& vector);

        static Renderer* GetCurrentRenderer();

    protected:
        Renderer() = default;

        void ConstructProgram(const std::string& vsString, const std::string& fsString);
        void DestructProgram();

        void* m_VertexShader;
        void* m_FragmentShader;
        void* m_Program;

        static Renderer* m_CurrentRenderer;
    };
}
