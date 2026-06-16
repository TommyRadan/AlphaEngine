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
 * down. The pointer the asset layer reads is therefore exactly the engine's
 * device in a normal run, but a test can install a lightweight fake device and
 * exercise the cache with no engine, window, or backend present.
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
     * Undefined behaviour if called before a device has been installed via
     * @ref set_asset_device. In a normal run the engine installs it during
     * initialisation, before any asset is loaded.
     */
    gpu::device& asset_device();

    /**
     * @brief Installs (or clears, with @c nullptr) the device the asset layer
     *        uses. Called by the engine around the device lifetime; tests may
     *        install a fake device.
     */
    void set_asset_device(gpu::device* device);
} // namespace rendering_engine
