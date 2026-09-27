// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/shader_hot_reload.hpp>

#include <string>

#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader.hpp>

#if defined(_DEBUG)
#include <algorithm>
#include <cassert>
#include <exception>
#include <map>
#include <unordered_map>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/gpu/shader_library.hpp>
#endif

namespace rendering_engine::gpu
{
#if defined(_DEBUG)
    namespace
    {
        // The reload create_library_shader_module registers with; see
        // shader_hot_reload::active.
        shader_hot_reload* installed_reload = nullptr;

        const char* stage_label(shader_stage stage)
        {
            switch (stage)
            {
            case shader_stage::vertex:
                return "vertex";
            case shader_stage::fragment:
                return "fragment";
            case shader_stage::geometry:
                return "geometry";
            case shader_stage::tessellation_control:
                return "tessellation control";
            case shader_stage::tessellation_evaluation:
                return "tessellation evaluation";
            case shader_stage::compute:
                return "compute";
            }
            return "unknown";
        }

        // Identity of one compile: modules created from the same path,
        // defines and stage (the fullscreen vertex stage every post pass
        // creates, one keyword set of a template shared by two instances)
        // recompile once per reload.
        std::string compile_identity(const shader_variant& variant, shader_stage stage)
        {
            std::string identity = std::to_string(static_cast<int>(stage));
            identity += '|';
            identity += variant.path;
            for (const auto& [name, value] : variant.defines)
            {
                identity += '|';
                identity += name;
                identity += '=';
                identity += value;
            }
            return identity;
        }

        std::string join(const std::set<std::string>& paths)
        {
            std::string joined;
            for (const std::string& path : paths)
            {
                if (!joined.empty())
                {
                    joined += ", ";
                }
                joined += path;
            }
            return joined;
        }

        // The first line of an exception message, without a trailing
        // colon: the compiler's thrown message starts with
        // "shader '<name>' (<stage>) failed to parse:" and carries
        // glslang's full log below it, which the compiler has already
        // logged.
        std::string first_line(const char* message)
        {
            const std::string text{message};
            std::string line = text.substr(0, text.find('\n'));
            if (!line.empty() && line.back() == ':')
            {
                line.pop_back();
            }
            return line;
        }
    } // namespace
#endif

    shader_module create_library_shader_module(device& device, const shader_variant& variant, shader_stage stage)
    {
        // The backend define rides along with the variant's own, so a
        // hot-reload recompile of the tracked variant keeps it.
        shader_variant target = variant;
        if (device.features().push_constants)
        {
            target.defines.emplace_back(std::string{push_constants_define}, std::string{});
        }

        shader_module_descriptor descriptor{};
        descriptor.stage = stage;
        descriptor.spirv = compile_library_shader(target, stage);
        const shader_module module = device.create_shader_module(descriptor);
#if defined(_DEBUG)
        if (shader_hot_reload* reload = shader_hot_reload::active();
            reload != nullptr && module.valid() && &reload->target_device() == &device)
        {
            reload->track(module, target, stage);
        }
#endif
        return module;
    }

    shader_module create_library_shader_module(device& device, std::string_view path, shader_stage stage)
    {
        shader_variant variant{};
        variant.path = std::string{path};
        return create_library_shader_module(device, variant, stage);
    }

#if defined(_DEBUG)
    shader_hot_reload::shader_hot_reload(device& device, std::filesystem::path root)
        : m_device(&device), m_watcher(std::move(root)), m_last_scan(std::chrono::steady_clock::now())
    {
        assert(installed_reload == nullptr && "only one shader_hot_reload may be installed at a time");
        installed_reload = this;

        m_root_prefix = m_watcher.root().generic_string();
        if (!m_root_prefix.empty() && m_root_prefix.back() != '/')
        {
            m_root_prefix += '/';
        }
        LOG_INF("Shader hot reload: watching %s (%zu files, polled every %lld ms)",
                m_watcher.root().string().c_str(),
                m_watcher.tracked_count(),
                static_cast<long long>(poll_interval.count()));
    }

    shader_hot_reload::~shader_hot_reload()
    {
        if (installed_reload == this)
        {
            installed_reload = nullptr;
        }
    }

    shader_hot_reload* shader_hot_reload::active()
    {
        return installed_reload;
    }

    device& shader_hot_reload::target_device() const
    {
        return *m_device;
    }

    void shader_hot_reload::track(shader_module module, const shader_variant& variant, shader_stage stage)
    {
        m_modules.push_back(tracked_module{module, variant, stage});
    }

