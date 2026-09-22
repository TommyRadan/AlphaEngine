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

#include <rendering_engine/assets/asset_device.hpp>

#include <core/log.hpp>

#include <cstdlib>

namespace rendering_engine
{
    namespace
    {
        // The device the asset layer creates/frees resources against. Installed
        // by the engine for the device's lifetime; null outside a run (or in a
        // test before a fake is installed).
        gpu::device* g_asset_device = nullptr;
    } // namespace

    gpu::device& asset_device()
    {
        if (g_asset_device == nullptr)
        {
            // An asset created or destroyed with no device installed — before
            // engine init or after shutdown — is a lifetime bug. It must fail
            // loudly in every configuration rather than become a silent null
            // dereference in Release, so this is not a compiled-out assert.
            // LOG_FTL aborts the process; the explicit abort() only keeps the
            // control flow obvious to readers and static analysis.
            LOG_FTL("asset_device() called with no gpu device installed (before init or after shutdown)");
            std::abort();
        }
        return *g_asset_device;
    }

    void set_asset_device(gpu::device* device)
    {
        g_asset_device = device;
    }
} // namespace rendering_engine
