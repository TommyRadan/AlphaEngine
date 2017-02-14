#include "Events.hpp"
#include <iostream>

void MediaLayer::Events::Process()
{
    while (SDL_PollEvent(&m_Events)) {
        switch (m_Events.type) {
            case SDL_MOUSEMOTION: {
                float sensitivity = m_GameSettings->GetMouseSensitivity();
                int mouseMoveX = m_Events.motion.xrel;
                int mouseMoveY = m_Events.motion.yrel;

                break;
            }
            case SDL_MOUSEWHEEL:
                std::cout << "Mouse wheel (" << m_Events.wheel.y << ")" << std::endl;
                break;
            case SDL_MOUSEBUTTONDOWN:
                std::cout << "Mouse button down (" << int{ m_Events.button.button } << ")" << std::endl;
                break;
            case SDL_MOUSEBUTTONUP:
                std::cout << "Mouse button up (" << int{ m_Events.button.button } << ")" << std::endl;
                break;
            case SDL_KEYDOWN: {
                switch (m_Events.key.keysym.sym) {
                    case KeyBinding::KEY_ESCAPE: {
                        m_IsUserQuit = true;
                        break;
                    }
                    default: break;
                }
                break;
            }
            case SDL_KEYUP: {
                switch (m_Events.key.keysym.sym) {}
                break;
            }
            default: break;
        }
    }
}

void MediaLayer::Events::Update(Uint32 deltaTime)
{
    (void) deltaTime;
}
