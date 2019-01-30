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

#include <SceneGraph/SceneGraph.hpp>

#include <Infrastructure/Log.hpp>

#include <json.hpp>
#include <fstream>
#include <string>

using namespace nlohmann;

SceneGraph::Context* SceneGraph::Context::GetInstance()
{
    static Context* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Context;
    }

    return instance;
}

void SceneGraph::Context::Init()
{
    LOG_INFO("Init Scene Graph");
}

void SceneGraph::Context::Quit()
{
    LOG_INFO("Quit Scene Graph");
}

void SceneGraph::Context::LoadScene(const std::string& filename)
{
    std::ifstream file(filename);

    std::string fileString;
    json fileJSON;
    try
    {
        file >> fileJSON;

        if (fileJSON["asset"]["version"] != "2.0")
        {
            LOG_WARN("Loaded scene is version %s, version 2.0 is supported",
                    fileJSON["asset"]["version"].dump().c_str());
        }
    }
    catch(...)
    {
        LOG_ERROR("Could not load scene (%s)", filename.c_str());
        return;
    }

    LOG_INFO("Loaded scene (%s)", filename.c_str());
}
