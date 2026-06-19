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
 * @file frame_graph.hpp
 * @brief A declarative view over the engine's ordered render-pass list.
 *
 * Each pass declares the logical resources it reads and writes (by stable
 * name) through @ref pass_io_builder. The @ref frame_graph collects those
 * declarations, validates that every read is produced by an earlier pass or
 * imported as an external resource, and executes the passes.
 *
 * Execution order is the registration order, which is exactly the order the
 * engine used before adopting the graph, so the graph does not change what is
 * rendered — it adds dependency validation and one place that owns resource
 * identity. That is the foundation a later step builds on to derive barriers
 * automatically and alias transient targets; today those remain the passes'
 * own responsibility (render-pass implicit transitions + the precise barriers
 * the gpu backend emits).
 */

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rendering_engine
{
    struct frame_context;

    namespace gpu
    {
        struct command_encoder;
    }
} // namespace rendering_engine

namespace rendering_engine::render_graph
{
    /**
     * @brief Collects one pass's declared resource reads and writes.
     *
     * Passes name resources with stable strings (e.g. "scene_color") so they
     * never have to thread graph handles through their interfaces; the graph
     * resolves the names. A pass that declares nothing is an ordering-only
     * node — still executed in place, just invisible to the dependency check.
     */
    class pass_io_builder
    {
    public:
        void read(std::string_view resource);
        void write(std::string_view resource);

        const std::vector<std::string>& reads() const noexcept
        {
            return m_reads;
        }
        const std::vector<std::string>& writes() const noexcept
        {
            return m_writes;
        }

    private:
        std::vector<std::string> m_reads;
        std::vector<std::string> m_writes;
    };

    /**
     * @brief Ordered pass graph: validate declared dependencies, then execute.
     */
    class frame_graph
    {
    public:
        using execute_fn = std::function<void(gpu::command_encoder&, const frame_context&)>;

        /**
         * @brief Mark @p resource as valid at frame start with no in-frame
         *        producer.
         *
         * Used for the acquired swapchain image and the TAA history carried
         * over from the previous frame; reads of these never flag as
         * unproduced during @ref compile.
         */
        void import_external(std::string_view resource);

        /**
         * @brief Register a pass in execution order with its declared I/O and
         *        the callback that records it.
         */
        void add_pass(std::string name, pass_io_builder io, execute_fn execute);

        /**
         * @brief Validate declared dependencies.
         *
         * Walks the passes in registration order; a read of a resource that no
         * earlier pass wrote and that was not imported is logged as a hazard.
         * Returns true when no hazard was found. Intended to be called once
         * after the graph is built; it does not alter execution.
         */
        bool compile();

        /**
         * @brief Record every pass into @p encoder in registration order.
         */
        void execute(gpu::command_encoder& encoder, const frame_context& ctx) const;

        /**
         * @brief Record passes in the half-open range [begin, end) into
         *        @p encoder, in registration order.
         *
         * Used by the parallel path, which slices the frame into contiguous
         * ranges and records each into its own encoder. @p end is clamped to
         * the pass count.
         */
        void execute_range(gpu::command_encoder& encoder, const frame_context& ctx, size_t begin, size_t end) const;

        /// Drop all passes and imported resources.
        void clear();

        /// Number of registered passes.
        size_t pass_count() const noexcept
        {
            return m_nodes.size();
        }

    private:
        struct node
        {
            std::string name;
            pass_io_builder io;
            execute_fn execute;
        };

        std::vector<node> m_nodes;
        std::vector<std::string> m_external;
    };
} // namespace rendering_engine::render_graph
