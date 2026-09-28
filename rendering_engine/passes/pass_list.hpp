// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file pass_list.hpp
 * @brief The renderer's ordered list of passes, and where a pass is placed
 *        in it.
 *
 * The renderer owns one @ref pass_list; every pass enters it through
 * @ref renderer::add_pass with a @ref pass_placement. Every frame the list
 * prepares its enabled passes in order, then records them in that order —
 * the shadow stage once, the scene to UI stages once per view, the overlay
 * stage once — and whenever it changes it validates the resources each
 * pass declares
 * through @ref pass::declare_io. It does not reorder, allocate, alias or
 * cull anything, and derives no barriers — resource ownership stays with
 * the renderer and the passes, and synchronisation with the render-pass
 * implicit transitions plus the precise barriers the gpu backend emits.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief The stages of a frame, in the order they run. Every pass
     *        belongs to one.
     *
     * The shadow and overlay stages run once per frame, before and after
     * the views; the scene, post and UI stages run once per view (see
     * @ref renderer::render).
     */
    enum class render_stage : uint8_t
    {
        // Light-space depth the scene samples: the directional, omni and
        // spot shadow maps, shared by every view.
        shadow,
        // The view's HDR scene target: the depth pre-pass, the scene pass
        // and the skybox.
        scene,
        // Full-screen effects from the view's HDR scene colour to its
        // output: velocity, volumetric fog, motion blur, bloom, auto
        // exposure, tonemap, TAA and FXAA, which writes the output.
        post,
        // The game UI, composited over the view's output, for the views
        // that want it.
        ui,
        // Tool overlays on top of everything: the debug pass in Debug
        // builds.
        overlay,
    };

    /**
     * @brief Where @ref renderer::add_pass puts a pass: at the end of a
     *        @ref render_stage, or right before or after a pass already in
     *        the list — the anchor, named as its @ref pass::name returns
     *        it (the built-in names are in @ref builtin_passes).
     *
     * A pass placed next to an anchor joins the anchor's stage. There is no
     * index: a placement keeps its meaning however the list around it
     * changes. Passes given the same placement run in the order they were
     * added.
     */
    struct pass_placement
    {
        enum class relation : uint8_t
        {
            in_stage,
            before,
            after,
        };

        relation where{relation::in_stage};
        render_stage stage{render_stage::post};
        std::string anchor;

        /// At the end of @p stage, after every pass already in it.
        static pass_placement in(render_stage stage)
        {
            return {relation::in_stage, stage, {}};
        }

        /// Right before the pass named @p anchor.
        static pass_placement before(std::string_view anchor)
        {
            return {relation::before, render_stage::post, std::string{anchor}};
        }

        /// Right after the pass named @p anchor, and after any pass placed
        /// after it earlier.
        static pass_placement after(std::string_view anchor)
        {
            return {relation::after, render_stage::post, std::string{anchor}};
        }
    };

    /**
     * @brief Observer of the list's recording: called around every pass
     *        with the encoder the pass records into.
     *
     * The GPU profiler stamps timestamps here; anything else that wants a
     * per-pass bracket (a CPU timer, a debugger capture) plugs in the same
     * way. The hooks run outside the pass's own render-pass scope, and for
     * a disabled pass too, with nothing recorded between them, so a hook's
     * per-index state stays aligned with @ref pass_list::pass_names.
     */
    class pass_hooks
    {
    public:
        virtual ~pass_hooks() = default;
        virtual void before_pass(gpu::command_encoder& encoder, size_t index, std::string_view name) = 0;
        virtual void after_pass(gpu::command_encoder& encoder, size_t index, std::string_view name) = 0;
    };

    /**
     * @brief Owning, ordered list of passes: placed by stage and anchor,
     *        validated on every change, prepared and recorded in order
     *        each frame.
     */
    class pass_list
    {
    public:
        pass_list() = default;
        ~pass_list() = default;

        pass_list(const pass_list&) = delete;
        pass_list& operator=(const pass_list&) = delete;

        /**
         * @brief Mark @p resource as valid at frame start with no in-frame
         *        producer.
         *
         * Used for what the renderer publishes whole rather than as a
         * target for the passes to fill: the acquired swapchain image and
         * the colour-grading table. Reads of these never flag as
         * unproduced during @ref validate.
         */
        void import_external(std::string_view resource);

        /**
         * @brief Adds @p p at @p placement and returns it, or refuses it
         *        (logged) and returns null: when a pass of the same name is
         *        already in the list, or the placement's anchor is not.
         *
         * The list owns the pass from here on and collects its
         * declarations (@ref pass::declare_io) now; the returned pointer
         * stays valid until the pass is removed or the list cleared.
         */
        pass* add(std::unique_ptr<pass> p, const pass_placement& placement);

        /// Destroys the pass named @p name; false when there is none.
        bool remove(std::string_view name);

        /**
         * @brief Enables or disables the pass named @p name; false when
         *        there is none. A disabled pass keeps its place and is
         *        still validated, but is neither prepared nor
         *        recorded, so it publishes nothing.
         */
        bool set_enabled(std::string_view name, bool enabled);

        /// Whether a pass named @p name is in the list and enabled.
        bool enabled(std::string_view name) const;

        /// The pass named @p name, or null when there is none.
        pass* find(std::string_view name) const;

        /**
         * @brief Validate the declared dependencies.
         *
         * Walks every pass in order, enabled or not: a read of a resource
         * no earlier pass writes and that was not imported, or an optional
         * read of one a later pass writes, is logged as an error; an
         * unordered read is never flagged. Returns true when no hazard was
         * found. It records nothing. The renderer runs it whenever the
         * list changed, before the next frame, and treats a false result
         * as a programming error: it asserts in debug builds and logs in
         * release, since a mis-declared read is exactly the class of
         * ordering bug the validation exists to catch.
         */
        bool validate() const;

        /**
         * @brief Run the @ref pass::prepare of every enabled pass of the
         *        stages @p first through @p last, in order.
         *
         * The producer-before-consumer walk of one scope's per-pass state
         * (the shadow stage, one view's scene to UI stages, the overlay
         * stage): a pass looks up what the passes before it published in
         * @p ctx's store. Called once per scope before @ref record.
         */
        void prepare(const frame_context& ctx, render_stage first, render_stage last) const;

        /**
         * @brief Record every enabled pass of the stages @p first through
         *        @p last into @p encoder in order, after @ref prepare.
         *
         * Each pass is wrapped in a debug group carrying its name (so a
         * graphics debugger shows the frame as a tree of passes) and, when
         * @p hooks is given, in its before / after calls. A pass that
         * records part of itself on worker threads (the scene pass) forks
         * and joins inside its own @ref pass::record, so the group and the
         * hooks bracket the whole of it.
         */
        void record(gpu::command_encoder& encoder,
                    const frame_context& ctx,
                    render_stage first,
                    render_stage last,
                    pass_hooks* hooks = nullptr) const;

        /**
         * @brief Calls @p hooks around every pass of the stages @p first
         *        through @p last, recording nothing: for a scope that skips
         *        those stages, so a hook's per-index state stays aligned
         *        with @ref pass_names.
         */
        void skip(gpu::command_encoder& encoder, render_stage first, render_stage last, pass_hooks& hooks) const;

        /// Names of the passes, in recording order.
        std::vector<std::string> pass_names() const;

        /// Destroy every pass and drop the imported resources.
        void clear();

        /// Number of passes.
        size_t size() const noexcept
        {
            return m_entries.size();
        }

    private:
        struct entry
        {
            std::unique_ptr<pass> instance;
            render_stage stage{render_stage::post};
            // The anchor of an after() placement, so a later pass placed
            // after the same anchor goes behind this one.
            std::string placed_after;
            // The pass's declarations, collected when it was added.
            pass_io_builder declared;
            bool enabled{true};
        };

        // Index of the pass named @p name, or size() when there is none.
        size_t index_of(std::string_view name) const;

        std::vector<entry> m_entries;
        std::vector<std::string> m_external;
    };
} // namespace rendering_engine
