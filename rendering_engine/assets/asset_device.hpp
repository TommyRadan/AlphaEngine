// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file asset_device.hpp
 * @brief The gpu device the asset layer creates and frees resources against.
 *
 * The asset cache and the reference-counted asset handles (@ref texture_asset,
 * @ref mesh_asset) need a @ref gpu::device to upload and release GPU
 * resources, including from their destructors which run at arbitrary points in
 * the program. Rather than reach into the @c runtime::engine global directly —
 * which would couple the whole asset layer (and anything that links it) to the
 * engine, the window, and both gpu backends — they go through this tiny
 * accessor.
 *
 * The engine installs the live device via @ref set_asset_device once the
 * renderer has brought the device up, and clears it as the device is torn
 * down, so the pointer the asset layer reads is exactly the engine's device
 * for as long as the engine runs.
 */

#pragma once

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    /**
     * @brief The device the asset layer uses for resource creation/destruction.
     *
     * Aborts the process (through a fatal log) in every build configuration if
     * called with no device installed — before @ref set_asset_device or after
     * it was cleared — so a lifetime bug surfaces loudly instead of
     * dereferencing null in Release. In a normal run the engine installs it
     * during initialisation, before any asset is loaded.
     */
    gpu::device& asset_device();

    /**
     * @brief Installs (or clears, with @c nullptr) the device the asset layer
     *        uses. Called by the engine around the device lifetime.
     */
    void set_asset_device(gpu::device* device);
} // namespace rendering_engine
