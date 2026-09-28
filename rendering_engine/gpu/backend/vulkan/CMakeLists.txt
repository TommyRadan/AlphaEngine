# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(alpha_rhi PRIVATE
    vk_allocator.cpp
    vk_allocator.hpp
    vk_check.cpp
    vk_check.hpp
    vk_command_encoder.cpp
    vk_command_encoder.hpp
    vk_descriptor_allocator.cpp
    vk_descriptor_allocator.hpp
    vk_device.cpp
    vk_device.hpp
    vk_device_buffer.cpp
    vk_device_pipeline.cpp
    vk_device_render_target.cpp
    vk_device_resources.cpp
    vk_device_shader.cpp
    vk_device_texture.cpp
    vk_factory.cpp
    vk_frame.cpp
    vk_frame.hpp
    vk_instance.cpp
    vk_instance.hpp
    vk_logical_device.cpp
    vk_logical_device.hpp
    vk_negotiate.cpp
    vk_negotiate.hpp
    vk_overlay_renderer.cpp
    vk_physical_device.cpp
    vk_physical_device.hpp
    vk_pipeline_cache.cpp
    vk_pipeline_cache.hpp
    vk_query_pool.cpp
    vk_query_pool.hpp
    vk_render_pass_cache.cpp
    vk_render_pass_cache.hpp
    vk_resources.hpp
    vk_staging_ring.cpp
    vk_staging_ring.hpp
    vk_swapchain.cpp
    vk_swapchain.hpp
    vk_transfer.cpp
    vk_transfer.hpp
    vk_translate.cpp
    vk_translate.hpp
)
