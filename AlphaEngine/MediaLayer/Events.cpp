#include "Events.hpp"

void Events::Process(void)
{
	while (SDL_PollEvent(&m_Events)) {
		switch (m_Events.type) {
			case SDL_MOUSEMOTION: {
				int mouseMoveX = m_Events.motion.xrel;
				int mouseMoveY = m_Events.motion.yrel;
				int mouseMoveZ = m_Events.wheel.y;
				EventCallBacks::GetInstance()->OnMouseMove(mouseMoveX, mouseMoveY);
				break;
			}
			case SDL_MOUSEWHEEL: {
				m_Events.wheel.y;
				break;
			}
			case SDL_MOUSEBUTTONDOWN: {
				m_Events.button.button;
				break;
			}
			case SDL_MOUSEBUTTONUP: {
				m_Events.button.button;
				break;
			}
			case SDL_KEYDOWN: {
				EventCallBacks::GetInstance()->OnKeyDown(m_Events.key.keysym.sym);
				break;
			}
			case SDL_KEYUP: {
				EventCallBacks::GetInstance()->OnKeyUp(m_Events.key.keysym.sym);
				break;
			}
			case SDL_QUIT: {
				m_IsUserQuit = true;
				break; 
			}
		}
	}
}

void Events::Update(const Uint32 deltaTime)
{
	EventCallBacks::GetInstance()->OnFrame(deltaTime);
}
