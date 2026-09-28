// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device.cpp
 * @brief @c vk_device: the bring-up and teardown of its components,
 *        the capabilities it reports, the frame boundary, submission
 *        and swapchain rebuild that order the components' work, and the
 *        accessors the encoder reaches them through. The resource
 *        families live in their own translation units
 *        (vk_device_buffer.cpp, etc.).
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_command_encoder.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    vk_device::vk_device() = default;

    vk_device::~vk_device()
    {
        if (m_initialised)
        {
            quit();
        }
    }

    void vk_device::init(const surface_desc& surface, uint32_t frames_in_flight)
    {
        LOG_INF("Init gpu::backend::vulkan::vk_device");

        // Fallback extent for a surface that leaves the size to the
        // application (see choose_swapchain_extent): the drawable's
        // pixel size, which is what the swapchain follows on a scaled
        // display.
        m_swapchain.set_window_size(surface.width, surface.height);
        m_swapchain.set_vsync(surface.vsync);

        m_frame.set_frames_in_flight(frames_in_flight);

        m_instance.create_instance(surface.vulkan_instance_extensions);
        m_instance.load_debug_utils_functions();
        m_instance.create_debug_messenger();
        m_instance.create_surface(surface);
        m_physical_device.pick_physical_device(m_instance);
        m_physical_device.resolve_depth_formats();
        m_device.create_logical_device(m_features);
        query_capabilities();
        m_pipeline_cache.create(m_physical_device.handle(), m_device.handle());
        m_device.create_allocator();
        m_transfer.create_command_pool();
        m_frame.create_command_pools();
        if (!m_transfer.create_staging_ring())
        {
            throw std::runtime_error{"vk_device::init: staging ring allocation failed"};
        }
        if (!m_descriptors.create_descriptor_pool())
        {
            throw std::runtime_error{"vkCreateDescriptorPool failed"};
        }
        create_fallback_sampler();

        // Bring-up builds the swapchain strictly: a surface that has no
        // usable extent yet (a window created minimised) or a failed
        // build is fatal here, unlike in the render loop, where the same
        // conditions suspend presentation until a rebuild succeeds. The
        // suspended path relies on a live swapchain target and surface
        // format having been established once.
        VkSurfaceCapabilitiesKHR caps{};
        const VkResult caps_result =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device.handle(), m_instance.surface(), &caps);
        if (caps_result != VK_SUCCESS)
        {
            LOG_ERR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %s", vk_result_to_string(caps_result));
            throw std::runtime_error{"vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed"};
        }
        const VkExtent2D extent = m_swapchain.choose_extent(caps);
        if (extent.width == 0 || extent.height == 0 || !m_swapchain.create_swapchain(caps, extent))
        {
            throw std::runtime_error{"vk_device::init: swapchain creation failed"};
        }
        m_frame.create_sync_objects();

        vk_render_target swap{};
        swap.is_swapchain = true;
        swap.width = m_swapchain.extent().width;
        swap.height = m_swapchain.extent().height;
        swap.samples = 1;
        swap.has_depth = true;
        swap.has_stencil =
            (aspect_for_vk_format(vk_format_for(m_swapchain.depth_format())) & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u;
        m_swapchain_target.id = m_render_targets.insert(swap);

        create_default_textures();

        LOG_INF("Vulkan frames in flight: %u", m_frame.frames_in_flight());
        m_initialised = true;
    }

    void vk_device::quit()
    {
        if (!m_initialised)
        {
            return;
        }
        if (m_device.handle() != VK_NULL_HANDLE && !m_device.device_lost())
        {
            // A lost device has nothing left to wait for; its objects
            // may still be destroyed, which is all that follows.
            m_device.check_queue_result(vkDeviceWaitIdle(m_device.handle()), "vkDeviceWaitIdle (quit)");
        }
        // Every pipeline this run built is in the cache by now; write it
        // out before anything is torn down.
        m_pipeline_cache.save_and_destroy(m_device.handle(), m_device.device_lost());
        // Idle or lost: nothing executes any more, so every deferred
        // destroy — including those stamped with a submission that
        // never happened — may run.
        m_frame.abandon_frame();
        m_frame.note_device_drained();

        if (m_default_texture_2d.valid())
        {
            destroy(m_default_texture_2d);
            m_default_texture_2d = {};
        }
        if (m_default_texture_cube.valid())
        {
            destroy(m_default_texture_cube);
            m_default_texture_cube = {};
        }

        // GPU is idle (or gone): every transfer batch has run or never
        // will, so none gates a deferred destroy any more; anything
        // queued by destroy() during the run is now safe to actually
        // free, and the active handle pools below need to release
        // whatever is still resident.
        m_transfer.discard_transfer_batches();
        m_frame.drain_pending_destroys();

        destroy_resource_tables();
        m_queries.destroy_all();

        m_frame.destroy_sync_objects();
        m_swapchain.destroy_swapchain();

        // Every descriptor set was freed above (or belongs to a leaked
        // bind group), so the whole chain goes at once.
        m_descriptors.destroy();
        if (m_fallback_sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_device.handle(), m_fallback_sampler, nullptr);
            m_fallback_sampler = VK_NULL_HANDLE;
        }
        // The batches' dedicated staging buffers and the ring are VMA
        // allocations, so both go before the allocator, which goes
        // before the device.
        m_transfer.destroy_command_pool();
        m_frame.destroy_command_pools();
        m_transfer.destroy_staging_ring();
        m_device.destroy_allocator();
        m_device.destroy();
        m_instance.shutdown();

        m_swapchain.reset();
        m_frame.reset();
        m_physical_device.reset();
        m_transfer.reset();
        m_device.reset();
        m_device_lost_thrown = false;
        m_features = {};
        m_limits = {};
        m_initialised = false;
        LOG_INF("Quit gpu::backend::vulkan::vk_device");
    }

    // -- Capabilities ----------------------------------------------------

    void vk_device::query_capabilities()
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device.handle(), &props);
        const VkPhysicalDeviceLimits& limits = props.limits;

        m_limits = {};
        m_limits.max_texture_size_2d = limits.maxImageDimension2D;
        m_limits.max_texture_size_3d = limits.maxImageDimension3D;
        m_limits.max_texture_size_cube = limits.maxImageDimensionCube;
        m_limits.max_array_layers = limits.maxImageArrayLayers;
        m_limits.max_color_attachments = limits.maxColorAttachments;
        m_limits.uniform_buffer_offset_alignment = static_cast<uint32_t>(limits.minUniformBufferOffsetAlignment);
        m_limits.storage_buffer_offset_alignment = static_cast<uint32_t>(limits.minStorageBufferOffsetAlignment);
        // VkSampleCountFlags is bit-per-count, the engine's mask too.
        m_limits.color_sample_counts = static_cast<sample_count_mask>(limits.framebufferColorSampleCounts);
        m_limits.depth_sample_counts = static_cast<sample_count_mask>(limits.framebufferDepthSampleCounts);
        m_limits.max_anisotropy = m_features.sampler_anisotropy ? limits.maxSamplerAnisotropy : 1.0f;
        m_limits.timestamp_period_ns = limits.timestampPeriod > 0.0f ? limits.timestampPeriod : 1.0f;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            m_limits.max_compute_workgroup_count[axis] = limits.maxComputeWorkGroupCount[axis];
        }
        m_limits.max_compute_workgroup_invocations = limits.maxComputeWorkGroupInvocations;
        m_limits.max_push_constants_size = limits.maxPushConstantsSize;

        // The grants recorded by create_logical_device stay; the rest
        // is what every Vulkan device has, plus the two extension-
        // backed ones.
        m_features.compute = true;
        m_features.indirect_draw = true;
        m_features.timestamp_queries = m_physical_device.timestamp_valid_bits() > 0 && limits.timestampPeriod > 0.0f;
        m_features.debug_labels = m_instance.debug_utils_enabled() && m_instance.object_names_loaded();
        // Compute pipelines, storage-image bind groups and the layout
        // transitions the IBL convolution needs are implemented, so
        // the GPU prefilter path is taken.
        m_features.compute_prefilter = true;
        // Every pipeline that draws a renderable declares the 128-byte
        // PerDraw push-constant range. The spec guarantees
        // min_push_constants_size bytes, so only a non-conformant device
        // falls short, and it could create none of those pipelines.
        if (limits.maxPushConstantsSize < min_push_constants_size)
        {
            LOG_FTL("Vulkan maxPushConstantsSize is %u, below the required %u bytes",
                    limits.maxPushConstantsSize,
                    min_push_constants_size);
            throw std::runtime_error{"Vulkan device offers too few push-constant bytes"};
        }

        LOG_INF("Vulkan limits: texture %u / 3d %u / cube %u, %u array layers, %u colour attachments, "
                "anisotropy %.0f, msaa colour 0x%x depth 0x%x, timestamps %s (%.2f ns/tick), debug labels %s, "
                "push constants %u bytes",
                m_limits.max_texture_size_2d,
                m_limits.max_texture_size_3d,
                m_limits.max_texture_size_cube,
                m_limits.max_array_layers,
                m_limits.max_color_attachments,
                static_cast<double>(m_limits.max_anisotropy),
                m_limits.color_sample_counts,
                m_limits.depth_sample_counts,
                m_features.timestamp_queries ? "on" : "off",
                static_cast<double>(m_limits.timestamp_period_ns),
                m_features.debug_labels ? "on" : "off",
                m_limits.max_push_constants_size);
    }

    texture_usage vk_device::format_support(texture_format format) const
    {
        if (m_physical_device.handle() == VK_NULL_HANDLE)
        {
            return 0;
        }
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(m_physical_device.handle(), vk_format_for(format), &props);
        return to_texture_usage(props.optimalTilingFeatures, is_depth_format(format));
    }

    // -- Swapchain -------------------------------------------------------

    render_target vk_device::swapchain_target()
    {
        return m_swapchain_target;
    }

    void vk_device::resize_swapchain(uint32_t width, uint32_t height)
    {
        m_swapchain.set_window_size(width, height);
        if (!m_initialised || m_device.handle() == VK_NULL_HANDLE)
        {
            return;
        }
        if (width == 0 || height == 0)
        {
            // Minimised. Nothing is torn down: the existing swapchain
            // stays (possibly out of date) as the oldSwapchain for the
            // rebuild that resumes presentation, once a later hint or
            // the per-frame surface poll reports a real extent.
            m_swapchain.suspend_swapchain("the window reports a 0x0 backbuffer (minimised)", false);
            return;
        }
        // The renderer's window_resized listener is the one caller
        // (plus renderer::init, once, with the drawable's pixel size). A
        // hint that matches the live swapchain is a no-op — which is
        // how a resize the acquire or present already recovered from
        // avoids a second rebuild. Anything else — a different size,
        // or a size arriving while suspended — rebuilds against the
        // surface's current capabilities; the hint itself only matters
        // when the surface leaves the extent to us.
        if (!m_swapchain.suspended() && m_swapchain.handle() != VK_NULL_HANDLE && width == m_swapchain.extent().width &&
            height == m_swapchain.extent().height)
        {
            return;
        }
        recreate_swapchain();
    }

    bool vk_device::swapchain_suspended() const noexcept
    {
        return m_swapchain.suspended();
    }

    bool vk_device::recreate_swapchain()
    {
        if (m_device.handle() == VK_NULL_HANDLE || m_instance.surface() == VK_NULL_HANDLE || m_device.device_lost())
        {
            return false;
        }
        // The surface, not the cached window size, is the authority on
        // the extent. An OS-driven out-of-date (display change, a
        // compositor decision) arrives without any resize hint, so
        // rebuilding at the cached size would leave the swapchain out
        // of date for every following acquire.
        VkSurfaceCapabilitiesKHR caps{};
        const VkResult caps_result =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device.handle(), m_instance.surface(), &caps);
        if (caps_result != VK_SUCCESS)
        {
            m_swapchain.suspend_swapchain(vk_result_to_string(caps_result), true);
            return false;
        }
        const VkExtent2D extent = m_swapchain.choose_extent(caps);
        if (extent.width == 0 || extent.height == 0)
        {
            // Minimised: vkCreateSwapchainKHR rejects a zero extent, so
            // keep whatever swapchain exists (it becomes oldSwapchain
            // on resume) and stop acquiring. begin_frame polls the
            // surface until this branch stops being taken.
            m_swapchain.suspend_swapchain("the surface extent is 0x0 (window minimised)", false);
            return false;
        }

        // Everything that references the old images has to be idle:
        // every frame in flight, and the present that may still be
        // reading the image it was handed. A rebuild is rare, so the
        // full drain is affordable.
        if (!m_device.check_queue_result(vkDeviceWaitIdle(m_device.handle()), "vkDeviceWaitIdle (swapchain rebuild)"))
        {
            m_swapchain.suspend_swapchain("the device could not be waited idle before the rebuild", true);
            return false;
        }
        // The idle wait covered every frame the fences track and every
        // submitted transfer batch; the batch still recording, if any,
        // was never submitted and stays open.
        m_frame.note_device_idle();
        m_transfer.retire_transfer_batches();

        // The swapchain target's framebuffers point at image views that
        // are about to go, and its render passes at a format / sample
        // count the new swapchain need not share; retire them (and the
        // pipelines built against them) so the next acquire_render_pass
        // rebuilds against the new images.
        auto* swap = m_render_targets.lookup(m_swapchain_target.id);
        if (swap != nullptr)
        {
            m_render_passes.retire_render_pass_variants(*swap, /*device_idle=*/true);
        }

        if (!m_swapchain.create_swapchain(caps, extent))
        {
            // create_swapchain released the old swapchain (passing it
            // as oldSwapchain retired it whether or not the call
            // succeeded), so there is nothing to present into until a
            // later poll succeeds; the next one starts from scratch.
            m_swapchain.suspend_swapchain("the swapchain could not be rebuilt", true);
            return false;
        }
        if (swap != nullptr)
        {
            swap->width = m_swapchain.extent().width;
            swap->height = m_swapchain.extent().height;
        }
        m_swapchain.resume();
        return true;
    }

    // -- Query sets ----------------------------------------------------

    query_set vk_device::create_query_set(const query_set_descriptor& descriptor)
    {
        return m_queries.create_query_set(descriptor, m_features.timestamp_queries);
    }

    void vk_device::destroy(query_set handle)
    {
        m_queries.destroy(handle);
    }

    bool vk_device::resolve_queries(query_set set, uint32_t first, uint32_t count, uint64_t* out_ticks)
    {
        return m_queries.resolve_queries(set, first, count, out_ticks);
    }

    // -- Debug names ---------------------------------------------------

    void vk_device::set_debug_name(buffer handle, const char* name)
    {
        if (const auto* record = m_buffers.lookup(handle.id))
        {
            m_instance.name_object(
                m_device.handle(), VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(record->object), name);
        }
    }

    void vk_device::set_debug_name(texture handle, const char* name)
    {
        if (const auto* record = m_textures.lookup(handle.id))
        {
            m_instance.name_object(
                m_device.handle(), VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(record->image), name);
            m_instance.name_object(
                m_device.handle(), VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(record->view), name);
        }
    }

    void vk_device::set_debug_name(sampler handle, const char* name)
    {
        if (const auto* record = m_samplers.lookup(handle.id))
        {
            m_instance.name_object(
                m_device.handle(), VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(record->object), name);
        }
    }

    void vk_device::set_debug_name(pipeline handle, const char* name)
    {
        // The VkPipeline objects are built lazily per render pass, so
        // the layout — shared by every variant — carries the name.
        if (const auto* record = m_pipelines.lookup(handle.id))
        {
            m_instance.name_object(
                m_device.handle(), VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<uint64_t>(record->layout), name);
            m_instance.name_object(
                m_device.handle(), VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(record->compute_object), name);
        }
    }

    void vk_device::set_debug_name(render_target handle, const char* name)
    {
        // A target is its attachments to a debugger: name the images the
        // target itself allocated (an imported texture keeps its own).
        const auto* record = m_render_targets.lookup(handle.id);
        if (record == nullptr || record->is_swapchain)
        {
            return;
        }
        for (const vk_attachment& attachment : record->color)
        {
            if (attachment.owned)
            {
                set_debug_name(attachment.tex, name);
            }
        }
        if (record->depth.owned)
        {
            set_debug_name(record->depth.tex, name);
        }
    }

    // -- Command recording ---------------------------------------------

    std::unique_ptr<command_encoder> vk_device::create_command_encoder()
    {
        return std::make_unique<vk_command_encoder>(*this);
    }

    void vk_device::submit(std::unique_ptr<command_encoder> encoder)
    {
        auto* vk_enc = static_cast<vk_command_encoder*>(encoder.get());
        if (vk_enc == nullptr)
        {
            encoder.reset();
            return;
        }
        if (m_device.device_lost())
        {
            // Nothing can execute any more; end_frame raises the loss
            // to the main loop. The command buffer belongs to the frame
            // pool and is reclaimed with it.
            encoder.reset();
            return;
        }
        // The command buffer is owned by the frame pool, not the
        // encoder, so every early return below simply leaves it for the
        // pool reset at the next begin_frame.
        VkCommandBuffer cmd = vk_enc->release_command_buffer();
        encoder.reset();
        if (cmd == VK_NULL_HANDLE)
        {
            return;
        }
        if (!vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer"))
        {
            return;
        }

        // Every upload recorded since the last flush goes first in
        // queue order, with the batch's trailing barrier making the
        // copies visible to this command buffer. A failed flush is
        // logged there; the frame still runs.
        m_transfer.flush_transfer_batch();

        if (!m_swapchain.have_current_image())
        {
            m_frame.submit_without_image(cmd);
            return;
        }
        if (!m_frame.submit_with_image(cmd, m_swapchain.render_finished()))
        {
            // Nothing signals the render-finished semaphore now, so the
            // frame is not presented (the present would wait forever on
            // it).
            return;
        }
        m_swapchain.note_submitted(m_frame.frame_slot());
    }

    void vk_device::flush_pending_destroys()
    {
        if (m_device.handle() == VK_NULL_HANDLE || m_device.device_lost())
        {
            return;
        }
        m_device.check_queue_result(vkDeviceWaitIdle(m_device.handle()), "vkDeviceWaitIdle (flush_pending_destroys)");
        // Idle: nothing executes any more, so every deferred destroy —
        // including one stamped for a submission that never happened —
        // may run now, same reasoning as quit()'s own final drain.
        m_frame.note_device_drained();
        m_transfer.retire_transfer_batches();
        m_frame.drain_pending_destroys();
    }

    void vk_device::acquire_swapchain_image()
    {
        m_swapchain.acquire_swapchain_image();
    }

    // -- Frame boundary ------------------------------------------------

    void vk_device::begin_frame()
    {
        if (!m_initialised || m_device.device_lost())
        {
            // A lost device has no frame to wait for; the renderer's
            // recording is dropped by submit and end_frame raises the
            // loss.
            return;
        }
        // Wait for the frame that last recorded into this slot, before
        // anything is written for the new one (see vk_frame::begin).
        if (!m_frame.begin() && m_device.device_lost())
        {
            return;
        }
        // Every transfer batch submitted before that frame completed
        // ahead of its fence in queue order, so polling reclaims them
        // (ring bytes, dedicated staging buffers); one submitted since
        // — a ring-full flush during loading between frames — may
        // still be executing and is left alone, as are the deferred
        // destroys it gates.
        m_transfer.retire_transfer_batches();
        if (m_device.device_lost())
        {
            return;
        }
        // Nothing from this slot's pool is pending any more: reclaim
        // the command buffers for this frame's encoders.
        m_frame.reset_frame_command_pool();
        // Everything enqueued for destruction up to the submission the
        // fence retired is no longer referenced by the GPU — and no
        // command buffer is open yet that could reference what a
        // material rebuilds this frame. This is the one in-frame point
        // where freeing is safe (entries a later submission or a live
        // transfer batch still gates stay queued).
        m_frame.drain_pending_destroys();

        if (m_swapchain.suspended())
        {
            // The surface reported no extent, or the last rebuild
            // failed: poll the surface once per frame and resume as
            // soon as it has a usable extent. The main loop already
            // skips whole frames while the window says it is
            // minimised, so this is reached when the surface lags the
            // window (a restore whose first frame still measures 0x0)
            // or after a failed rebuild. recreate_swapchain stays
            // suspended, silently, while the extent is still 0x0.
            recreate_swapchain();
        }
    }

    void vk_device::end_frame()
    {
        if (!m_initialised)
        {
            return;
        }
        m_swapchain.present();

        if (m_device.device_lost() && !m_device_lost_thrown)
        {
            // Raised exactly once, from the frame boundary, so the main
            // loop's failure path shows the message and tears the
            // engine down in order. A transparent re-initialisation
            // would need every GPU resource the renderer holds to be
            // re-creatable; the per-frame bookkeeping above is left
            // consistent for quit(). Later frames are no-ops.
            m_device_lost_thrown = true;
            throw std::runtime_error{"Vulkan device lost"};
        }

        m_frame.end();
    }

    // -- Render-pass cache ---------------------------------------------

    VkRenderPass vk_device::acquire_render_pass(vk_render_target& target,
                                                VkAttachmentLoadOp color_load,
                                                VkAttachmentLoadOp depth_load,
                                                bool use_depth)
    {
        return m_render_passes.acquire_render_pass(target, color_load, depth_load, use_depth);
    }

    VkRenderPass vk_device::acquire_render_pass(vk_render_target& target, const vk_render_pass_key& key)
    {
        return m_render_passes.acquire_render_pass(target, key);
    }

    // -- Internal accessors --------------------------------------------

    vk_query_set* vk_device::lookup_query_set(query_set h)
    {
        return m_queries.lookup_query_set(h);
    }
    PFN_vkCmdBeginDebugUtilsLabelEXT vk_device::cmd_begin_debug_label() const noexcept
    {
        return m_instance.cmd_begin_debug_label();
    }
    PFN_vkCmdEndDebugUtilsLabelEXT vk_device::cmd_end_debug_label() const noexcept
    {
        return m_instance.cmd_end_debug_label();
    }
    VkInstance vk_device::instance() const noexcept
    {
        return m_instance.handle();
    }
    VkDevice vk_device::vk_handle() const noexcept
    {
        return m_device.handle();
    }
    VkPhysicalDevice vk_device::physical_device() const noexcept
    {
        return m_physical_device.handle();
    }
    VkQueue vk_device::graphics_queue() const noexcept
    {
        return m_device.graphics_queue();
    }
    uint32_t vk_device::graphics_queue_family() const noexcept
    {
        return m_physical_device.graphics_queue_family();
    }
    VmaAllocator vk_device::allocator() const noexcept
    {
        return m_device.allocator();
    }
    VkPipelineCache vk_device::pipeline_cache() const noexcept
    {
        return m_pipeline_cache.handle();
    }
    bool vk_device::device_lost() const noexcept
    {
        return m_device.device_lost();
    }
    VkFormat vk_device::vk_format_for(texture_format format) const noexcept
    {
        return m_physical_device.vk_format_for(format);
    }
    uint64_t vk_device::swapchain_generation() const noexcept
    {
        return m_swapchain.generation();
    }
    uint32_t vk_device::swapchain_image_count() const noexcept
    {
        return m_swapchain.image_count();
    }
    uint32_t vk_device::current_swapchain_image_index() const noexcept
    {
        return m_swapchain.current_image_index();
    }
    bool vk_device::have_current_swapchain_image() const noexcept
    {
        return m_swapchain.have_current_image();
    }
    uint32_t vk_device::swapchain_framebuffer_index(uint32_t image_index) const noexcept
    {
        return image_index * m_frame.frames_in_flight() + m_frame.frame_slot();
    }
    uint32_t vk_device::frames_in_flight() const noexcept
    {
        return m_frame.frames_in_flight();
    }
    uint32_t vk_device::frame_slot() const noexcept
    {
        return m_frame.frame_slot();
    }
    bool vk_device::extended_dynamic_state_enabled() const noexcept
    {
        return m_device.extended_dynamic_state_enabled();
    }
    PFN_vkCmdBindVertexBuffers2EXT vk_device::cmd_bind_vertex_buffers2() const noexcept
    {
        return m_device.cmd_bind_vertex_buffers2();
    }

    VkCommandBuffer vk_device::acquire_frame_command_buffer()
    {
        return m_frame.acquire_frame_command_buffer();
    }
    VkCommandBuffer vk_device::acquire_secondary_command_buffer(uint32_t lane)
    {
        return m_frame.acquire_secondary_command_buffer(lane);
    }
    void vk_device::enqueue_destroy(std::function<void()> fn)
    {
        m_frame.enqueue_destroy(std::move(fn));
    }
    void vk_device::note_render_pass_opened(bool is_swapchain, bool use_depth)
    {
        m_frame.note_render_pass_opened(is_swapchain, use_depth);
    }
    void vk_device::note_draw(uint32_t vertex_count)
    {
        m_frame.note_draw(vertex_count);
    }
    void vk_device::note_draw_indexed(uint32_t index_count)
    {
        m_frame.note_draw_indexed(index_count);
    }
    void vk_device::note_draws(uint32_t draws, uint32_t vertices, uint32_t draws_indexed, uint32_t indices)
    {
        m_frame.note_draws(draws, vertices, draws_indexed, indices);
    }
} // namespace rendering_engine::gpu::backend::vulkan
