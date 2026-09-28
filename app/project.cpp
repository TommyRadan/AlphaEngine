// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <app/project.hpp>

#include <algorithm>
#include <cstdint>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/log.hpp>
#include <core/os/os.hpp>

namespace app
{
    namespace
    {
        using json = nlohmann::json;

        constexpr const char* k_format = "alphaengine.project";
        constexpr std::int64_t k_version = 1;

        // The keys a version 1 project file may hold.
        constexpr const char* k_known_keys[] = {
            "format", "version", "name", "content_root", "startup_scene", "modules", "settings"};

        // The project file @p path names: the path itself, or the project file
        // inside it when it is a directory.
        std::filesystem::path project_file_at(const std::filesystem::path& path)
        {
            std::error_code error;
            if (std::filesystem::is_directory(path, error))
            {
                return path / k_project_file_name;
            }
            return path;
        }

        // The text of the optional string member @p key of @p document: false,
        // with @p error set, when it is present but not a string.
        bool read_optional_string(const json& document, const char* key, std::string& out, std::string& error)
        {
            const auto member = document.find(key);
            if (member == document.end())
            {
                return true;
            }
            if (!member->is_string())
            {
                error = std::string{"\""} + key + "\" is not a string";
                return false;
            }
            out = member->get<std::string>();
            return true;
        }
    } // namespace

    std::optional<project> read_project(const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        const std::filesystem::path file =
            std::filesystem::absolute(project_file_at(path), filesystem_error).lexically_normal();
        const std::string file_text = core::os::path_to_utf8(file);
        auto fail = [&](const std::string& reason) -> std::optional<project>
        {
            error = file_text + ": " + reason;
            return std::nullopt;
        };
        if (filesystem_error)
        {
            return fail(filesystem_error.message());
        }

        std::string text;
        std::string read_error;
        if (!core::os::read_text_file(file, text, &read_error))
        {
            return fail(read_error.empty() ? std::string{"cannot be read"} : read_error);
        }

        const json document = json::parse(text, nullptr, false);
        if (document.is_discarded())
        {
            return fail("not valid JSON");
        }
        if (!document.is_object())
        {
            return fail("not a JSON object");
        }
        const auto format = document.find("format");
        if (format == document.end() || !format->is_string() || format->get<std::string>() != k_format)
        {
            return fail(std::string{"\"format\" is not \""} + k_format + "\"");
        }
        const auto version = document.find("version");
        if (version == document.end() || !version->is_number_integer())
        {
            return fail("\"version\" is missing or not an integer");
        }
        if (version->get<std::int64_t>() > k_version || version->get<std::int64_t>() < 1)
        {
            return fail("version " + std::to_string(version->get<std::int64_t>()) +
                        " is not one this build reads (1 to " + std::to_string(k_version) + ")");
        }

        project result;
        result.file = file;

        std::string reason;
        if (!read_optional_string(document, "name", result.name, reason))
        {
            return fail(reason);
        }
        if (result.name.empty())
        {
            return fail("\"name\" is missing or empty");
        }

        std::string content_root = "content";
        if (!read_optional_string(document, "content_root", content_root, reason) ||
            !read_optional_string(document, "startup_scene", result.startup_scene, reason))
        {
            return fail(reason);
        }
        std::filesystem::path root = core::os::utf8_path(content_root);
        if (root.is_relative())
        {
            root = file.parent_path() / root;
        }
        result.content_root = root.lexically_normal();

        if (const auto modules = document.find("modules"); modules != document.end())
        {
            if (!modules->is_array())
            {
                return fail("\"modules\" is not an array");
            }
            for (const json& entry : *modules)
            {
                if (!entry.is_string() || entry.get<std::string>().empty())
                {
                    return fail("\"modules\" holds an entry that is not a module name");
                }
                std::string name = entry.get<std::string>();
                if (std::find(result.modules.begin(), result.modules.end(), name) != result.modules.end())
                {
                    LOG_WRN("Project %s lists the module '%s' more than once; it is installed once",
                            file_text.c_str(),
                            name.c_str());
                    continue;
                }
                result.modules.push_back(std::move(name));
            }
        }

        if (const auto settings = document.find("settings"); settings != document.end())
        {
            if (!settings->is_object())
            {
                return fail("\"settings\" is not an object");
            }
            result.settings = settings->dump();
        }

        for (const auto& [key, value] : document.items())
        {
            (void)value;
            if (std::find_if(std::begin(k_known_keys),
                             std::end(k_known_keys),
                             [&key](const char* known) { return key == known; }) == std::end(k_known_keys))
            {
                LOG_WRN("Project %s: unknown key \"%s\" ignored", file_text.c_str(), key.c_str());
            }
        }

        if (std::error_code root_error; !std::filesystem::is_directory(result.content_root, root_error))
        {
            LOG_WRN("Project %s: the content root %s is not a directory",
                    file_text.c_str(),
                    core::os::path_to_utf8(result.content_root).c_str());
        }
        return result;
    }
} // namespace app
