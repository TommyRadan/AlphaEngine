// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file pass_list.hpp
 * @brief The renderer's ordered list of passes.
 *
 * The renderer owns one @ref pass_list and registers its passes into it in
 * render order. Every frame the list prepares them in that order, then
 * records them in that order, and it validates, once, the resources each
 * pass declares through @ref pass::declare_io: every read must be produced
 * by an earlier pass or imported as valid at frame start. It does not
 * reorder, allocate, alias or cull anything, and derives no barriers —
 * resource ownership stays with the renderer and the passes, and
 * synchronisation with the render-pass implicit transitions plus the
 * precise barriers the gpu backend emits.
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
     * @brief Observer of the list's recording: called around every pass
     *        with the encoder the pass records into.
     *
     * The GPU profiler stamps timestamps here; anything else that wants a
     * per-pass bracket (a CPU timer, a debugger capture) plugs in the same
     * way. The hooks run outside the pass's own render-pass scope.
     */
    class pass_hooks
    {
    public:
        virtual ~pass_hooks() = default;
        virtual void before_pass(gpu::command_encoder& encoder, size_t index, std::string_view name) = 0;
        virtual void after_pass(gpu::command_encoder& encoder, size_t index, std::string_view name) = 0;
    };

    /**
     * @brief Owning, ordered list of passes: validate the declared
     *        dependencies once, then record every pass in order each frame.
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
         * Used for the acquired swapchain image and the TAA history carried
         * over from the previous frame; reads of these never flag as
         * unproduced during @ref validate.
         */
        void import_external(std::string_view resource);

        /**
         * @brief Appends @p p, which runs after every pass added before it,
         *        and returns it.
         *
         * The list owns the pass from here on; the returned reference stays
         * valid until @ref clear.
         */
        template<typename P>
        P& add(std::unique_ptr<P> p)
        {
            P& added = *p;
            m_passes.push_back(std::move(p));
            return added;
        }

        /**
         * @brief Validate the declared dependencies.
         *
         * Walks the passes in order, collecting each one's @ref
         * pass::declare_io; a read of a resource that no earlier pass wrote
         * and that was not imported is logged as an error. Returns true
         * when no hazard was found. Intended to be called once after the
         * list is built; it records nothing. The renderer treats a false
         * result as a programming error: it asserts in debug builds and
         * logs in release, since a mis-declared read is exactly the class
         * of ordering bug the validation exists to catch.
         */
        bool validate() const;

        /**
         * @brief Run every pass's @ref pass::prepare in order.
         *
         * The producer-before-consumer walk of the frame's per-pass
         * state: a pass reads what the passes before it prepared through
         * @p ctx. Called once per frame before @ref record.
         */
        void prepare(const frame_context& ctx) const;

        /**
         * @brief Record every pass into @p encoder in order, after
         *        @ref prepare.
         *
         * Each pass is wrapped in a debug group carrying its name (so a
         * graphics debugger shows the frame as a tree of passes) and, when
         * @p hooks is given, in its before / after calls. A pass that
         * records part of itself on worker threads (the scene pass) forks
         * and joins inside its own @ref pass::record, so the group and the
         * hooks bracket the whole of it.
         */
        void record(gpu::command_encoder& encoder, const frame_context& ctx, pass_hooks* hooks = nullptr) const;

        /** @brief Calls @ref pass::resize on every pass, in order. */
        void resize(uint32_t width, uint32_t height) const;

        /// Names of the passes, in recording order.
        std::vector<std::string> pass_names() const;

        /// Destroy every pass and drop the imported resources.
        void clear();

        /// Number of passes.
        size_t size() const noexcept
        {
            return m_passes.size();
        }

    private:
        std::vector<std::unique_ptr<pass>> m_passes;
        std::vector<std::string> m_external;
    };
} // namespace rendering_engine
