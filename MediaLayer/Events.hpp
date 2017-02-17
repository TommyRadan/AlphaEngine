#pragma once

#include <Control/Settings.hpp>

namespace MediaLayer
{
    enum KeyBinding {
        KEY_W = 119,
        KEY_A = 97,
        KEY_S = 115,
        KEY_D = 100,
        KEY_SPACE = 32,
        KEY_SHIFT = 1073742049,
        KEY_CTRL = 1073741881,
        KEY_ENTER = 13,
        KEY_ESCAPE = 27
    };

    class Events
    {
        Events() :
            m_IsUserQuit { false },
            m_GameSettings { Settings::GetInstance() }
        {}

    public:
        static Events* GetInstance(void)
        {
            static Events* instance = nullptr;
            if(instance == nullptr) {
                instance = new Events();
            }
            return instance;
        }

        void Process(void);
        void Update(void);

        bool IsQuitRequested(void) const {
            return m_IsUserQuit;
        }

    private:
        bool m_IsUserQuit;

        Settings* m_GameSettings;
    };
}
