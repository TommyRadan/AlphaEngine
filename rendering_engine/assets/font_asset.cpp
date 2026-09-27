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

#include <rendering_engine/assets/font_asset.hpp>

#include <cstddef>

#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine
{
    font_asset::font_asset(const std::string& filename, float size) : font{filename, size}
    {
        const util::image& pixels = font.atlas();

        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = gpu::texture_format::rgba8_unorm;
        descriptor.width = pixels.get_width();
        descriptor.height = pixels.get_height();
        descriptor.mipmaps = false;
        descriptor.min_filter = gpu::filter_mode::linear;
        descriptor.mag_filter = gpu::filter_mode::linear;
        descriptor.address_u = gpu::address_mode::clamp_edge;
        descriptor.address_v = gpu::address_mode::clamp_edge;
        descriptor.address_w = gpu::address_mode::clamp_edge;

        auto& gpu = asset_device();
        atlas = gpu.create_texture(descriptor);
        const std::size_t pixel_bytes = static_cast<std::size_t>(pixels.get_width()) *
                                        static_cast<std::size_t>(pixels.get_height()) * sizeof(util::color);
        gpu.write_texture(atlas, pixels.get_pixels(), pixel_bytes);
    }

    font_asset::~font_asset()
    {
        // The engine tears the asset cache (and the renderables holding
        // these handles) down ahead of the gpu device, so the device is
        // installed when this runs.
        if (atlas.valid())
        {
            asset_device().destroy(atlas);
        }
    }
} // namespace rendering_engine
