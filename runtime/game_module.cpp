// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
