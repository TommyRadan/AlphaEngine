// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
