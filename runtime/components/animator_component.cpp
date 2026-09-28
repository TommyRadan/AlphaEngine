// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/animator_component.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include <core/event.hpp>
#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/time.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>

namespace runtime
{
    namespace
    {
        namespace math = core::math;
        using animation::animation_clip;
        using animation::no_joint;

        // Layers blended at once. A burst of cross-fades beyond this drops
        // the faintest outgoing layer rather than growing without bound.
        constexpr std::size_t max_layers = 8;

        // The fixed step arrives in milliseconds; clip time is in seconds.
        constexpr double milliseconds_per_second = 1000.0;

        // Below this total the blend weights carry no usable direction.
        constexpr float weight_epsilon = 1e-6f;

        // One clip playing at its own time, speed and blend weight. Both the
        // latest fixed step's values and the previous step's are kept so the
        // render-time sample can sit between them. A layer with no clip is
        // the bind pose.
        struct layer
        {
            std::size_t clip{no_clip};
            playback mode{playback::loop};
            float speed{1.0f};

            // Seconds; unwrapped for a loop (wrapped only when sampled) so
            // the render-time interpolation never runs backwards across the
            // loop point.
            double time{0.0};
            double previous_time{0.0};

            float weight{1.0f};
            float previous_weight{1.0f};
            float target_weight{1.0f};

            // Weight per second towards target_weight; 0 snaps.
            float fade_rate{0.0f};
        };

        struct node_binding
        {
            std::size_t joint{no_joint};
            node* target{nullptr};
            math::quat pre_rotation{};
        };

        struct skin_binding
        {
            std::size_t skin{0};
            std::size_t mesh_joint{no_joint};
            node* mesh_node{nullptr};
        };

        struct machine_state
        {
            std::string name;
            std::size_t clip{no_clip};
            playback mode{playback::loop};
            float speed{1.0f};
        };

        struct transition
        {
            // no_clip: from any state.
            std::size_t from{no_clip};
            std::string trigger;
            std::size_t to{no_clip};
            float fade_seconds{0.0f};
        };

        bool interpolating(const layer& l)
        {
            return l.time != l.previous_time || l.weight != l.previous_weight;
        }
    } // namespace

    struct animator_component::impl
    {
        std::shared_ptr<const animation::skeleton> skeleton;
        std::vector<std::shared_ptr<const animation_clip>> clips;

        // The joints some clip drives: the only ones that are blended and
        // written to bound nodes. Every other joint holds its bind pose.
        std::vector<std::size_t> animated_joints;
        std::vector<bool> animated;

        std::vector<layer> layers;
        std::vector<node_binding> node_bindings;
        std::vector<skin_binding> skin_bindings;
        std::vector<machine_state> states;
        std::vector<transition> transitions;
        std::size_t current_state{no_clip};

        bool active{true};

        // Set whenever the sampled pose may differ from the one last
        // written; apply() is a no-op otherwise.
        bool needs_apply{true};

        // Scratch reused across frames.
        std::vector<math::trs> pose;
        std::vector<math::trs> layer_pose;
        std::vector<math::vec3> sum_translation;
        std::vector<math::quat> sum_rotation;
        std::vector<math::vec3> sum_scale;
        std::vector<math::mat4> model;
        std::vector<math::mat4> palette;

        impl() = default;

        impl(std::shared_ptr<const animation::skeleton> in_skeleton,
             std::vector<std::shared_ptr<const animation_clip>> in_clips)
            : skeleton{std::move(in_skeleton)}, clips{std::move(in_clips)}
        {
            const std::size_t count = skeleton ? skeleton->joint_count() : 0;
            animated.assign(count, false);
            for (const auto& clip : clips)
            {
                if (!clip)
                {
                    continue;
                }
                for (const animation::joint_track& track : clip->tracks())
                {
                    if (track.joint < count && !animated[track.joint])
                    {
                        animated[track.joint] = true;
                        animated_joints.push_back(track.joint);
                    }
                }
            }
        }

