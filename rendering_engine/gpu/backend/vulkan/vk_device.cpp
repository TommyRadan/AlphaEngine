// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device.cpp
 * @brief @c vk_device lifecycle, instance / surface / device /
 *        swapchain bring-up, command-encoder factory, and the
 *        @c lookup_* accessors. Per-resource @c vk_device member
 *        functions live in their own translation units
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

    void vk_device::create_default_textures()
    {
        // Opaque white 1x1 placeholders. White is a neutral default for
        // the maps these stand in for (albedo / IBL etc. are gated by the
        // material's uniform flags, so the sampled value is unused — it
        // only has to be a valid resource of the right dimension).
        const uint32_t white = 0xFFFFFFFFu;

        texture_descriptor td2{};
        td2.dimension = texture_dimension::d2;
        td2.format = texture_format::rgba8_unorm;
        td2.width = 1;
        td2.height = 1;
        m_default_texture_2d = create_texture(td2);
        write_texture(m_default_texture_2d, &white, sizeof(white));

        texture_descriptor tdc{};
        tdc.dimension = texture_dimension::cube;
        tdc.format = texture_format::rgba8_unorm;
        tdc.width = 1;
        tdc.height = 1;
        m_default_texture_cube = create_texture(tdc);
        for (uint32_t face = 0; face < 6; ++face)
        {
            write_cube_face(m_default_texture_cube, static_cast<cube_face>(face), &white, sizeof(white));
        }
    }

    texture vk_device::default_texture(texture_dimension dim) const noexcept
    {
        return dim == texture_dimension::cube ? m_default_texture_cube : m_default_texture_2d;
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

        m_pipelines.for_each(
            [&](vk_pipeline& p)
            {
                for (auto& v : p.graphics_variants)
                {
                    if (v.object != VK_NULL_HANDLE)
                    {
                        vkDestroyPipeline(m_device.handle(), v.object, nullptr);
                        v.object = VK_NULL_HANDLE;
                    }
                }
                p.graphics_variants.clear();
                if (p.compute_object != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(m_device.handle(), p.compute_object, nullptr);
                    p.compute_object = VK_NULL_HANDLE;
                }
                if (p.layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(m_device.handle(), p.layout, nullptr);
                    p.layout = VK_NULL_HANDLE;
                }
            });
        m_bind_group_layouts.for_each(
            [&](vk_bind_group_layout& l)
            {
                if (l.object != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(m_device.handle(), l.object, nullptr);
                    l.object = VK_NULL_HANDLE;
                }
            });
        m_shader_modules.for_each(
            [&](vk_shader_module& s)
            {
                if (s.object != VK_NULL_HANDLE)
                {
                    vkDestroyShaderModule(m_device.handle(), s.object, nullptr);
                    s.object = VK_NULL_HANDLE;
                }
            });
        m_samplers.for_each(
            [&](vk_sampler& s)
            {
                if (s.object != VK_NULL_HANDLE)
                {
                    vkDestroySampler(m_device.handle(), s.object, nullptr);
                    s.object = VK_NULL_HANDLE;
                }
            });
        m_textures.for_each(
            [&](vk_texture& t)
            {
                if (t.default_sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(m_device.handle(), t.default_sampler, nullptr);
                    t.default_sampler = VK_NULL_HANDLE;
                }
                if (t.view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_device.handle(), t.view, nullptr);
                    t.view = VK_NULL_HANDLE;
                }
                for (VkImageView storage_view : t.storage_views)
                {
                    if (storage_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(m_device.handle(), storage_view, nullptr);
                    }
                }
                t.storage_views.clear();
                for (VkImageView attachment_view : t.attachment_views)
                {
                    if (attachment_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(m_device.handle(), attachment_view, nullptr);
                    }
                }
                t.attachment_views.clear();
                if (!t.external && t.image != VK_NULL_HANDLE)
                {
                    vmaDestroyImage(m_device.allocator(), t.image, t.allocation);
                }
                t.image = VK_NULL_HANDLE;
                t.allocation = VK_NULL_HANDLE;
            });
        m_buffers.for_each(
            [&](vk_buffer& b)
            {
                // The persistent map belongs to the allocation and goes
                // with it.
                if (b.object != VK_NULL_HANDLE)
                {
                    vmaDestroyBuffer(m_device.allocator(), b.object, b.allocation);
                }
                b.object = VK_NULL_HANDLE;
                b.allocation = VK_NULL_HANDLE;
                b.mapped = nullptr;
            });
        m_render_targets.for_each(
            [&](vk_render_target& rt)
            {
                for (auto& v : rt.variants)
                {
                    for (auto fb : v.framebuffers)
                    {
                        if (fb != VK_NULL_HANDLE)
                        {
                            vkDestroyFramebuffer(m_device.handle(), fb, nullptr);
                        }
                    }
                    v.framebuffers.clear();
                    if (v.render_pass != VK_NULL_HANDLE)
                    {
                        vkDestroyRenderPass(m_device.handle(), v.render_pass, nullptr);
                        v.render_pass = VK_NULL_HANDLE;
                    }
                }
                rt.variants.clear();
            });
        m_queries.destroy_all();

        m_pipelines.clear();
        m_shader_modules.clear();
        m_bind_group_layouts.clear();
        m_bind_groups.clear();
        m_samplers.clear();
        m_textures.clear();
        m_buffers.clear();
        m_render_targets.clear();

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

    // -- Physical / logical device --------------------------------------

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

    // -- Render targets / swapchain accessors ---------------------------

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
            retire_render_pass_variants(*swap, /*device_idle=*/true);
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

    void vk_device::retire_render_pass_variants(vk_render_target& target, bool device_idle)
    {
        if (target.variants.empty())
        {
            return;
        }
        std::vector<uint64_t> generations;
        std::vector<VkRenderPass> render_passes;
        std::vector<VkFramebuffer> framebuffers;
        generations.reserve(target.variants.size());
        render_passes.reserve(target.variants.size());
        for (auto& v : target.variants)
        {
            generations.push_back(v.render_pass_generation);
            if (v.render_pass != VK_NULL_HANDLE)
            {
                render_passes.push_back(v.render_pass);
            }
            for (VkFramebuffer fb : v.framebuffers)
            {
                if (fb != VK_NULL_HANDLE)
                {
                    framebuffers.push_back(fb);
                }
            }
        }
        target.variants.clear();

        // Every graphics pipeline that was bound inside one of these
        // passes cached a VkPipeline built against it. Purge those
        // entries now, matched by generation rather than by handle: a
        // driver may hand a later render pass the same handle value,
        // and a stale entry matched by handle would bind a pipeline
        // built for a pass that no longer exists.
        const auto is_retired = [&generations](const vk_pipeline::variant& pv)
        { return std::find(generations.begin(), generations.end(), pv.render_pass_generation) != generations.end(); };
        std::vector<VkPipeline> pipelines;
        m_pipelines.for_each(
            [&](vk_pipeline& p)
            {
                for (const auto& pv : p.graphics_variants)
                {
                    if (is_retired(pv) && pv.object != VK_NULL_HANDLE)
                    {
                        pipelines.push_back(pv.object);
                    }
                }
                p.graphics_variants.erase(
                    std::remove_if(p.graphics_variants.begin(), p.graphics_variants.end(), is_retired),
                    p.graphics_variants.end());
            });

        const VkDevice dev = m_device.handle();
        if (device_idle)
        {
            // Nothing is in flight and the caller is about to destroy
            // the image views these framebuffers were built on; free
            // them first so no framebuffer ever outlives its attachments.
            for (VkFramebuffer fb : framebuffers)
            {
                vkDestroyFramebuffer(dev, fb, nullptr);
            }
            framebuffers.clear();
        }
        // The render passes and pipelines may still be bound by the
        // previous frame's command buffer when a target is destroyed
        // mid-run (VUID-vkDestroyFramebuffer-framebuffer-00892 and
        // friends), so they always wait for the next fence wait.
        enqueue_destroy(
            [dev,
             framebuffers = std::move(framebuffers),
             render_passes = std::move(render_passes),
             pipelines = std::move(pipelines)]
            {
                for (VkPipeline p : pipelines)
                {
                    vkDestroyPipeline(dev, p, nullptr);
                }
                for (VkFramebuffer fb : framebuffers)
                {
                    vkDestroyFramebuffer(dev, fb, nullptr);
                }
                for (VkRenderPass rp : render_passes)
                {
                    vkDestroyRenderPass(dev, rp, nullptr);
                }
            });
    }

    render_target vk_device::create_render_target(const render_target_descriptor& descriptor)
    {
        if (const char* problem = validate_render_target_descriptor(descriptor); problem != nullptr)
        {
            LOG_ERR("create_render_target: %s", problem);
            return {};
        }
        if (descriptor.color.size() > m_limits.max_color_attachments)
        {
            LOG_ERR("create_render_target: %zu colour attachments, the device allows %u",
                    descriptor.color.size(),
                    m_limits.max_color_attachments);
            return {};
        }
        const sample_count_mask sample_counts =
            descriptor.color.empty()
                ? m_limits.depth_sample_counts
                : (m_limits.color_sample_counts & (descriptor.with_depth ? m_limits.depth_sample_counts : ~0u));
        if (!sample_count_supported(sample_counts, descriptor.sample_count))
        {
            LOG_ERR("create_render_target: %u samples per pixel are not supported for these attachments",
                    descriptor.sample_count);
            return {};
        }

        vk_render_target record{};
        record.is_swapchain = false;
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.samples = descriptor.sample_count;
        record.has_depth = descriptor.with_depth;

        // Textures allocated so far, released if a later step fails so
        // nothing half-built is handed out.
        std::vector<texture> allocated;
        const auto release_allocated = [&]
        {
            for (const texture t : allocated)
            {
                destroy(t);
            }
        };

        // Resolve one attachment: check an imported texture against the
        // target, or allocate a fresh single-mip texture of the target's
        // shape (colour attachments sample linearly, depth attachments
        // nearest, both clamped so a fullscreen pass never wraps at the
        // seam). The render pass and framebuffers are built lazily by
        // acquire_render_pass, per load-op combination.
        const auto resolve = [&](const attachment_desc& desc, bool depth, vk_attachment& out) -> bool
        {
            if (desc.texture.valid())
            {
                const vk_texture* tex = m_textures.lookup(desc.texture.id);
                if (tex == nullptr || tex->image == VK_NULL_HANDLE)
                {
                    LOG_ERR("create_render_target: imported attachment is not a live texture");
                    return false;
                }
                if ((tex->usage & texture_usage_render_attachment) == 0u)
                {
                    LOG_ERR("create_render_target: imported texture was created without "
                            "texture_usage_render_attachment");
                    return false;
                }
                if (tex->is_depth != depth)
                {
                    LOG_ERR("create_render_target: imported texture format does not fit a %s attachment",
                            depth ? "depth" : "colour");
                    return false;
                }
                if (desc.mip_level >= tex->mip_levels || desc.layer >= tex->array_layers)
                {
                    LOG_ERR("create_render_target: level %u / layer %u is outside the imported texture (%u levels, "
                            "%u layers)",
                            desc.mip_level,
                            desc.layer,
                            tex->mip_levels,
                            tex->array_layers);
                    return false;
                }
                if (tex->samples != descriptor.sample_count)
                {
                    LOG_ERR("create_render_target: imported texture has %u samples, the target %u",
                            tex->samples,
                            descriptor.sample_count);
                    return false;
                }
                const uint32_t level_width = std::max(1u, tex->width >> desc.mip_level);
                const uint32_t level_height = std::max(1u, tex->height >> desc.mip_level);
                if (level_width != descriptor.width || level_height != descriptor.height)
                {
                    LOG_ERR("create_render_target: imported level measures %ux%u, the target %ux%u",
                            level_width,
                            level_height,
                            descriptor.width,
                            descriptor.height);
                    return false;
                }
                out.tex = desc.texture;
                out.owned = false;
                out.mip_level = desc.mip_level;
                out.layer = desc.layer;
                return true;
            }

            texture_descriptor td{};
            td.dimension = descriptor.dimension;
            td.format = desc.format;
            td.width = descriptor.width;
            td.height = descriptor.height;
            td.array_layers = descriptor.array_layers;
            td.sample_count = descriptor.sample_count;
            td.mipmaps = false;
            td.usage = texture_usage_default | texture_usage_render_attachment;
            td.min_filter = depth ? filter_mode::nearest : filter_mode::linear;
            td.mag_filter = td.min_filter;
            td.mipmap_filter = mipmap_mode::none;
            td.address_u = address_mode::clamp_edge;
            td.address_v = address_mode::clamp_edge;
            td.address_w = address_mode::clamp_edge;
            const texture t = create_texture(td);
            if (!t.valid())
            {
                return false;
            }
            allocated.push_back(t);
            out.tex = t;
            out.owned = true;
            out.mip_level = 0;
            out.layer = desc.layer;
            return true;
        };

        record.color.resize(descriptor.color.size());
        for (size_t i = 0; i < descriptor.color.size(); ++i)
        {
            if (!resolve(descriptor.color[i], false, record.color[i]))
            {
                release_allocated();
                return {};
            }
        }
        if (descriptor.with_depth)
        {
            if (!resolve(descriptor.depth, true, record.depth))
            {
                release_allocated();
                return {};
            }
            if (const vk_texture* depth_tex = m_textures.lookup(record.depth.tex.id))
            {
                record.has_stencil = (depth_tex->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u;
            }
        }

        render_target h{};
        h.id = m_render_targets.insert(record);
        return h;
    }

    void vk_device::destroy(render_target handle)
    {
        if (handle.id == m_swapchain_target.id)
        {
            return;
        }
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            // The variants own framebuffers and render-pass objects
            // that may still be referenced by the previous frame's
            // command buffer, and pipelines were built against those
            // passes; retire them together through the deferred queue.
            retire_render_pass_variants(*record, /*device_idle=*/false);
            // Only the attachments the target allocated go with it; an
            // imported texture stays with its owner.
            for (vk_attachment& attachment : record->color)
            {
                if (attachment.owned && attachment.tex.valid())
                {
                    destroy(attachment.tex);
                }
                attachment = {};
            }
            if (record->depth.owned && record->depth.tex.valid())
            {
                destroy(record->depth.tex);
            }
            record->depth = {};
            m_render_targets.remove(handle.id);
        }
    }

    texture vk_device::render_target_color_texture(render_target handle, uint32_t index)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            if (index < record->color.size())
            {
                return record->color[index].tex;
            }
        }
        return {};
    }

    texture vk_device::render_target_depth_texture(render_target handle)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            return record->depth.tex;
        }
        return {};
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

    void vk_device::create_fallback_sampler()
    {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.minFilter = VK_FILTER_LINEAR;
        si.magFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.minLod = 0.0f;
        si.maxLod = VK_LOD_CLAMP_NONE;
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        si.maxAnisotropy = 1.0f;
        if (!vk_check(vkCreateSampler(m_device.handle(), &si, nullptr, &m_fallback_sampler),
                      "vkCreateSampler (fallback)"))
        {
            m_fallback_sampler = VK_NULL_HANDLE;
            throw std::runtime_error{"vkCreateSampler (fallback) failed"};
        }
    }

    // -- Render-pass cache ---------------------------------------------

    VkRenderPass vk_device::acquire_render_pass(vk_render_target& target,
                                                VkAttachmentLoadOp color_load,
                                                VkAttachmentLoadOp depth_load,
                                                bool use_depth)
    {
        vk_render_pass_key key{};
        key.color_load.fill(color_load);
        key.color_store.fill(VK_ATTACHMENT_STORE_OP_STORE);
        key.depth_load = depth_load;
        key.depth_store = VK_ATTACHMENT_STORE_OP_STORE;
        key.use_depth = use_depth;
        return acquire_render_pass(target, key);
    }

    VkRenderPass vk_device::acquire_render_pass(vk_render_target& target, const vk_render_pass_key& requested)
    {
        // The window backbuffer counts as one colour attachment. Key
        // entries past the attachment count are irrelevant, so they
        // are normalised away: two callers that agree on the used
        // attachments share the variant whatever they left in the rest.
        const uint32_t color_count = target.is_swapchain ? 1u : static_cast<uint32_t>(target.color.size());
        const bool variant_uses_depth = requested.use_depth && target.has_depth;
        vk_render_pass_key key = requested;
        key.use_depth = variant_uses_depth;
        for (uint32_t i = color_count; i < max_color_attachments; ++i)
        {
            key.color_load[i] = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            key.color_store[i] = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        if (!variant_uses_depth)
        {
            key.depth_load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            key.depth_store = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }

        // Reuse a matching variant if one already exists.
        for (const auto& v : target.variants)
        {
            if (v.key == key && v.render_pass != VK_NULL_HANDLE)
            {
                return v.render_pass;
            }
        }

        const VkSampleCountFlagBits samples = to_vk_sample_count(target.samples);

        std::array<VkAttachmentDescription, max_color_attachments + 1> attachments{};
        std::array<VkAttachmentReference, max_color_attachments> color_refs{};
        VkAttachmentReference depth_ref{};
        uint32_t attachment_count = 0;

        for (uint32_t i = 0; i < color_count; ++i)
        {
            VkAttachmentDescription& attachment = attachments[attachment_count];
            attachment = {};
            if (target.is_swapchain)
            {
                attachment.format = m_swapchain.surface_format();
            }
            else if (auto* color_tex = m_textures.lookup(target.color[i].tex.id))
            {
                attachment.format = color_tex->vk_format;
            }
            else
            {
                attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
            }
            attachment.samples = samples;
            attachment.loadOp = key.color_load[i];
            attachment.storeOp = key.color_store[i];
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout =
                key.color_load[i] == VK_ATTACHMENT_LOAD_OP_LOAD
                    ? (target.is_swapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    : VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout =
                target.is_swapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            color_refs[i].attachment = attachment_count;
            color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ++attachment_count;
        }

        if (variant_uses_depth)
        {
            VkAttachmentDescription& attachment = attachments[attachment_count];
            attachment = {};
            if (target.is_swapchain)
            {
                attachment.format = vk_format_for(m_swapchain.depth_format());
            }
            else if (auto* depth_tex = m_textures.lookup(target.depth.tex.id))
            {
                attachment.format = depth_tex->vk_format;
            }
            else
            {
                attachment.format = VK_FORMAT_D32_SFLOAT;
            }
            // Off-screen depth is sampled by later post passes (the velocity
            // pass reads sceneDepth, the lit materials the shadow maps), so
            // it leaves the pass in the shader-read layout the
            // combined-image-sampler descriptor expects, mirroring the
            // off-screen colour attachment above. A LOAD therefore resumes
            // from that same layout. The swapchain depth is never sampled,
            // so it stays in the attachment layout. A stencil plane, when
            // the format has one, loads and stores with depth.
            const VkImageLayout depth_rest_layout = target.is_swapchain
                                                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachment.samples = samples;
            attachment.loadOp = key.depth_load;
            attachment.storeOp = key.depth_store;
            attachment.stencilLoadOp = target.has_stencil ? key.depth_load : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = target.has_stencil ? key.depth_store : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout =
                key.depth_load == VK_ATTACHMENT_LOAD_OP_LOAD ? depth_rest_layout : VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = depth_rest_layout;
            depth_ref.attachment = attachment_count;
            depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachment_count;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = color_count;
        subpass.pColorAttachments = color_refs.data();
        subpass.pDepthStencilAttachment = variant_uses_depth ? &depth_ref : nullptr;

        // Two subpass dependencies. The EXTERNAL → 0 incoming dep
        // synchronises this pass's writes, and any LOAD it performs,
        // against a previous render pass's color/depth writes to the
        // same attachment — needed when consecutive passes target the
        // same off-screen or swapchain image. It covers both write
        // stages (late fragment tests for depth, since a depth write
        // only completes there) and both directions a LOAD can hazard
        // against a prior write: the automatic layout transition on
        // entry is itself a read of the old contents, and blending
        // reads the loaded color. Without this, syncval reports
        // READ_AFTER_WRITE against the layout transition whenever a
        // pass loads an attachment a previous pass wrote (skybox and
        // bloom's composite loading the HDR target, the UI and debug
        // passes loading the swapchain image). The 0 → EXTERNAL
        // outgoing dep releases color writes from this pass for
        // FRAGMENT_SHADER reads in a subsequent pass — without it
        // tonemap's fragment shader can sample the scene HDR target
        // before scene_pass's color writes are visible, and the
        // sample silently returns undefined data (typically zeros).
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstStageMask = deps[0].srcStageMask;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        // Release both colour and depth writes for a subsequent pass's
        // fragment-shader reads: tonemap samples the HDR colour and the
        // velocity pass samples the scene depth, so the depth write
        // (completed at late fragment tests) must be made visible too.
        deps[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = attachment_count;
        rpi.pAttachments = attachments.data();
        rpi.subpassCount = 1;
        rpi.pSubpasses = &subpass;
        rpi.dependencyCount = static_cast<uint32_t>(deps.size());
        rpi.pDependencies = deps.data();

        VkRenderPass new_render_pass = VK_NULL_HANDLE;
        const VkResult rp_result = vkCreateRenderPass(m_device.handle(), &rpi, nullptr, &new_render_pass);
        if (rp_result != VK_SUCCESS)
        {
            LOG_ERR("vkCreateRenderPass failed: %s (colour attachments=%u color_load[0]=%i depth_load=%i use_depth=%i)",
                    vk_result_to_string(rp_result),
                    color_count,
                    static_cast<int>(key.color_load[0]),
                    static_cast<int>(key.depth_load),
                    static_cast<int>(variant_uses_depth));
            return VK_NULL_HANDLE;
        }
        vk_render_target::variant new_variant{};
        new_variant.key = key;
        new_variant.render_pass = new_render_pass;
        new_variant.color_count = color_count;
        // The generation is what the pipeline cache keys on; it is
        // never reused, unlike the handle value the driver hands out.
        new_variant.render_pass_generation = m_next_render_pass_generation++;
        target.variants.push_back(std::move(new_variant));
        auto& v = target.variants.back();

        // Build per-variant framebuffers. Variants with different
        // use_depth aren't render-pass compatible (different
        // attachment counts), so they need their own framebuffer
        // sets — sharing one framebuffer across them would fail
        // vkCmdBeginRenderPass with INVALID_RENDER_PASS.
        if (target.is_swapchain)
        {
            // One framebuffer per (swapchain image, frame slot) pair,
            // since the depth attachment is the slot's; the encoder
            // picks by swapchain_framebuffer_index.
            const size_t image_count = m_swapchain.image_views().size();
            v.framebuffers.resize(image_count * m_frame.frames_in_flight(), VK_NULL_HANDLE);
            for (size_t i = 0; i < image_count; ++i)
            {
                for (uint32_t slot = 0; slot < m_frame.frames_in_flight(); ++slot)
                {
                    std::array<VkImageView, 2> views{m_swapchain.image_views()[i], m_swapchain.depth_view(slot)};
                    VkFramebufferCreateInfo fbi{};
                    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    fbi.renderPass = new_render_pass;
                    fbi.attachmentCount = variant_uses_depth ? 2 : 1;
                    fbi.pAttachments = views.data();
                    fbi.width = m_swapchain.extent().width;
                    fbi.height = m_swapchain.extent().height;
                    fbi.layers = 1;
                    VkFramebuffer& framebuffer = v.framebuffers[i * m_frame.frames_in_flight() + slot];
                    const VkResult fb_result = vkCreateFramebuffer(m_device.handle(), &fbi, nullptr, &framebuffer);
                    if (fb_result != VK_SUCCESS)
                    {
                        LOG_ERR("vkCreateFramebuffer (swapchain image %u, slot %u) failed: %s",
                                static_cast<unsigned>(i),
                                slot,
                                vk_result_to_string(fb_result));
                    }
                }
            }
        }
        else
        {
            // Every attachment renders through a single-level,
            // single-layer view of its texture: the mip and the layer /
            // cube face the target attached.
            v.framebuffers.resize(1, VK_NULL_HANDLE);
            std::array<VkImageView, max_color_attachments + 1> views{};
            uint32_t view_count = 0;
            bool views_ok = true;
            const auto add_view = [&](const vk_attachment& attachment)
            {
                VkImageView view = VK_NULL_HANDLE;
                if (auto* tex = m_textures.lookup(attachment.tex.id))
                {
                    view = attachment_image_view(*tex, attachment.mip_level, attachment.layer);
                }
                if (view == VK_NULL_HANDLE)
                {
                    views_ok = false;
                }
                views[view_count++] = view;
            };
            for (uint32_t i = 0; i < color_count; ++i)
            {
                add_view(target.color[i]);
            }
            if (variant_uses_depth)
            {
                add_view(target.depth);
            }
            if (!views_ok)
            {
                LOG_ERR("acquire_render_pass: an attachment of the off-screen target has no image view");
            }
            else
            {
                VkFramebufferCreateInfo fbi{};
                fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbi.renderPass = new_render_pass;
                fbi.attachmentCount = view_count;
                fbi.pAttachments = views.data();
                fbi.width = target.width;
                fbi.height = target.height;
                fbi.layers = 1;
                const VkResult fb_result = vkCreateFramebuffer(m_device.handle(), &fbi, nullptr, &v.framebuffers[0]);
                if (fb_result != VK_SUCCESS)
                {
                    LOG_ERR("vkCreateFramebuffer (offscreen) failed: %s", vk_result_to_string(fb_result));
                }
            }
        }

        return new_render_pass;
    }

    // -- Internal accessors --------------------------------------------

    vk_buffer* vk_device::lookup_buffer(buffer h)
    {
        return m_buffers.lookup(h.id);
    }
    vk_texture* vk_device::lookup_texture(texture h)
    {
        return m_textures.lookup(h.id);
    }
    vk_sampler* vk_device::lookup_sampler(sampler h)
    {
        return m_samplers.lookup(h.id);
    }
    vk_shader_module* vk_device::lookup_shader_module(shader_module h)
    {
        return m_shader_modules.lookup(h.id);
    }
    vk_pipeline* vk_device::lookup_pipeline(pipeline h)
    {
        return m_pipelines.lookup(h.id);
    }
    vk_bind_group* vk_device::lookup_bind_group(bind_group h)
    {
        return m_bind_groups.lookup(h.id);
    }
    vk_render_target* vk_device::lookup_render_target(render_target h)
    {
        return m_render_targets.lookup(h.id);
    }
    vk_bind_group_layout* vk_device::lookup_bind_group_layout(bind_group_layout h)
    {
        return m_bind_group_layouts.lookup(h.id);
    }
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
