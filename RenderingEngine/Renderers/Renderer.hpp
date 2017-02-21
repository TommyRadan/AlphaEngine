#pragma once

#include <Mathematics/glm.hpp>
#include <string>

#include <RenderingEngine/Material.hpp>

struct Renderer
{
    void StartRenderer(void);
    void StopRenderer(void);

	virtual void SetupCamera(void) = 0;
	virtual void SetupMaterial(const Material& material) = 0;

    void UploadTextureReference(const std::string& textureName, const int position);
    void UploadCoefficient(const std::string& coefficientName, const float coefficient);

    void UploadMatrix3(const std::string& mat3Name, const glm::mat3& matrix);
    void UploadMatrix4(const std::string& mat4Name, const glm::mat4& matrix);
    void UploadVector2(const std::string& vec2Name, const glm::vec2& vector);
    void UploadVector3(const std::string& vec3Name, const glm::vec3& vector);
    void UploadVector4(const std::string& vec4Name, const glm::vec4& vector);

protected:
	Renderer(void) :
		m_IsInit{ false }
	{}

	virtual void Init(void) = 0;
	virtual void Quit(void) = 0;
	bool m_IsInit;

	void* m_VertexShader;
	void* m_FragmentShader;
	void* m_Program;

private:
	const bool CheckForBadInit(void);
};
