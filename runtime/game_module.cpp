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
        std::string_view name;
        runtime::game_module_bootstrap bootstrap;
    };

    // Filled during static initialisation, so a function-local static: it is
    // constructed on first use, whichever module's initialiser runs first.
    std::vector<registration>& registrations()
    {
        static std::vector<registration> storage;
        return storage;
    }

    // "external/fog_demo_module.cpp" -> "fog_demo". A view into the source
    // path, which __FILE__ keeps alive for the whole run.
    std::string_view module_name(const char* source)
    {
        constexpr std::string_view suffix = "_module";
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
        if (name.size() > suffix.size() && name.ends_with(suffix))
        {
            name.remove_suffix(suffix.size());
        }
        return name;
    }

    const registration* find(std::string_view name)
    {
        for (const registration& entry : registrations())
        {
            if (entry.name == name)
            {
                return &entry;
            }
        }
        return nullptr;
    }
} // namespace

bool runtime::register_game_module(const char* source, game_module_bootstrap bootstrap)
{
    const std::string_view name = module_name(source);
    if (bootstrap != nullptr && find(name) == nullptr)
    {
        registrations().push_back(registration{name, bootstrap});
    }
    return true;
}

std::vector<std::string> runtime::game_module_names()
{
    std::vector<std::string> names;
    names.reserve(registrations().size());
    for (const registration& entry : registrations())
    {
        names.emplace_back(entry.name);
    }
    return names;
}

bool runtime::has_game_module(std::string_view name)
{
    return find(name) != nullptr;
}

bool runtime::install_game_module(std::string_view name, scene& scene)
{
    // Copied out, so the entry stays valid even if the bootstrap were to
    // register another module.
    const registration* found = find(name);
    if (found == nullptr)
    {
        return false;
    }
    const registration entry = *found;
    LOG_INF("Installing game module: %.*s", static_cast<int>(entry.name.size()), entry.name.data());
    entry.bootstrap(scene);
    return true;
}
