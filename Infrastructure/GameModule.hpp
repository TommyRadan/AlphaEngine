/**
 * Copyright (c) 2018 Tomislav Radanovic
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

#include <EventEngine/EventEngine.hpp>
#include <EventEngine/Dispatch.hpp>

#define GAME_MODULE() bool ModuleInit()
#define REGISTER_CALLBACK(event, callback) EventEngine::Dispatch::GetInstance()->Register ## event ## Callback(callback);
#define LET_EVERY_MS(time) do { \
        static uint32_t totalTime = 0u;\
        static uint32_t lastPass = 0u;\
        totalTime += deltaTime; \
        if (totalTime < lastPass + time) return; \
        lastPass = totalTime; \
    } while(false)

static bool ModuleInit();

static bool initStatus = ModuleInit();
