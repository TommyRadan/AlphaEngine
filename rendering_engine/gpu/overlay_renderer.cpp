// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file overlay_renderer.cpp
 * @brief Anchor for the @ref rendering_engine::gpu::overlay_renderer
 *        abstract interface — defines the out-of-line virtual destructor
 *        so the vtable and typeinfo are emitted in this single
 *        translation unit instead of every TU that includes
 *        @c overlay_renderer.hpp.
 */

#include <rendering_engine/gpu/overlay_renderer.hpp>

namespace rendering_engine::gpu
{
    overlay_renderer::~overlay_renderer() = default;
} // namespace rendering_engine::gpu
