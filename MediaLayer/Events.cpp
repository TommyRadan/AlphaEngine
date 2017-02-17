#include "Events.hpp"
#include <SDL2/SDL.h>

void MediaLayer::Events::Process()
{
    SDL_Event events;
    while (SDL_PollEvent(&events)) {
        switch (events.type) {
            case SDL_MOUSEMOTION:
                // Moved mouse
                break;
            case SDL_MOUSEWHEEL:
                // Mouse wheel
                break;
            case SDL_MOUSEBUTTONDOWN:
                // Mouse button down
                break;
            case SDL_MOUSEBUTTONUP:
                // Mouse button up
                break;
            case SDL_KEYDOWN: {
                // Keyboard key down
                switch (events.key.keysym.sym) {
                    case KeyBinding::KEY_ESCAPE: {
                        m_IsUserQuit = true;
                        break;
                    }
                }
                break;
            }
            case SDL_KEYUP:
                // Keyboard key up
                break;
            case SDL_QUIT:
                m_IsUserQuit = true;
                break;
            default: break;
        }
    }
}

void MediaLayer::Events::Update()
{}
