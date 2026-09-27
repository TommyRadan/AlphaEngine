// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/scene_manager.hpp>

#include <utility>

#include <core/log.hpp>
#include <runtime/scene.hpp>

namespace
{
    const core::string_id& persistent_name()
    {
        static const core::string_id name{"persistent"};
        return name;
    }
} // namespace

runtime::scene_manager::scene_manager()
{
    m_scenes.push_back(entry{persistent_name(), std::make_unique<runtime::scene>()});
    m_active = m_scenes.front().scene.get();
}

runtime::scene_manager::~scene_manager()
{
    // Newest first, the persistent scene last: a later scene may hold nodes
    // linked under an earlier one's.
    while (!m_scenes.empty())
    {
        m_scenes.pop_back();
    }
}

void runtime::scene_manager::init()
{
    m_scenes.front().scene->init();
}

void runtime::scene_manager::quit()
{
    for (std::size_t index = m_scenes.size(); index-- > 1;)
    {
        destroy(index);
    }
    m_active = m_scenes.front().scene.get();
    m_scenes.front().scene->quit();
}

void runtime::scene_manager::update()
{
    m_updating = true;
    // Indexed: a hook may load a scene mid-loop, appending here; it gets its
    // first update this frame. Scenes on their way out are skipped.
    for (std::size_t index = 0; index < m_scenes.size(); ++index)
    {
        if (!m_scenes[index].unload_pending)
        {
            m_scenes[index].scene->update();
        }
    }
    m_updating = false;
    apply_pending_unloads();
}

runtime::scene& runtime::scene_manager::load(core::string_id name, load_mode mode)
{
    runtime::scene* scene = find(name);
    if (scene == nullptr)
    {
        m_scenes.push_back(entry{name, std::make_unique<runtime::scene>()});
        scene = m_scenes.back().scene.get();
        scene->init();
        LOG_INF("Loaded scene '%s'%s", name.c_str(), mode == load_mode::additive ? " (additive)" : "");
    }
    else if (entry* existing = find_entry(*scene))
    {
        existing->unload_pending = false;
    }

    if (mode == load_mode::single)
    {
        for (std::size_t index = 1; index < m_scenes.size(); ++index)
        {
            if (m_scenes[index].scene.get() != scene)
            {
                m_scenes[index].unload_pending = true;
            }
        }
        m_active = scene;
        if (!busy())
        {
            apply_pending_unloads();
        }
    }
    return *scene;
}

bool runtime::scene_manager::unload(core::string_id name)
{
    runtime::scene* scene = find(name);
    if (scene == nullptr)
    {
        LOG_WRN("runtime::scene_manager::unload: no scene '%s' is loaded", name.c_str());
        return false;
    }
    return unload(*scene);
}

bool runtime::scene_manager::unload(runtime::scene& scene)
{
    entry* target = find_entry(scene);
    if (target == nullptr)
    {
        LOG_WRN("runtime::scene_manager::unload: the scene is not loaded here");
        return false;
    }
    if (target == &m_scenes.front())
    {
        LOG_WRN("runtime::scene_manager::unload: the persistent scene cannot be unloaded");
        return false;
    }
    target->unload_pending = true;
    if (!busy())
    {
        apply_pending_unloads();
    }
    return true;
}

runtime::scene* runtime::scene_manager::find(core::string_id name) noexcept
{
    for (entry& candidate : m_scenes)
    {
        if (candidate.name == name)
        {
            return candidate.scene.get();
        }
    }
    return nullptr;
}

core::string_id runtime::scene_manager::name_of(const runtime::scene& scene) const noexcept
{
    const entry* found = find_entry(scene);
    return found != nullptr ? found->name : core::string_id{};
}

runtime::scene& runtime::scene_manager::persistent_scene() noexcept
{
    return *m_scenes.front().scene;
}

runtime::scene& runtime::scene_manager::active_scene() noexcept
{
    return *m_active;
}

void runtime::scene_manager::set_active_scene(runtime::scene& scene)
{
    if (find_entry(scene) == nullptr)
    {
        LOG_ERR("runtime::scene_manager::set_active_scene: the scene is not loaded here");
        return;
    }
    m_active = &scene;
}

void runtime::scene_manager::set_enabled(runtime::scene& scene, bool enabled)
{
    // The root's active flag drives every component below it through
    // on_active_changed; that is what takes the scene's meshes, lights and
    // cameras out of the renderer's registries (and puts them back).
    if (scene.is_traversing())
    {
        scene.defer_set_active(scene.root, enabled);
    }
    else
    {
        scene.root.set_active(enabled);
    }
}

bool runtime::scene_manager::is_enabled(const runtime::scene& scene) const noexcept
{
    return scene.root.is_active();
}

std::size_t runtime::scene_manager::scene_count() const noexcept
{
    return m_scenes.size();
}

runtime::scene& runtime::scene_manager::scene_at(std::size_t index) noexcept
{
    return *m_scenes[index].scene;
}

core::string_id runtime::scene_manager::name_at(std::size_t index) const noexcept
{
    return m_scenes[index].name;
}

runtime::scene_manager::entry* runtime::scene_manager::find_entry(const runtime::scene& scene) noexcept
{
    for (entry& candidate : m_scenes)
    {
        if (candidate.scene.get() == &scene)
        {
            return &candidate;
        }
    }
    return nullptr;
}

const runtime::scene_manager::entry* runtime::scene_manager::find_entry(const runtime::scene& scene) const noexcept
{
    for (const entry& candidate : m_scenes)
    {
        if (candidate.scene.get() == &scene)
        {
            return &candidate;
        }
    }
    return nullptr;
}

bool runtime::scene_manager::busy() const noexcept
{
    if (m_updating)
    {
        return true;
    }
    for (const entry& candidate : m_scenes)
    {
        if (candidate.scene->is_traversing())
        {
            return true;
        }
    }
    return false;
}

void runtime::scene_manager::apply_pending_unloads()
{
    for (std::size_t index = m_scenes.size(); index-- > 1;)
    {
        if (index < m_scenes.size() && m_scenes[index].unload_pending)
        {
            destroy(index);
        }
    }
}

void runtime::scene_manager::destroy(std::size_t index)
{
    entry doomed = std::move(m_scenes[index]);
    m_scenes.erase(m_scenes.begin() + static_cast<std::ptrdiff_t>(index));

    // Hand the active role on first, so nothing reached from the teardown
    // below sees a dying active scene: to the newest scene left, which is
    // the persistent one when no other remains.
    if (m_active == doomed.scene.get())
    {
        m_active = m_scenes.back().scene.get();
    }

    doomed.scene->quit();
    LOG_INF("Unloaded scene '%s'", doomed.name.c_str());
}