        const animation_clip* clip_at(std::size_t index) const
        {
            return index < clips.size() ? clips[index].get() : nullptr;
        }

        double duration(std::size_t clip) const
        {
            const animation_clip* c = clip_at(clip);
            return c != nullptr ? static_cast<double>(c->duration()) : 0.0;
        }

        // Where on the clip an (unwrapped) layer time falls.
        double wrapped_time(const layer& l, double time) const
        {
            const double length = duration(l.clip);
            if (length <= 0.0)
            {
                return 0.0;
            }
            if (l.mode == playback::once)
            {
                return std::clamp(time, 0.0, length);
            }
            const double wrapped = std::fmod(time, length);
            return wrapped < 0.0 ? wrapped + length : wrapped;
        }

        bool finished(const layer& l) const
        {
            if (l.clip == no_clip || l.mode != playback::once)
            {
                return false;
            }
            return l.speed >= 0.0f ? l.time >= duration(l.clip) : l.time <= 0.0;
        }

        void start(std::size_t clip, float fade_seconds, playback mode, float speed)
        {
            layer fresh;
            fresh.clip = clip;
            fresh.mode = mode;
            fresh.speed = speed;
            // Played backwards, a clip starts from its end.
            fresh.time = speed < 0.0f ? duration(clip) : 0.0;
            fresh.previous_time = fresh.time;

            if (!(fade_seconds > 0.0f))
            {
                layers.clear();
                layers.push_back(fresh);
                needs_apply = true;
                return;
            }

            if (layers.empty())
            {
                // Nothing playing: fade in from the bind pose.
                layers.push_back(layer{});
            }
            const float rate = 1.0f / fade_seconds;
            for (layer& l : layers)
            {
                l.target_weight = 0.0f;
                l.fade_rate = rate;
            }
            fresh.weight = 0.0f;
            fresh.previous_weight = 0.0f;
            fresh.target_weight = 1.0f;
            fresh.fade_rate = rate;
            layers.push_back(fresh);

            while (layers.size() > max_layers)
            {
                // Drop the faintest outgoing layer (never the new one).
                const auto faintest =
                    std::min_element(layers.begin(),
                                     layers.end() - 1,
                                     [](const layer& a, const layer& b) { return a.weight < b.weight; });
                layers.erase(faintest);
            }
            needs_apply = true;
        }

        void advance(double delta_seconds)
        {
            // A layer that has faded all the way out contributes nothing to
            // the interval about to start; drop it (the newest layer stays).
            if (layers.size() > 1)
            {
                const std::size_t before = layers.size();
                layers.erase(std::remove_if(layers.begin(),
                                            layers.end() - 1,
                                            [](const layer& l) { return l.weight <= 0.0f && l.target_weight <= 0.0f; }),
                             layers.end() - 1);
                needs_apply = needs_apply || layers.size() != before;
            }

            for (layer& l : layers)
            {
                l.previous_time = l.time;
                l.previous_weight = l.weight;

                const double length = duration(l.clip);
                if (l.clip != no_clip && length > 0.0)
                {
                    l.time += delta_seconds * static_cast<double>(l.speed);
                    if (l.mode == playback::once)
                    {
                        l.time = std::clamp(l.time, 0.0, length);
                    }
                    else
                    {
                        // Keep both times in step but bounded: shift whole
                        // loops out once the earlier of the two has passed
                        // one, so hours of looping lose no precision.
                        const double shift = std::floor(std::min(l.time, l.previous_time) / length) * length;
                        l.time -= shift;
                        l.previous_time -= shift;
                    }
                }

                if (l.weight != l.target_weight)
                {
                    if (l.fade_rate <= 0.0f)
                    {
                        l.weight = l.target_weight;
                    }
                    else
                    {
                        const float step = l.fade_rate * static_cast<float>(delta_seconds);
                        l.weight = l.weight < l.target_weight ? std::min(l.weight + step, l.target_weight)
                                                              : std::max(l.weight - step, l.target_weight);
                    }
                }
                needs_apply = needs_apply || interpolating(l);
            }
        }

