/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file animator_component.hpp
 * @brief Component that plays animation clips over a skeleton: playback,
 *        cross-fades, a small trigger-driven state machine, and the joint
 *        poses and skinning palettes it writes out.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <core/math/math.hpp>
#include <core/subscription.hpp>
#include <rendering_engine/animation/animation_clip.hpp>
#include <rendering_engine/animation/skeleton.hpp>

namespace runtime
{
    struct node;

    /** @brief "No clip" / "no state" for the lookups below. */
    inline constexpr std::size_t no_clip = static_cast<std::size_t>(-1);

    /** @brief What a clip does when it reaches its end. */
    enum class playback : uint8_t
    {
        // Wrap to the start and keep playing.
        loop,
        // Hold the last pose (the first one, when playing backwards).
        once,
    };

    /**
     * @brief Animates a skeleton and writes the result into the scene.
     *
     * Holds a shared @ref rendering_engine::skeleton and the clips that pose
     * it. A *layer* is one clip playing at its own time and speed; @ref play
     * with a fade starts a new layer at weight 0 and fades every older layer
     * out over the same time, so any number of cross-fades can overlap and
     * the blended pose never pops (translation and scale blend linearly,
     * rotations by sign-aligned normalised sum). A layer with no clip is the
     * bind pose, which is what @ref stop fades to. On top of that sits a
     * minimal state machine: named states (a clip with its playback mode and
     * speed) and transitions from one state — or from any — to another,
     * fired by name through @ref trigger.
     *
     * **Timing.** Playback advances on the engine's fixed update step (the
     * @c core::frame event, subscribed in @ref on_attach), so it is
     * deterministic and independent of the render rate. Each step keeps the
     * previous step's clip times and fade weights alongside the current
     * ones; @ref on_update, once per rendered frame, samples the clips
     * @c core::time::interpolation_alpha of the way between the two, so
     * motion stays smooth when the render rate runs ahead of the fixed
     * rate. Clip time is in seconds (the clips' unit); the fixed step
     * arrives in milliseconds.
     *
     * **Output.** Two kinds of binding say where the pose goes:
     * @ref bind_node makes a scene node follow a joint (its local transform
     * is overwritten with the joint's local pose whenever a clip animates
     * that joint — rigid node animation, and bones other nodes can hang
     * off), and @ref bind_skin hands a skin's joint palette to the
     * @c mesh_component of a node each frame
     * (@ref rendering_engine::model::set_joint_matrices). The palette is
     * computed from the skeleton's own pose, not from the nodes' world
     * matrices, so it does not depend on the order the scene updates in.
     * @ref runtime::instantiate_gltf sets all of this up for an imported
     * model.
     *
     * Bound nodes are held by raw pointer and must outlive the component
     * (or the component must be removed first) — the same contract the
     * nodes @ref runtime::instantiate_gltf returns already carry. The
     * playback state lives on the heap behind a stable address, so the
     * component may be relocated within its pool. Main-thread only.
     */
    struct animator_component
    {
        /** @brief Empty component — no skeleton, animates nothing. */
        animator_component();

        /**
         * @brief Animates @p skeleton with @p clips (either may be empty: a
         *        skeleton with no clips holds its bind pose, which still
         *        gives bound skins a palette).
         */
        animator_component(std::shared_ptr<const rendering_engine::skeleton> skeleton,
                           std::vector<std::shared_ptr<const rendering_engine::animation_clip>> clips);

        ~animator_component();
        animator_component(animator_component&& other) noexcept;
        animator_component& operator=(animator_component&& other) noexcept;
        animator_component(const animator_component&) = delete;
        animator_component& operator=(const animator_component&) = delete;

        /**
         * @brief A new animator over the same skeleton and clips, with the
         *        same state machine and playback, for @c scene::clone.
         *
         * The skeleton and clips are shared (both are immutable data), and
         * the state machine (states, transitions) and current playback
         * (layers and the current state) are copied, so the clone samples
         * the same pose the source does the moment it is made. Node and skin
         * bindings are never copied: @c scene::clone has no way to tell a
         * binding into the cloned subtree (which could be remapped to the
         * copy) from one to a node outside it (which could not), so a
         * binding of either kind is dropped, with a warning, and the clone
         * drives no nodes or meshes until @ref bind_node / @ref bind_skin are
         * called again.
         */
        animator_component clone() const;

        // --- Bindings -----------------------------------------------------

