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
 * @file shader_hot_reload.hpp
 * @brief Shader modules created from the shader library, and the
 *        debug-build hot reload that keeps them in step with the source
 *        tree.
 *
 * @ref create_library_shader_module is how the renderer turns a library
 * shader into a module: it compiles the variant and creates the module,
 * and in a debug build it also registers the module with the
 * @ref shader_hot_reload installed for the device, if any.
 *
 * The hot reload (debug builds only) watches the directory the shader
 * library's override root points at — the source tree's @c shaders/, or
 * @c ALPHAENGINE_SHADER_DIR — through a polling
 * @ref core::platform::directory_watcher, about once a second. When files
 * change it drops their cached text from the library, works out which
 * registered modules read one of them (their own source or anything they
 * transitively @c #include, see @ref shader_dependencies), recompiles
 * those and hands the new SPIR-V to @c device::reload_shader_modules,
 * which swaps it in behind the existing handles and rebuilds every
 * pipeline built from them — material-template variants, pass pipelines
 * and compute pipelines alike — releasing the old objects through the
 * backend's deferred destroy. A compile error leaves every pipeline as it
 * was: glslang's full message is logged, the changed files stay pending,
 * and the next change anywhere under the root retries them. Release
 * builds compile none of it.
 */

#pragma once

#include <string_view>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/gpu/types.hpp>

#if defined(_DEBUG)
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <core/platform/directory_watcher.hpp>
#endif

namespace rendering_engine::gpu
{
    struct device;

    /**
     * @brief Compile the library shader @p variant for @p stage and create
     *        its module on @p device.
     *
     * In a debug build, a module created while a @ref shader_hot_reload
     * is installed for @p device is registered with it, so an edit of its
     * source (or of anything it includes) reaches the module and every
     * pipeline built from it without a restart.
     *
     * @throws std::runtime_error when the compile fails (see
     *         @ref compile_library_shader).
     */
    shader_module create_library_shader_module(device& device, const shader_variant& variant, shader_stage stage);

    /** @brief @ref create_library_shader_module for @p path with no defines. */
    shader_module create_library_shader_module(device& device, std::string_view path, shader_stage stage);

#if defined(_DEBUG)
    /**
     * @brief Debug-build shader hot reload over one device (see the file
     *        comment).
     *
     * Constructing one installs it as the reload
     * @ref create_library_shader_module registers modules with; at most
     * one is installed at a time, and destroying it uninstalls it. The
     * renderer owns one for its device between @c context::init and
     * @c context::quit and calls @ref poll once per frame, before the
     * frame opens. Main-thread only.
     */
    struct shader_hot_reload
    {
        /** @brief Rescans happen at most this often. */
        static constexpr std::chrono::milliseconds poll_interval{1000};

        /**
         * @param device The device whose modules are reloaded; must
         *        outlive this object.
         * @param root   The directory to watch: the shader library's
         *        override root, whose files the library reads.
         */
        shader_hot_reload(device& device, std::filesystem::path root);
        ~shader_hot_reload();

        shader_hot_reload(const shader_hot_reload&) = delete;
        shader_hot_reload& operator=(const shader_hot_reload&) = delete;

        /** @brief The installed reload, or null. */
        static shader_hot_reload* active();

        /** @brief The device this reload targets. */
        device& target_device() const;

        /**
         * @brief Register @p module as compiled from @p variant for
         *        @p stage. Called by @ref create_library_shader_module.
         */
        void track(shader_module module, const shader_variant& variant, shader_stage stage);

        /**
         * @brief Rescan the watched tree when @ref poll_interval has passed
         *        since the last scan and reload what changed. Call between
         *        frames.
         */
        void poll();

        /** @brief Modules registered and not yet found destroyed. */
        std::size_t tracked_count() const;

    private:
        struct tracked_module
        {
            shader_module module{};
            shader_variant variant;
            shader_stage stage{shader_stage::vertex};
        };

        // Recompile every live module that reads a pending file and swap
        // the results in; clears m_pending once that succeeded.
        void reload_pending();

        device* m_device{nullptr};
        core::platform::directory_watcher m_watcher;
        // The watched root as a generic path string ending in '/', the
        // prefix stripped from a change to get its library path.
        std::string m_root_prefix;
        std::chrono::steady_clock::time_point m_last_scan;
        std::vector<tracked_module> m_modules;
        // Library paths changed since the last reload that went through.
        std::set<std::string> m_pending;
    };
#endif
} // namespace rendering_engine::gpu
