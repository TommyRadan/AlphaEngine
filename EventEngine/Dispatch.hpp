/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
        void RegisterOnFrameCallback(std::function<void(void)> callback);
        void RegisterOnKeyDownCallback(std::function<void(KeyCode)> callback);
        void RegisterOnKeyUpCallback(std::function<void(KeyCode)> callback);
        void RegisterKeyPressedCallback(std::function<void(KeyCode)> callback);
        void RegisterOnMouseMoveCallback(std::function<void(int32_t, int32_t)> callback);

		void DispatchOnGameStartCallback();
		void DispatchOnGameEndCallback();
        void DispatchOnFrameCallback();
        void DispatchOnKeyDownCallback(KeyCode keyCode);
        void DispatchOnKeyUpCallback(KeyCode keyCode);
        void DispatchKeyPressedCallback(KeyCode keyCode);
        void DispatchOnMouseMoveCallback(int32_t deltaX, int32_t deltaY);

        bool IsKeyPressed(KeyCode keyCode);

    private:
		std::vector<std::function<void(void)>> m_OnGameStartCallbacks;
		std::vector<std::function<void(void)>> m_OnGameEndCallbacks;
        std::vector<std::function<void(void)>> m_OnFrameCallbacks;
        std::vector<std::function<void(KeyCode)>> m_OnKeyDownCallbacks;
        std::vector<std::function<void(KeyCode)>> m_OnKeyUpCallbacks;
        std::vector<std::function<void(KeyCode)>> m_KeyPressedCallbacks;
        std::vector<KeyCode> m_PressedKeys;
        std::vector<std::function<void(int32_t, int32_t)>> m_OnMouseMoveCallbacks;
    };
}
