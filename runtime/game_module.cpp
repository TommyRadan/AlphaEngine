/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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

#include <runtime/game_module.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

#include <core/log.hpp>

namespace
{
    struct registration
    {
        const char* source;
        runtime::game_module_bootstrap bootstrap;
    };

    // Filled during static initialisation, so a function-local static: it is
    // constructed on first use, whichever module's initialiser runs first.
    std::vector<registration>& registrations()
    {
        static std::vector<registration> storage;
        return storage;
    }

    // "external/camera_module.cpp" -> "camera_module", for the log.
    std::string_view module_name(const char* source)
    {
        std::string_view name{source != nullptr ? source : "?"};
        const std::size_t slash = name.find_last_of("/\\");
        if (slash != std::string_view::npos)
        {
            name.remove_prefix(slash + 1);
        }
        const std::size_t dot = name.rfind('.');
        if (dot != std::string_view::npos && dot != 0)
        {
            name.remove_suffix(name.size() - dot);
        }
        return name;
    }
} // namespace

bool runtime::register_game_module(const char* source, game_module_bootstrap bootstrap)
{
    if (bootstrap != nullptr)
    {
        registrations().push_back(registration{source, bootstrap});
    }
    return true;
}

void runtime::install_game_modules(scene& scene)
{
    // Indexed, and copied out, so the loop stays valid even if a bootstrap
    // were to register another module.
    for (std::size_t index = 0; index < registrations().size(); ++index)
    {
        const registration entry = registrations()[index];
        const std::string_view name = module_name(entry.source);
        LOG_INF("Installing game module: %.*s", static_cast<int>(name.size()), name.data());
        entry.bootstrap(scene);
    }
}
