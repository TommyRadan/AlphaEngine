// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_allocator.cpp
 * @brief The one translation unit that compiles the Vulkan Memory
 *        Allocator's implementation. Nothing else lives here: the
 *        header is third-party code fetched at configure time and
 *        stays outside the format / tidy scope.
 */

#define VMA_IMPLEMENTATION
#include <rendering_engine/gpu/backend/vulkan/vk_allocator.hpp>
