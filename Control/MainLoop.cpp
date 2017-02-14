#include <MediaLayer/MediaLayer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>

#include <Utilities/Exception.hpp>

int main(int argc, char* argv[])
{
	(void) argc;
	(void) argv;

	try {
		MediaLayer::Context::GetInstance()->Init();
		RenderingEngine::GetInstance()->Init();
	} catch (Exception e) {
		MediaLayer::Context::GetInstance()->ShowDialog("Initialization error!", e.what());
		return 1;
	}

	OpenGL::Shader vert(OpenGL::ShaderType::Vertex);
	vert.Source("#version 150\nin vec2 position; void main() { gl_Position = vec4( position, 0.0, 1.0 ); }");
	vert.Compile();

	OpenGL::Shader frag(OpenGL::ShaderType::Fragment );
	frag.Source("#version 150\nout vec4 outColor; void main() { outColor = vec4( 1.0, 0.0, 0.0, 1.0 ); }");
	frag.Compile();

	OpenGL::Program program;
	program.Attach(vert);
	program.Attach(frag);
	program.Link();

	float vertices[] = {
			-0.5f,  0.5f,
			0.5f,  0.5f,
			0.5f, -0.5f
	};
	OpenGL::VertexBuffer vbo;
	vbo.Data(vertices, sizeof(vertices), OpenGL::BufferUsage::StaticDraw);

	OpenGL::VertexArray vao;
	vao.BindAttribute(program.GetAttribute("position"), vbo, OpenGL::Type::Float, 2, 0, 0);

	try {
		for (;;)
		{
			MediaLayer::Events::GetInstance()->Process();
            if(MediaLayer::Events::GetInstance()->IsQuitRequested()) break;

			OpenGL::Context::GetInstance()->ClearColor(Color(0, 0, 0, 255));
			OpenGL::Context::GetInstance()->Clear();

			OpenGL::Context::GetInstance()->DrawArrays(vao, OpenGL::Primitive::Triangles, 0, 3);

            MediaLayer::Window::GetInstance()->SwapBuffers();
		}
	} catch (Exception e) {
		MediaLayer::Context::GetInstance()->ShowDialog("Runtime error!", e.what());
		return 1;
	}

	RenderingEngine::GetInstance()->Quit();
	MediaLayer::Context::GetInstance()->Quit();
    return 0;
}
