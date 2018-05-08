#pragma once

#include <functional>
#include <vector>

namespace EventEngine
{
    enum class KeyCode
    {
        W = 119,
        A = 97,
        S = 115,
        D = 100,
        SPACE = 32,
        SHIFT = 1073742049,
        CTRL = 1073741881,
        ENTER = 13,
        ESCAPE = 27,

        MOUSE_LEFT = 141881,
        MOUSE_RIGHT = 141882,
        MOUSE_MIDDLE = 141883
    };

    class Dispatch
    {
		Dispatch() = default;

    public:
        static Dispatch* GetInstance();

        void HandleEvents();

		void RegisterOnGameStartCallback(std::function<void(void)> callback);
		void RegisterOnGameEndCallback(std::function<void(void)> callback);
        void RegisterOnFrameCallback(std::function<void(uint32_t)> callback);
        void RegisterOnKeyDownCallback(std::function<void(KeyCode)> callback);
        void RegisterOnKeyUpCallback(std::function<void(KeyCode)> callback);
        void RegisterKeyPressedCallback(std::function<void(KeyCode, uint32_t)> callback);
        void RegisterOnMouseMoveCallback(std::function<void(int32_t, int32_t)> callback);

		void DispatchOnGameStartCallback();
		void DispatchOnGameEndCallback();
        void DispatchOnFrameCallback(uint32_t deltaTime);
        void DispatchOnKeyDownCallback(KeyCode keyCode);
        void DispatchOnKeyUpCallback(KeyCode keyCode);
        void DispatchKeyPressedCallback(KeyCode keyCode, uint32_t deltaTime);
        void DispatchOnMouseMoveCallback(int32_t deltaX, int32_t deltaY);

        bool IsKeyPressed(KeyCode keyCode);

    private:
        void HandleFrame();

		std::vector<std::function<void(void)>> m_OnGameStartCallbacks;
		std::vector<std::function<void(void)>> m_OnGameEndCallbacks;
        std::vector<std::function<void(uint32_t)>> m_OnFrameCallbacks;
        std::vector<std::function<void(KeyCode)>> m_OnKeyDownCallbacks;
        std::vector<std::function<void(KeyCode)>> m_OnKeyUpCallbacks;
        std::vector<std::function<void(KeyCode, uint32_t)>> m_KeyPressedCallbacks;
        std::vector<KeyCode> m_PressedKeys;
        std::vector<std::function<void(int32_t, int32_t)>> m_OnMouseMoveCallbacks;
    };
}