        /**
         * @brief Makes @p target follow @p joint: while a clip animates the
         *        joint, the node's local transform is set to the joint's
         *        local pose, rotated by @p pre_rotation (which folds a basis
         *        change the node's parent does not carry into a root joint,
         *        such as the glTF +Y-up to engine +Z-up turn).
         */
        void bind_node(std::size_t joint, node& target, const core::math::quat& pre_rotation = {});

        /**
         * @brief Feeds skin @p skin's palette, relative to @p mesh_joint (the
         *        joint the mesh hangs from, or @ref rendering_engine::no_joint
         *        for a mesh in the skeleton's model space), to the model of
         *        @p mesh_node's @c mesh_component every frame.
         */
        void bind_skin(std::size_t skin, std::size_t mesh_joint, node& mesh_node);

        // --- Clips and playback ---------------------------------------------

        const std::shared_ptr<const rendering_engine::skeleton>& skeleton() const noexcept;
        const std::vector<std::shared_ptr<const rendering_engine::animation_clip>>& clips() const noexcept;

        /** @brief Index of the first clip named @p name, or @ref no_clip. */
        std::size_t find_clip(std::string_view name) const noexcept;

        /**
         * @brief Starts clip @p clip from its beginning. With a positive
         *        @p fade_seconds it cross-fades in over that time (from the
         *        bind pose when nothing was playing); otherwise it replaces
         *        whatever played. @p speed scales the clip's time (negative
         *        plays it backwards). Leaves the state machine's current
         *        state unchanged. False, and nothing changes, for an unknown
         *        clip.
         */
        bool play(std::size_t clip, float fade_seconds = 0.0f, playback mode = playback::loop, float speed = 1.0f);
        bool play(std::string_view clip, float fade_seconds = 0.0f, playback mode = playback::loop, float speed = 1.0f);

        /** @brief Fades (or, with no fade, snaps) back to the bind pose. */
        void stop(float fade_seconds = 0.0f);

        /** @brief Sets the speed of the most recently started clip. */
        void set_speed(float speed);

        /** @brief The most recently started clip, or @ref no_clip. */
        std::size_t current_clip() const noexcept;

        /** @brief That clip's time at the latest fixed step, in seconds (wrapped for a loop). */
        float current_time() const noexcept;

        /** @brief Whether a clip is playing: started, and not a finished @ref playback::once. */
        bool is_playing() const noexcept;

        // --- State machine ------------------------------------------------

        /**
         * @brief Declares state @p name playing @p clip. False (and no
         *        change) when the name is taken or the clip is unknown.
         */
        bool add_state(std::string name, std::size_t clip, playback mode = playback::loop, float speed = 1.0f);

        /**
         * @brief Declares that @p trigger_name moves the machine from state
         *        @p from — or from any state, for an empty @p from — to state
         *        @p to, cross-fading over @p fade_seconds. A transition out
         *        of the current state wins over an any-state one. False
         *        when a named state does not exist.
         */
        bool add_transition(std::string_view from, std::string trigger_name, std::string_view to, float fade_seconds);

        /** @brief Enters state @p name directly (cross-fading over @p fade_seconds). */
        bool set_state(std::string_view name, float fade_seconds = 0.0f);

        /**
         * @brief Fires trigger @p name: takes the matching transition, if any.
         *        The switch is immediate; the cross-fade itself runs on the
         *        following fixed steps. False when no transition matches.
         */
        bool trigger(std::string_view name);

        /** @brief The current state's name; empty before any state is entered and after @ref stop. */
        const std::string& current_state() const noexcept;

        // --- Driving ------------------------------------------------------

        /**
         * @brief Advances playback by one fixed step of @p delta_ms. Called
         *        from the @c core::frame subscription while attached and
         *        active; exposed for callers driving an unattached animator.
         */
        void advance(double delta_ms);

        /**
         * @brief Samples the pose @p alpha of the way from the previous
         *        fixed step to the latest one and writes it to the bound
         *        nodes and skins. Called from @ref on_update; exposed like
         *        @ref advance.
         */
        void apply(double alpha);

        // --- Component hooks ------------------------------------------------

        /** @brief Subscribes to the fixed step and writes the first pose. */
        void on_attach(node& owner);

        /** @brief Writes the render-time pose (see the class notes). */
        void on_update(node& owner);

        /** @brief Drops the fixed-step subscription. */
        void on_destroy();

        /** @brief Pauses playback while the owning node is disabled. */
        void on_active_changed(node& owner, bool active);

    private:
        struct impl;

        // Heap state: the fixed-step callback captures its address, which
        // stays put when the pool relocates the component.
        std::unique_ptr<impl> m_impl;
        core::subscription m_fixed_step;
    };
} // namespace runtime
