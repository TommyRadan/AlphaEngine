#pragma once

// Utilities
#include <Utilities/Exception.hpp>
#include <Utilities/FileToString.hpp>

// Base class
#include <RenderingEngine/Renderer.hpp>

class StandardRenderer :
	public Renderer
{
	StandardRenderer(void) :
		Renderer()
	{}

public:
	static StandardRenderer* GetInstance(void)
	{
		static StandardRenderer* instance = nullptr;
		if(instance == nullptr) {
			instance = new StandardRenderer();
		}
		return instance;
	}

	void Init(void) override
	{
		if (m_IsInit) {
			throw Exception("Called StandardRenderer::Init twice");
		}

		m_VertexShader = new OpenGL::Shader(OpenGL::ShaderType::Vertex);
		m_FragmentShader = new OpenGL::Shader(OpenGL::ShaderType::Fragment);
		m_Program = new OpenGL::Program();

		std::string vertexCode = FileToString("StandardShader.vs");
		std::string fragmentCode = FileToString("StandardShader.fs");

		m_VertexShader->Source(vertexCode);
		m_VertexShader->Compile();

		m_FragmentShader->Source(fragmentCode);
		m_FragmentShader->Compile();

		m_Program->Attach(*m_VertexShader);
		m_Program->Attach(*m_FragmentShader);
		m_Program->Link();

		m_IsInit = true;
	}

	void Quit(void) override
	{
		if (!m_IsInit) {
			throw Exception("Called StandardRenderer::Quit before StandardRenderer::Init");
		}

		delete m_VertexShader;
		delete m_FragmentShader;
		delete m_Program;

		m_IsInit = false;
	}
};
