// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file project.hpp
 * @brief A project: the file that says which game the engine runs — its
 *        name, where its content is, the scene it starts in, its default
 *        settings and the native game modules it installs.
 *
 * A project file is a JSON object:
 * @code
 * {
 *     "format": "alphaengine.project",
 *     "version": 1,
 *     "name": "Fog",
 *     "content_root": "../../content",
 *     "startup_scene": "scenes/start.scene.json",
 *     "modules": ["camera", "exit", "fog_demo"],
 *     "settings": { "window": { "title": "Fog" } }
 * }
 * @endcode
 *
 * - @c format and @c version are required: the file says it is a project,
 *   in a version this build reads.
 * - @c name is required and names the game in the log.
 * - @c content_root is the directory the engine mounts as the root of the
 *   virtual filesystem, relative to the directory the project file is in;
 *   @c content beside the file when left out.
 * - @c startup_scene is a scene file (runtime/scene_serializer.hpp), a path
 *   under the content root, loaded once the engine is up; none when left out
 *   or empty.
 * - @c modules names the native game modules to install, in the order their
 *   bootstraps run (see runtime/game_module.hpp); none when left out.
 * - @c settings holds default settings in the shape of @c settings.json.
 *   They sit between the compiled defaults and the user's settings file, so
 *   the settings file, the environment and the command line still override
 *   them (see @c core::load_settings).
 *
 * Any other key is warned about and ignored.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace app
{
    /** @brief The file name a project directory holds its project file under. */
    inline constexpr const char* k_project_file_name = "project.json";

    /** @brief A project as read from its file, with its paths resolved. See the file notes for the fields. */
    struct project
    {
        /** @brief The game's name. */
        std::string name;

        /** @brief The project file this was read from; empty for a project built in code. */
        std::filesystem::path file;

        /** @brief The content root, absolute (for a project read from a file). */
        std::filesystem::path content_root;

        /** @brief The scene file loaded at start-up, a path under the content root; empty for none. */
        std::string startup_scene;

        /** @brief The native game modules to install, by name, in the order their bootstraps run. */
        std::vector<std::string> modules;

        /** @brief The project's default settings as a @c settings.json document; empty for none. */
        std::string settings;
    };

    /**
     * @brief Reads the project at @p path: a project file, or a directory
     *        holding one named @ref k_project_file_name. A relative path is
     *        taken from the working directory.
     *
     * Paths in the file are resolved against the file's own directory, so
     * the project reads the same from any working directory. A content root
     * that is not a directory is warned about but kept.
     *
     * @return The project, or @c std::nullopt with the reason in @p error when
     *         the file cannot be read or is not a project this build reads.
     */
    std::optional<project> read_project(const std::filesystem::path& path, std::string& error);
} // namespace app