    std::size_t shader_hot_reload::tracked_count() const
    {
        return m_modules.size();
    }

    void shader_hot_reload::poll()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_last_scan < poll_interval)
        {
            return;
        }
        m_last_scan = now;

        const std::vector<core::platform::file_change> changes = m_watcher.poll();
        if (changes.empty())
        {
            return;
        }
        for (const core::platform::file_change& change : changes)
        {
            // The watcher spells each file <root>/<relative>, so the
            // library path is what follows the root prefix.
            const std::string file = change.path.generic_string();
            if (file.size() <= m_root_prefix.size() || file.compare(0, m_root_prefix.size(), m_root_prefix) != 0)
            {
                continue;
            }
            std::string path = file.substr(m_root_prefix.size());
            // The next compile that reads the file must see the edit (or,
            // for a removed file, the embedded copy again).
            shader_library::refresh(path);
            m_pending.insert(std::move(path));
        }
        if (!m_pending.empty())
        {
            reload_pending();
        }
    }

    void shader_hot_reload::reload_pending()
    {
        // Registrations outlive their modules when an owner destroys one
        // (a material template going away); drop those first.
        m_modules.erase(std::remove_if(m_modules.begin(),
                                       m_modules.end(),
                                       [this](const tracked_module& tracked)
                                       { return !m_device->shader_module_live(tracked.module); }),
                        m_modules.end());

        // The modules that read a pending file: their own source, or
        // anything it includes. Many modules share a path (keyword
        // variants, the fullscreen vertex stage), so each path's
        // dependencies are scanned once.
        std::unordered_map<std::string, std::vector<std::string>> dependencies;
        std::vector<const tracked_module*> affected;
        for (const tracked_module& tracked : m_modules)
        {
            auto found = dependencies.find(tracked.variant.path);
            if (found == dependencies.end())
            {
                found = dependencies.emplace(tracked.variant.path, shader_dependencies(tracked.variant.path)).first;
            }
            const std::vector<std::string>& files = found->second;
            if (std::any_of(
                    files.begin(), files.end(), [this](const std::string& file) { return m_pending.count(file) != 0; }))
            {
                affected.push_back(&tracked);
            }
        }

        const std::string changed = join(m_pending);
        if (affected.empty())
        {
            // Its text is refreshed all the same, so the next compile
            // of it (a keyword variant, an IBL kernel) sees the edit.
            LOG_INF("Shader hot reload: %s changed; no live shader module reads it", changed.c_str());
            m_pending.clear();
            return;
        }

        // Recompile every affected module, each distinct compile once.
        // A failure has already been logged by the compiler with
        // glslang's full message; nothing is swapped in until every
        // affected module compiles, so a half-edited include cannot
        // leave the vertex and fragment stages of one pipeline out of
        // step.
        std::map<std::string, std::vector<uint32_t>> compiled;
        std::vector<shader_module_update> updates;
        updates.reserve(affected.size());
        std::size_t failures = 0;
        for (const tracked_module* tracked : affected)
        {
            const std::string identity = compile_identity(tracked->variant, tracked->stage);
            auto found = compiled.find(identity);
            if (found == compiled.end())
            {
                std::vector<uint32_t> spirv;
                try
                {
                    spirv = compile_library_shader(tracked->variant, tracked->stage);
                }
                catch (const std::exception& error)
                {
                    ++failures;
                    LOG_WRN("Shader hot reload: %s", first_line(error.what()).c_str());
                }
                found = compiled.emplace(identity, std::move(spirv)).first;
            }
            if (!found->second.empty())
            {
                updates.push_back(shader_module_update{tracked->module, found->second});
            }
        }
        if (failures > 0)
        {
            LOG_WRN("Shader hot reload: %zu shader(s) failed to compile after %s changed; the previous pipelines stay "
                    "in use until the next change compiles",
                    failures,
                    changed.c_str());
            return;
        }

        if (!m_device->reload_shader_modules(updates))
        {
            LOG_WRN("Shader hot reload: the device could not rebuild the pipelines after %s changed; the previous "
                    "pipelines stay in use until the next change",
                    changed.c_str());
            return;
        }

        std::set<std::string> reloaded;
        for (const tracked_module* tracked : affected)
        {
            reloaded.insert(tracked->variant.path + " (" + stage_label(tracked->stage) + ")");
        }
        LOG_INF("Shader hot reload: %s changed; reloaded %zu shader module(s): %s",
                changed.c_str(),
                updates.size(),
                join(reloaded).c_str());
        m_pending.clear();
    }
#endif
} // namespace rendering_engine::gpu
