// A component that implements every runtime lifecycle hook and records what
// it saw, shared by the scene-graph and component-lifecycle suites.
//
// Components are moved between pool slots and stores, so the log lives outside
// the component in a caller-owned hook_log: per-hook counters for the suites
// that only care how often something fired, plus an ordered event list for
// the ones that assert the sequence. Several components may share one log —
// give each a tag and the events read "tag.attach", "tag.update", ...

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <runtime/node.hpp>

namespace test_support
{
    struct hook_log
    {
        int attaches{0};
        int updates{0};
        int destroys{0};
        std::vector<bool> active_changes;

        // Every hook in dispatch order: "attach", "update", "active:true" /
        // "active:false", "destroy" — prefixed with "<tag>." when the
        // component that fired it has a tag.
        std::vector<std::string> events;
    };

    // Implements every hook, with a pluggable on_update body so a test can
    // make it act on its node from inside the traversal.
    struct recording_component
    {
        hook_log* log{nullptr};
        std::string tag;
        int value{0};
        std::function<void(runtime::node&)> on_update_action;

        void on_attach(runtime::node&)
        {
            ++log->attaches;
            record("attach");
        }

        void on_update(runtime::node& owner)
        {
            ++log->updates;
            record("update");
            if (on_update_action)
            {
                on_update_action(owner);
            }
        }

        void on_active_changed(runtime::node&, bool active)
        {
            log->active_changes.push_back(active);
            record(active ? "active:true" : "active:false");
        }

        void on_destroy()
        {
            ++log->destroys;
            record("destroy");
        }

    private:
        void record(const char* hook)
        {
            log->events.push_back(tag.empty() ? std::string{hook} : tag + "." + hook);
        }
    };

    inline recording_component recorder(hook_log& log, int value = 0)
    {
        recording_component c;
        c.log = &log;
        c.value = value;
        return c;
    }

    inline recording_component recorder(hook_log& log, const char* tag, int value = 0)
    {
        recording_component c = recorder(log, value);
        c.tag = tag;
        return c;
    }
} // namespace test_support
