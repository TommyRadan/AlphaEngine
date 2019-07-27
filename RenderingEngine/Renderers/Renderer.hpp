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

#include <RenderingEngine/Renderers/RenderOptions.hpp>

namespace RenderingEngine
{
    struct Renderer
    {
        void StartRenderer();
        void StopRenderer();

        static Renderer *GetCurrentRenderer();

        void SetupCamera();
        void SetupOptions(const RenderOptions& options);

        void UploadTextureReference(const std::string& textureName, int position);
        void UploadCoefficient(const std::string& coefficientName, float coefficient);
        void UploadMatrix3(const std::string& mat3Name, const glm::mat3& matrix);
        void UploadMatrix4(const std::string& mat4Name, const glm::mat4& matrix);
        void UploadVector2(const std::string& vec2Name, const glm::vec2& vector);
        void UploadVector3(const std::string& vec3Name, const glm::vec3& vector);
        void UploadVector4(const std::string& vec4Name, const glm::vec4& vector);

    protected:
        Renderer() = default;

        void ConstructProgram(const std::string& vsString, const std::string& fsString);
        void DestructProgram();

        void* m_VertexShader;
        void* m_FragmentShader;
        void* m_Program;

        static Renderer *m_CurrentRenderer;
    };
}
