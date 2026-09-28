// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/pass_list.hpp>

#include <string>
#include <unordered_set>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>

namespace rendering_engine
{
    namespace
    {
        // Brackets one pass's accesses to the frame's store with the
        // declarations they are checked against (see
        // resource_store::set_declared_access).
        class declared_access_scope
        {
        public:
            declared_access_scope(resource_store* store, const char* pass_name, const pass_io_builder& declared)
                : m_store{store}
            {
                if (m_store != nullptr)
                {
                    m_store->set_declared_access(pass_name, &declared);
                }
            }

            declared_access_scope(const declared_access_scope&) = delete;
            declared_access_scope& operator=(const declared_access_scope&) = delete;

            ~declared_access_scope()
            {
                if (m_store != nullptr)
                {
                    m_store->set_declared_access(nullptr, nullptr);
                }
            }

        private:
            resource_store* m_store;
        };
    } // namespace

    void pass_list::import_external(std::string_view resource)
    {
        m_external.emplace_back(resource);
    }

    size_t pass_list::index_of(std::string_view name) const
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            if (name == m_entries[i].instance->name())
            {
                return i;
            }
        }
        return m_entries.size();
    }

    pass* pass_list::add(std::unique_ptr<pass> p, const pass_placement& placement)
    {
        if (p == nullptr)
        {
            return nullptr;
        }
        const char* name = p->name();
        if (index_of(name) != m_entries.size())
        {
            LOG_ERR("pass_list: a pass named '%s' is already in the list; the new one is not added", name);
            return nullptr;
        }

        entry added{};
        size_t position = m_entries.size();
        switch (placement.where)
        {
        case pass_placement::relation::in_stage:
            // Behind every pass of this stage and the stages before it.
            added.stage = placement.stage;
            position = 0;
            while (position < m_entries.size() && m_entries[position].stage <= placement.stage)
            {
                ++position;
            }
            break;
        case pass_placement::relation::before:
        case pass_placement::relation::after:
        {
            const size_t anchor = index_of(placement.anchor);
            if (anchor == m_entries.size())
            {
                LOG_ERR("pass_list: pass '%s' is placed %s '%s', which is not in the list; it is not added",
                        name,
                        placement.where == pass_placement::relation::before ? "before" : "after",
                        placement.anchor.c_str());
                return nullptr;
            }
            added.stage = m_entries[anchor].stage;
            position = anchor;
            if (placement.where == pass_placement::relation::after)
            {
                // Behind the passes placed after the same anchor earlier,
                // so they keep running in the order they were added.
                added.placed_after = placement.anchor;
                position = anchor + 1;
                while (position < m_entries.size() && m_entries[position].placed_after == placement.anchor)
                {
                    ++position;
                }
            }
            break;
        }
        }

        p->declare_io(added.declared);
        added.instance = std::move(p);
        pass* result = added.instance.get();
        m_entries.insert(m_entries.begin() + static_cast<std::ptrdiff_t>(position), std::move(added));
        return result;
    }

    bool pass_list::remove(std::string_view name)
    {
        const size_t index = index_of(name);
        if (index == m_entries.size())
        {
            return false;
        }
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
        // A pass placed after the removed one keeps its place, but no
        // longer groups with a later pass placed after a new pass of that
        // name.
        for (entry& e : m_entries)
        {
            if (e.placed_after == name)
            {
                e.placed_after.clear();
            }
        }
        return true;
    }

    bool pass_list::set_enabled(std::string_view name, bool enabled)
    {
        const size_t index = index_of(name);
        if (index == m_entries.size())
        {
            return false;
        }
        m_entries[index].enabled = enabled;
        return true;
    }

    bool pass_list::enabled(std::string_view name) const
    {
        const size_t index = index_of(name);
        return index != m_entries.size() && m_entries[index].enabled;
    }

    pass* pass_list::find(std::string_view name) const
    {
        const size_t index = index_of(name);
        return index != m_entries.size() ? m_entries[index].instance.get() : nullptr;
    }

    bool pass_list::validate() const
    {
        // A resource is "produced" once an imported external declares it or
        // an earlier pass writes it. Walking in list order, a read of an
        // unproduced resource means the list is mis-ordered (or a pass
        // forgot to declare a write) — exactly the class of bug a list
        // assembled by independent registrations can hide. An optional
        // read is flagged only when the resource's producer runs after it:
        // a later pass that writes it without reading it (a pass that reads
        // and writes it modifies it in place, it does not produce it). An
        // unordered read reads another time's value and is not checked.
        const auto produced_later = [this](size_t reader, const std::string& resource) -> const char*
        {
            for (size_t i = reader + 1; i < m_entries.size(); ++i)
            {
                const pass_io_builder& declared = m_entries[i].declared;
                bool writes = false;
                bool reads = false;
                for (const resource_use& use : declared.uses())
                {
                    if (use.name == resource)
                    {
                        (use.access == resource_access::write ? writes : reads) = true;
                    }
                }
                if (writes && !reads)
                {
                    return m_entries[i].instance->name();
                }
            }
            return nullptr;
        };

        std::unordered_set<std::string> produced(m_external.begin(), m_external.end());
        bool hazard_free = true;
        std::string order;
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const entry& e = m_entries[i];
            const char* name = e.instance->name();
            for (const resource_use& use : e.declared.uses())
            {
                if (produced.find(use.name) != produced.end())
                {
                    continue;
                }
                if (use.access == resource_access::read)
                {
                    LOG_ERR(
                        "pass_list: pass '%s' reads resource '%s' before any pass produces it", name, use.name.c_str());
                    hazard_free = false;
                }
                else if (use.access == resource_access::read_optional)
                {
                    if (const char* producer = produced_later(i, use.name); producer != nullptr)
                    {
                        LOG_ERR("pass_list: pass '%s' reads resource '%s', which pass '%s' only produces after it",
                                name,
                                use.name.c_str(),
                                producer);
                        hazard_free = false;
                    }
                }
            }
            for (const resource_use& use : e.declared.uses())
            {
                if (use.access == resource_access::write)
                {
                    produced.insert(use.name);
                }
            }
            if (!order.empty())
            {
                order += ", ";
            }
            order += name;
            if (!e.enabled)
            {
                order += " (disabled)";
            }
        }
        LOG_INF("pass_list: validated %zu passes (%s): %s",
                m_entries.size(),
                hazard_free ? "no hazards" : "hazards found — see errors",
                order.c_str());
        return hazard_free;
    }

    void pass_list::prepare(const frame_context& ctx, render_stage first, render_stage last) const
    {
        for (const entry& e : m_entries)
        {
            if (!e.enabled || e.stage < first || e.stage > last)
            {
                continue;
            }
            const declared_access_scope access{ctx.resources, e.instance->name(), e.declared};
            e.instance->prepare(ctx);
        }
    }

    void pass_list::record(gpu::command_encoder& encoder,
                           const frame_context& ctx,
                           render_stage first,
                           render_stage last,
                           pass_hooks* hooks) const
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const entry& e = m_entries[i];
            if (e.stage < first || e.stage > last)
            {
                continue;
            }
            pass& p = *e.instance;
            const char* name = p.name();
            if (!e.enabled)
            {
                // Nothing recorded, but the hooks still bracket the slot so
                // their per-index state stays aligned with the list.
                if (hooks != nullptr)
                {
                    hooks->before_pass(encoder, i, name);
                    hooks->after_pass(encoder, i, name);
                }
                continue;
            }
            // The debug group names the pass in a graphics debugger's
            // command tree; the hooks (the GPU profiler) bracket it.
            encoder.push_debug_group(name);
            if (hooks != nullptr)
            {
                hooks->before_pass(encoder, i, name);
            }
            {
                const declared_access_scope access{ctx.resources, name, e.declared};
                p.record(encoder, ctx);
            }
            if (hooks != nullptr)
            {
                hooks->after_pass(encoder, i, name);
            }
            encoder.pop_debug_group();
        }
    }

    void pass_list::skip(gpu::command_encoder& encoder, render_stage first, render_stage last, pass_hooks& hooks) const
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const entry& e = m_entries[i];
            if (e.stage < first || e.stage > last)
            {
                continue;
            }
            hooks.before_pass(encoder, i, e.instance->name());
            hooks.after_pass(encoder, i, e.instance->name());
        }
    }

    std::vector<std::string> pass_list::pass_names() const
    {
        std::vector<std::string> names;
        names.reserve(m_entries.size());
        for (const entry& e : m_entries)
        {
            names.emplace_back(e.instance->name());
        }
        return names;
    }

    void pass_list::clear()
    {
        m_entries.clear();
        m_external.clear();
    }
} // namespace rendering_engine
