#include"Display.hpp"
#include "SDL\SDL.h"

Display::Display(void)
	: m_Settings{ Settings::GetInstance() }
{
	Uint32 window_flags = SDL_WINDOW_OPENGL;

	switch (m_Settings->GetWindowType()) {
	case WinType::WIN_TYPE_BORDERLESS:
		window_flags |= SDL_WINDOW_BORDERLESS;
		break;
	case WinType::WIN_TYPE_FULLSCREEN:
		window_flags |= SDL_WINDOW_FULLSCREEN;
		break;
	}

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, m_Settings->IsDoubleBuffered() ? 1 : 0);

	m_Window = SDL_CreateWindow(
		m_Settings->GetWindowName(),
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		m_Settings->GetWindowWidth(),
		m_Settings->GetWindowHeight(),
		window_flags
	);

	if (m_Window == nullptr) {
		throw Exception(SDL_GetError());
	}

	m_GlContext = SDL_GL_CreateContext((SDL_Window*)m_Window);
	if(m_GlContext == nullptr) {
		throw Exception(SDL_GetError());
	}
	return;
}

Display::~Display(void)
{
	SDL_GL_DeleteContext(m_GlContext);

	SDL_DestroyWindow((SDL_Window*)m_Window);
	m_Window = nullptr;

	m_Settings = nullptr;
	return;
}

void Display::SwapBuffers(void)
{
	SDL_GL_SwapWindow((SDL_Window*)m_Window);
}
