// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file view_resources.hpp
 * @brief One view's resource set: its off-screen targets at the view's size,
 *        its resource store, its temporal history and the state every pass
 *        keeps for it.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

#include <core/math/mat4.hpp>
#include <core/math/vec2.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/resource_store.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct pass;

    /**
     * @brief What a pass keeps for one view: its targets at the view's size,
     *        its uniform buffers and bind groups, its history.
     *
     * A pass derives its own type from this and reaches it through
     * @ref view_resources::state, keyed by the pass. It lives as long as the
     * view does (or until the pass leaves the list), so a history never
     * outlives its view and two views never share one; its destructor
     * releases whatever GPU resources it holds.
     */
    struct pass_view_state
    {
        virtual ~pass_view_state() = default;
    };

    /**
     * @brief The temporal state the renderer carries from one frame of a
     *        view to the next.
     */
    struct view_history
    {
        // Frames the view has rendered; indexes its temporal-AA jitter
        // sequence.
        uint64_t frames{0};

        // The jitter the view's last frame was rasterised with.
        core::math::vec2 prev_jitter{0.0f, 0.0f};

        // The view's last unjittered view-projection, valid once it has
        // rendered a frame through its camera.
        core::math::mat4 prev_view_projection{};
        bool has_prev_view_projection{false};
    };

    /**
     * @brief One view's resource set, owned by the renderer and keyed by the
     *        view's identity (the camera proxy it renders).
     *
     * It holds what is the view's alone: the off-screen HDR scene-colour
     * target (with its depth) and the LDR target the post chain resolves
     * into, both at the view's size; the view's @ref resource_store, which
     * falls back to the frame-global store for names it does not hold; the
     * view's @ref view_history; and every pass's @ref pass_view_state for it
     * — among them the temporal-AA, motion-vector and eye-adaptation
     * histories. The renderer creates the set the first frame a view
     * renders, resizes its targets when the view's size changes (the passes
     * compare the size they built their own state for), and destroys it the
     * first frame the view is missing from the list, releasing everything
     * in it.
     * Main-thread only.
     */
    class view_resources
    {
    public:
        view_resources(gpu::device& device, camera_proxy_handle camera);
        ~view_resources();

        view_resources(const view_resources&) = delete;
        view_resources& operator=(const view_resources&) = delete;

        /** @brief The camera proxy the view renders: the view's identity. */
        camera_proxy_handle camera() const noexcept
        {
            return m_camera;
        }

        /** @brief The pixel size the view's targets are built at; 0 until the first @ref resize. */
        uint32_t width() const noexcept
        {
            return m_width;
        }

        uint32_t height() const noexcept
        {
            return m_height;
        }

        /**
         * @brief Builds the targets at @p width x @p height, or rebuilds them
         *        when the size differs from the current one: new targets
         *        first, then the old ones are released (the device defers the
         *        free until the frames using them retire), so every handle a
         *        pass compares against changes. Returns whether it built
         *        anything. Called by the renderer between frames or at the
         *        top of one, before any pass prepares.
         */
        bool resize(uint32_t width, uint32_t height);

        /** @brief The HDR scene-colour target the view's scene pass draws into. */
        color_target scene_color() const noexcept
        {
            return m_scene_color;
        }

        /** @brief The depth attachment of @ref scene_color. */
        gpu::texture scene_depth() const noexcept
        {
            return m_scene_depth;
        }

        /** @brief The LDR target the view's post chain resolves into. */
        color_target ldr_color() const noexcept
        {
            return m_ldr_color;
        }

        /** @brief The view's resource store, handed to its passes as @c frame_context::resources. */
        resource_store& resources() noexcept
        {
            return m_resources;
        }

        /** @copydoc resources() */
        const resource_store& resources() const noexcept
        {
            return m_resources;
        }

        /** @brief What the renderer carries from this view's last frame to its next. */
        view_history& history() noexcept
        {
            return m_history;
        }

        /**
         * @brief The state @p owner keeps for this view, built from @p args
         *        the first time it is asked for.
         *
         * @p T derives from @ref pass_view_state; a pass asks for one type
         * only (a debug build asserts it). Called from the pass's
         * @c prepare.
         */
        template<typename T, typename... Args>
        T& state(const pass& owner, Args&&... args)
        {
            static_assert(std::is_base_of_v<pass_view_state, T>, "a pass's view state derives from pass_view_state");
            if (pass_view_state* existing = find(owner, typeid(T)); existing != nullptr)
            {
                return static_cast<T&>(*existing);
            }
            auto created = std::make_unique<T>(std::forward<Args>(args)...);
            T& result = *created;
            m_states.push_back({&owner, std::type_index{typeid(T)}, std::move(created)});
            return result;
        }

        /** @brief The state @p owner built for this view, or null when it has none yet. */
        template<typename T>
        T* find_state(const pass& owner) const
        {
            static_assert(std::is_base_of_v<pass_view_state, T>, "a pass's view state derives from pass_view_state");
            return static_cast<T*>(find(owner, typeid(T)));
        }

        /** @brief Destroys the state @p owner keeps for this view; the pass is leaving the list. */
        void drop_state(const pass& owner);

    private:
        struct entry
        {
            const pass* owner{nullptr};
            std::type_index type;
            std::unique_ptr<pass_view_state> state;
        };

        // The state @p owner keeps here, or null; asserts that it is a @p type.
        pass_view_state* find(const pass& owner, const std::type_info& type) const;

        void release_targets();

        gpu::device* m_device;
        camera_proxy_handle m_camera{};
        uint32_t m_width{0};
        uint32_t m_height{0};

        color_target m_scene_color{};
        gpu::texture m_scene_depth{};
        color_target m_ldr_color{};

        resource_store m_resources;
        view_history m_history{};

        // A handful of entries, one per pass with view state: a linear
        // walk beats hashing.
        std::vector<entry> m_states;
    };
} // namespace rendering_engine
