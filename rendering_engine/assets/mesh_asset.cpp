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

#include <rendering_engine/assets/mesh_asset.hpp>

#include <rendering_engine/gpu/device.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine
{
    mesh_asset::~mesh_asset()
    {
        // Free in the reverse of the create order used by the cache, matching
        // the premade renderables' teardown. The device outlives the cache, so
        // it is always valid here.
        auto& gpu = *runtime::current_engine().gpu;
        if (index_buffer.valid())
        {
            gpu.destroy(index_buffer);
        }
        if (vertex_buffer.valid())
        {
            gpu.destroy(vertex_buffer);
        }
    }
} // namespace rendering_engine