        // Fills `pose` with the blended pose alpha of the way between the
        // previous and the latest fixed step.
        void evaluate(double alpha)
        {
            const std::vector<animation::skeleton_joint>& joints = skeleton->joints();
            pose.resize(joints.size());
            for (std::size_t j = 0; j < joints.size(); ++j)
            {
                pose[j] = joints[j].bind_pose;
            }
            if (layers.empty() || animated_joints.empty())
            {
                return;
            }

            const auto sample_layer = [&](const layer& l, std::vector<math::trs>& out)
            {
                const animation_clip* c = clip_at(l.clip);
                if (c == nullptr)
                {
                    return;
                }
                const double time = l.previous_time + (l.time - l.previous_time) * alpha;
                c->sample(static_cast<float>(wrapped_time(l, time)), out);
            };

            float total = 0.0f;
            for (const layer& l : layers)
            {
                total += math::lerp(l.previous_weight, l.weight, static_cast<float>(alpha));
            }
            if (layers.size() == 1 || total <= weight_epsilon)
            {
                // One layer is the whole pose, whatever its weight.
                sample_layer(layers.back(), pose);
                return;
            }

            sum_translation.assign(animated_joints.size(), math::vec3{0.0f, 0.0f, 0.0f});
            sum_rotation.assign(animated_joints.size(), math::quat{0.0f, 0.0f, 0.0f, 0.0f});
            sum_scale.assign(animated_joints.size(), math::vec3{0.0f, 0.0f, 0.0f});
            for (const layer& l : layers)
            {
                const float weight = math::lerp(l.previous_weight, l.weight, static_cast<float>(alpha)) / total;
                if (weight <= 0.0f)
                {
                    continue;
                }
                layer_pose = pose;
                sample_layer(l, layer_pose);
                for (std::size_t k = 0; k < animated_joints.size(); ++k)
                {
                    const math::trs& local = layer_pose[animated_joints[k]];
                    sum_translation[k] += local.translation * weight;
                    sum_scale[k] += local.scale * weight;
                    // q and -q are one rotation: add each in the hemisphere
                    // of what has been summed so far, or they cancel.
                    const math::quat rotation =
                        math::dot(sum_rotation[k], local.rotation) < 0.0f ? -local.rotation : local.rotation;
                    sum_rotation[k] = sum_rotation[k] + rotation * weight;
                }
            }
            for (std::size_t k = 0; k < animated_joints.size(); ++k)
            {
                math::trs& local = pose[animated_joints[k]];
                local.translation = sum_translation[k];
                local.scale = sum_scale[k];
                if (math::dot(sum_rotation[k], sum_rotation[k]) > weight_epsilon)
                {
                    local.rotation = math::normalize(sum_rotation[k]);
                }
            }
        }

        void apply(double alpha)
        {
            if (!skeleton || !needs_apply)
            {
                return;
            }
            evaluate(std::clamp(alpha, 0.0, 1.0));

            for (const node_binding& binding : node_bindings)
            {
                if (binding.target == nullptr || binding.joint >= animated.size() || !animated[binding.joint])
                {
                    continue;
                }
                math::trs local = pose[binding.joint];
                local.translation = binding.pre_rotation * local.translation;
                local.rotation = binding.pre_rotation * local.rotation;
                binding.target->transform.set_trs(local);
            }

            if (!skin_bindings.empty())
            {
                skeleton->model_matrices(pose, model);
                for (const skin_binding& binding : skin_bindings)
                {
                    if (binding.mesh_node == nullptr)
                    {
                        continue;
                    }
                    mesh_component* mesh = binding.mesh_node->get_component<mesh_component>();
                    if (mesh == nullptr || mesh->is_empty())
                    {
                        continue;
                    }
                    skeleton->skin_matrices(binding.skin, model, binding.mesh_joint, palette);
                    mesh->set_joint_matrices(palette);
                }
            }

            // While a layer is between two different fixed states the next
            // render frame samples at another alpha, so keep applying.
            needs_apply = std::any_of(layers.begin(), layers.end(), interpolating);
        }

