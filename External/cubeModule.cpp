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

#include "API/GameModule.hpp"
#include "API/Log.hpp"
#include "API/Time.hpp"

#include <RenderingEngine/Renderables/Pane.hpp>
#include <RenderingEngine/Renderables/Cube.hpp>

#include <SDL.h>

static RenderingEngine::Pane *pane;
static RenderingEngine::Cube *cube;

float rotation = 0.f;
float rotationSpeed = 3.14f / 2;

static void OnEngineStart()
{
    pane = new RenderingEngine::Pane(glm::vec2{ 0.6f, 0.1f }, Infrastructure::Color{ 41, 128, 185, 255 });
    pane->transform.SetPosition(glm::vec3 { -0.3f, 0.95f, 0.0f });
    cube = new RenderingEngine::Cube();

    pane->Upload();
    cube->Upload();
}

static void OnFrame()
{
    rotation += rotationSpeed * (GetDeltaTime() / 1000);
    cube->transform.SetRotation(glm::vec3{ 0.f, 0.f, rotation });
}

static void OnRenderScene()
{
    cube->Render();
}

static void OnRenderUi()
{
    pane->Render();
}

GAME_MODULE()
{
    struct GameModuleInfo info;
    info.onEngineStart = OnEngineStart;
    info.onRenderScene = OnRenderScene;
    info.onFrame = OnFrame;
    info.onRenderUi = OnRenderUi;
    RegisterGameModule(info);
    return true;
}
