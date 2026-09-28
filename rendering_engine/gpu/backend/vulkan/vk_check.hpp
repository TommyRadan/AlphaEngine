// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_check.hpp
 * @brief How the Vulkan backend reports a @c VkResult: the name of a
 *        result for log messages, and the check every call whose
 *        non-success codes are all failures goes through.
 */

#pragma once

#include <vulkan/vulkan.h>

namespace rendering_engine::gpu::backend::vulkan
{
    // Best-effort VkResult → human-readable string, used in error
    // logs so the user can pinpoint a failing call without validation
    // layers.
    const char* vk_result_to_string(VkResult r);

    // True for VK_SUCCESS. Anything else logs "<what> failed: <result>"
    // at error level and returns false, so a call site reads
    // `if (!vk_check(vkFoo(...), "vkFoo")) { bail; }` instead of
    // discarding the result. Only for calls whose every non-success
    // code is a failure; a call with informational codes
    // (VK_SUBOPTIMAL_KHR, VK_INCOMPLETE) inspects its result itself.
    bool vk_check(VkResult result, const char* what);
} // namespace rendering_engine::gpu::backend::vulkan
