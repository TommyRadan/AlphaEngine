// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/resource_store.hpp>

#include <algorithm>
#include <cassert>

#include <core/log.hpp>

namespace rendering_engine
{
    bool pass_io_builder::declares(std::string_view resource) const noexcept
    {
        return std::any_of(m_uses.begin(), m_uses.end(), [&](const resource_use& use) { return use.name == resource; });
    }

    bool pass_io_builder::declares_write(std::string_view resource) const noexcept
    {
        return std::any_of(m_uses.begin(),
                           m_uses.end(),
                           [&](const resource_use& use)
                           { return use.access == resource_access::write && use.name == resource; });
    }

    void resource_store::clear() noexcept
    {
        ++m_generation;
    }

    void* resource_store::publish_slot(std::string_view name, const std::type_info& type, std::size_t size)
    {
#if _DEBUG
        check_access(name, true);
#endif
        auto it = m_slots.find(name);
        if (it == m_slots.end())
        {
            slot fresh{std::type_index{type}, size, std::make_unique<std::byte[]>(size), 0};
            it = m_slots.emplace(std::string{name}, std::move(fresh)).first;
        }
        else if (it->second.type != std::type_index{type})
        {
            assert(false && "resource_store: a resource is published as a different type than before");
            LOG_ERR("resource_store: '%.*s' is published as a different type than before; ignored",
                    static_cast<int>(name.size()),
                    name.data());
            return nullptr;
        }
        it->second.generation = m_generation;
        return it->second.storage.get();
    }

    const void* resource_store::find_slot(std::string_view name, const std::type_info& type) const
    {
#if _DEBUG
        check_access(name, false);
#endif
        const auto it = m_slots.find(name);
        if (it == m_slots.end() || it->second.generation != m_generation)
        {
            return m_fallback != nullptr ? m_fallback->find_slot(name, type) : nullptr;
        }
        if (it->second.type != std::type_index{type})
        {
            assert(false && "resource_store: a resource is looked up as a different type than it was published as");
            LOG_ERR("resource_store: '%.*s' is looked up as a different type than it was published as",
                    static_cast<int>(name.size()),
                    name.data());
            return nullptr;
        }
        return it->second.storage.get();
    }

#if _DEBUG
    void resource_store::check_access(std::string_view name, bool publishing) const
    {
        if (m_declared == nullptr)
        {
            return;
        }
        const bool declared = publishing ? m_declared->declares_write(name) : m_declared->declares(name);
        if (declared)
        {
            return;
        }
        std::string report{m_pass_name != nullptr ? m_pass_name : "?"};
        report += publishing ? " publishes " : " looks up ";
        report += name;
        if (!m_reported.insert(report).second)
        {
            return;
        }
        LOG_ERR("resource_store: pass '%s' %s '%.*s' without declaring %s it (see pass::declare_io)",
                m_pass_name != nullptr ? m_pass_name : "?",
                publishing ? "publishes" : "looks up",
                static_cast<int>(name.size()),
                name.data(),
                publishing ? "a write of" : "a use of");
    }
#endif
} // namespace rendering_engine
