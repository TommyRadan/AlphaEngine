// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file command_encoder.cpp
 * @brief Anchor for the @ref rendering_engine::gpu::command_encoder
 *        and @ref rendering_engine::gpu::render_pass_encoder abstract
 *        interfaces — defines the out-of-line virtual destructors so
 *        the vtables and typeinfos are emitted in this single
 *        translation unit instead of every TU that includes
 *        @c command_encoder.hpp.
 */

#include <rendering_engine/gpu/command_encoder.hpp>

namespace rendering_engine::gpu
{
    render_pass_encoder::~render_pass_encoder() = default;

    compute_pass_encoder::~compute_pass_encoder() = default;

    command_encoder::~command_encoder() = default;
} // namespace rendering_engine::gpu
