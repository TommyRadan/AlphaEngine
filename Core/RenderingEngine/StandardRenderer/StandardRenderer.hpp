#pragma once

#include <Utilities\FileToString.hpp>

#include <Mathematics\Math.hpp>

#include <Utilities\Singleton.hpp>
#include <RenderingEngine\Renderer.hpp>

class StandardRenderer : 
	public Singleton<StandardRenderer>, 
	public Renderer
{
	friend Singleton<StandardRenderer>;
	StandardRenderer(void)
	{
		std::string vertexCode = FileToString(std::string("StandardShader.vs"));
		std::string fragmentCode = FileToString(std::string("StandardShader.fs"));

		m_VertexShader.Source(vertexCode);
		m_VertexShader.Compile();

		m_FragmentShader.Source(fragmentCode);
		m_FragmentShader.Compile();

		m_Program.Attach(m_VertexShader);
		m_Program.Attach(m_FragmentShader);
		m_Program.Link();
	}
};