        std::size_t find_state(std::string_view name) const
        {
            for (std::size_t s = 0; s < states.size(); ++s)
            {
                if (states[s].name == name)
                {
                    return s;
                }
            }
            return no_clip;
        }

        void enter(std::size_t state, float fade_seconds)
        {
            const machine_state& target = states[state];
            start(target.clip, fade_seconds, target.mode, target.speed);
            current_state = state;
        }
    };

    animator_component::animator_component() : m_impl{std::make_unique<impl>()} {}

    animator_component::animator_component(std::shared_ptr<const animation::skeleton> skeleton,
                                           std::vector<std::shared_ptr<const animation::animation_clip>> clips)
        : m_impl{std::make_unique<impl>(std::move(skeleton), std::move(clips))}
    {
    }

    animator_component::~animator_component() = default;
    animator_component::animator_component(animator_component&& other) noexcept = default;
    animator_component& animator_component::operator=(animator_component&& other) noexcept = default;

    animator_component animator_component::clone() const
    {
        if (!m_impl)
        {
            return animator_component{};
        }

        animator_component copy{m_impl->skeleton, m_impl->clips};
        copy.m_impl->states = m_impl->states;
        copy.m_impl->transitions = m_impl->transitions;
        copy.m_impl->current_state = m_impl->current_state;
        // Same layers, so the copy samples the same pose the source does
        // right now; apply() re-runs once a skin or node is bound to it.
        copy.m_impl->layers = m_impl->layers;
        copy.m_impl->active = m_impl->active;
        copy.m_impl->needs_apply = true;

        if (!m_impl->node_bindings.empty() || !m_impl->skin_bindings.empty())
        {
            LOG_WRN("runtime::animator_component::clone: node and skin bindings are not copied "
                    "(a binding cannot be told apart from one outside the cloned subtree); "
                    "bind_node/bind_skin the clone again");
        }
        return copy;
    }

    void animator_component::bind_node(std::size_t joint, node& target, const core::math::quat& pre_rotation)
    {
        m_impl->node_bindings.push_back(node_binding{joint, &target, pre_rotation});
        m_impl->needs_apply = true;
    }

    void animator_component::bind_skin(std::size_t skin, std::size_t mesh_joint, node& mesh_node)
    {
        m_impl->skin_bindings.push_back(skin_binding{skin, mesh_joint, &mesh_node});
        m_impl->needs_apply = true;
    }

    const std::shared_ptr<const animation::skeleton>& animator_component::skeleton() const noexcept
    {
        return m_impl->skeleton;
    }

    const std::vector<std::shared_ptr<const animation::animation_clip>>& animator_component::clips() const noexcept
    {
        return m_impl->clips;
    }

    std::size_t animator_component::find_clip(std::string_view name) const noexcept
    {
        for (std::size_t c = 0; c < m_impl->clips.size(); ++c)
        {
            if (m_impl->clips[c] && m_impl->clips[c]->name() == name)
            {
                return c;
            }
        }
        return no_clip;
    }

    bool animator_component::play(std::size_t clip, float fade_seconds, playback mode, float speed)
    {
        if (m_impl->clip_at(clip) == nullptr)
        {
            return false;
        }
        m_impl->start(clip, fade_seconds, mode, speed);
        return true;
    }

    bool animator_component::play(std::string_view clip, float fade_seconds, playback mode, float speed)
    {
        return play(find_clip(clip), fade_seconds, mode, speed);
    }

    void animator_component::stop(float fade_seconds)
    {
        m_impl->start(no_clip, fade_seconds, playback::loop, 1.0f);
        m_impl->current_state = no_clip;
    }

    void animator_component::set_speed(float speed)
    {
        if (!m_impl->layers.empty())
        {
            m_impl->layers.back().speed = speed;
        }
    }

    std::size_t animator_component::current_clip() const noexcept
    {
        return m_impl->layers.empty() ? no_clip : m_impl->layers.back().clip;
    }

    float animator_component::current_time() const noexcept
    {
        if (m_impl->layers.empty())
        {
            return 0.0f;
        }
        const layer& current = m_impl->layers.back();
        return static_cast<float>(m_impl->wrapped_time(current, current.time));
    }

    bool animator_component::is_playing() const noexcept
    {
        return !m_impl->layers.empty() && m_impl->layers.back().clip != no_clip &&
               !m_impl->finished(m_impl->layers.back());
    }

    bool animator_component::add_state(std::string name, std::size_t clip, playback mode, float speed)
    {
        if (m_impl->clip_at(clip) == nullptr || m_impl->find_state(name) != no_clip)
        {
            return false;
        }
        m_impl->states.push_back(machine_state{std::move(name), clip, mode, speed});
        return true;
    }

    bool animator_component::add_transition(std::string_view from,
                                            std::string trigger_name,
                                            std::string_view to,
                                            float fade_seconds)
    {
        const std::size_t from_state = from.empty() ? no_clip : m_impl->find_state(from);
        const std::size_t to_state = m_impl->find_state(to);
        if ((!from.empty() && from_state == no_clip) || to_state == no_clip)
        {
            return false;
        }
        m_impl->transitions.push_back(transition{from_state, std::move(trigger_name), to_state, fade_seconds});
        return true;
    }

    bool animator_component::set_state(std::string_view name, float fade_seconds)
    {
        const std::size_t state = m_impl->find_state(name);
        if (state == no_clip)
        {
            return false;
        }
        m_impl->enter(state, fade_seconds);
        return true;
    }

    bool animator_component::trigger(std::string_view name)
    {
        // A transition out of the current state takes precedence over an
        // any-state one with the same trigger.
        const transition* chosen = nullptr;
        for (const transition& candidate : m_impl->transitions)
        {
            if (candidate.trigger != name)
            {
                continue;
            }
            if (candidate.from == m_impl->current_state && candidate.from != no_clip)
            {
                chosen = &candidate;
                break;
            }
            if (candidate.from == no_clip && chosen == nullptr)
            {
                chosen = &candidate;
            }
        }
        if (chosen == nullptr)
        {
            return false;
        }
        m_impl->enter(chosen->to, chosen->fade_seconds);
        return true;
    }

    const std::string& animator_component::current_state() const noexcept
    {
        static const std::string none;
        return m_impl->current_state < m_impl->states.size() ? m_impl->states[m_impl->current_state].name : none;
    }

    void animator_component::advance(double delta_ms)
    {
        m_impl->advance(std::max(delta_ms, 0.0) / milliseconds_per_second);
    }

    void animator_component::apply(double alpha)
    {
        m_impl->apply(alpha);
    }

    void animator_component::on_attach(node& owner)
    {
        (void)owner;
        if (!m_impl)
        {
            return;
        }
        // The callback captures the heap state, not the component, which the
        // pool may move.
        impl* state = m_impl.get();
        m_fixed_step = runtime::current_engine().events->subscribe<core::frame>(
            [state](const core::frame& step)
            {
                if (state->active)
                {
                    state->advance(static_cast<double>(step.m_delta_time) / milliseconds_per_second);
                }
            });
        // Bound skins get a palette before their first draw.
        m_impl->needs_apply = true;
        m_impl->apply(0.0);
    }

    void animator_component::on_update(node& owner)
    {
        (void)owner;
        if (m_impl && m_impl->active)
        {
            m_impl->apply(runtime::current_engine().time->interpolation_alpha());
        }
    }

    void animator_component::on_destroy()
    {
        m_fixed_step.reset();
    }

    void animator_component::on_active_changed(node& owner, bool active)
    {
        (void)owner;
        if (m_impl)
        {
            m_impl->active = active;
        }
    }
} // namespace runtime
