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
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <platform/platform.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_command_encoder.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>
#include <rendering_engine/graphics_settings.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    const char* vk_result_to_string(VkResult r)
    {
        switch (r)
        {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
        default:
            return "VK_<unknown>";
        }
    }

    bool vk_check(VkResult result, const char* what)
    {
        if (result == VK_SUCCESS)
        {
            return true;
        }
        LOG_ERR("%s failed: %s", what, vk_result_to_string(result));
        return false;
    }

    namespace
    {
        constexpr const char* k_validation_layer = "VK_LAYER_KHRONOS_validation";

        // Spelled out rather than taken from the header so the build
        // does not depend on a Vulkan SDK new enough to define them:
        // VK_KHR_portability_enumeration arrived in header 1.3.216 and
        // VK_KHR_portability_subset sits behind VK_ENABLE_BETA_EXTENSIONS.
        // The flag value is VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR.
        constexpr const char* k_portability_enumeration_extension = "VK_KHR_portability_enumeration";
        constexpr VkInstanceCreateFlags k_enumerate_portability_flag = 0x00000001;
        constexpr const char* k_portability_subset_extension = "VK_KHR_portability_subset";

        bool layer_available(const char* name)
        {
            uint32_t count = 0;
            if (!vk_check(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties"))
            {
                return false;
            }
            std::vector<VkLayerProperties> layers(count);
            const VkResult r = vkEnumerateInstanceLayerProperties(&count, layers.data());
            if (r != VK_SUCCESS && r != VK_INCOMPLETE)
            {
                vk_check(r, "vkEnumerateInstanceLayerProperties");
                return false;
            }
            for (const auto& layer : layers)
            {
                if (std::strcmp(layer.layerName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // Whether the instance (or, with @p layer, that layer) exposes
        // the extension @p name. Every instance extension the backend
        // asks for beyond the window system's mandatory set goes through
        // here first, so vkCreateInstance never fails on an optional one.
        bool instance_extension_available(const char* name, const char* layer = nullptr)
        {
            uint32_t count = 0;
            if (!vk_check(vkEnumerateInstanceExtensionProperties(layer, &count, nullptr),
                          "vkEnumerateInstanceExtensionProperties"))
            {
                return false;
            }
            std::vector<VkExtensionProperties> extensions(count);
            const VkResult r = vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data());
            if (r != VK_SUCCESS && r != VK_INCOMPLETE)
            {
                vk_check(r, "vkEnumerateInstanceExtensionProperties");
                return false;
            }
            for (const auto& extension : extensions)
            {
                if (std::strcmp(extension.extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // The highest instance-level API version the loader supports.
        // vkEnumerateInstanceVersion is a 1.1 entry point: a 1.0 loader
        // has no symbol for it, so it is resolved through
        // vkGetInstanceProcAddr and its absence means 1.0.
        uint32_t probe_instance_version()
        {
            auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
                vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
            if (enumerate == nullptr)
            {
                return VK_API_VERSION_1_0;
            }
            uint32_t version = VK_API_VERSION_1_0;
            if (enumerate(&version) != VK_SUCCESS)
            {
                return VK_API_VERSION_1_0;
            }
            return version;
        }

        const char* depth_format_name(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_D16_UNORM:
                return "D16_UNORM";
            case VK_FORMAT_X8_D24_UNORM_PACK32:
                return "X8_D24_UNORM_PACK32";
            case VK_FORMAT_D32_SFLOAT:
                return "D32_SFLOAT";
            case VK_FORMAT_D16_UNORM_S8_UINT:
                return "D16_UNORM_S8_UINT";
            case VK_FORMAT_D24_UNORM_S8_UINT:
                return "D24_UNORM_S8_UINT";
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return "D32_SFLOAT_S8_UINT";
            default:
                return "<not a depth format>";
            }
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                      void* /*user_data*/)
        {
            if (data == nullptr || data->pMessage == nullptr)
            {
                return VK_FALSE;
            }
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                LOG_ERR("Vulkan: %s", data->pMessage);
            }
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                LOG_WRN("Vulkan: %s", data->pMessage);
            }
            else
            {
                LOG_INF("Vulkan: %s", data->pMessage);
            }
            return VK_FALSE;
        }

        VkSurfaceFormatKHR pick_surface_format(VkPhysicalDevice gpu, VkSurfaceKHR surface)
        {
            uint32_t count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data());
            for (const auto& f : formats)
            {
                if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    return f;
                }
            }
            return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                   : formats.front();
        }

        VkPresentModeKHR pick_present_mode(VkPhysicalDevice gpu, VkSurfaceKHR surface, bool vsync_enabled)
        {
            uint32_t count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, nullptr);
            std::vector<VkPresentModeKHR> modes(count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, modes.data());

            const auto supports = [&modes](VkPresentModeKHR wanted)
            { return std::find(modes.begin(), modes.end(), wanted) != modes.end(); };

            if (vsync_enabled)
            {
                // FIFO is always available and locks to the refresh rate.
                return VK_PRESENT_MODE_FIFO_KHR;
            }

            // Vsync off: prefer IMMEDIATE (uncapped, may tear); fall back to
            // MAILBOX (low-latency triple buffering) and finally the
            // guaranteed FIFO when neither is exposed.
            if (supports(VK_PRESENT_MODE_IMMEDIATE_KHR))
            {
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            if (supports(VK_PRESENT_MODE_MAILBOX_KHR))
            {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        // The extent the next swapchain has to be built with. The
        // surface dictates it through currentExtent; only when the
        // surface leaves the choice to the application (UINT32_MAX —
        // some Wayland compositors) does the last window size the
        // engine reported count, clamped to what the surface allows.
        // A 0x0 result means the window is minimised: Vulkan rejects
        // a zero-sized swapchain, so the caller must not build one.
        VkExtent2D
        choose_swapchain_extent(const VkSurfaceCapabilitiesKHR& caps, uint32_t window_width, uint32_t window_height)
        {
            if (caps.currentExtent.width != UINT32_MAX)
            {
                return caps.currentExtent;
            }
            VkExtent2D extent{};
            extent.width = std::clamp(window_width, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(window_height, caps.minImageExtent.height, caps.maxImageExtent.height);
            return extent;
        }
    } // namespace

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
        m_window_width = surface.width;
        m_window_height = surface.height;
        m_vsync = surface.vsync;

        // The slot ring is sized once, here: the command pools, sync
        // objects, swapchain depth images and every dynamic buffer's
        // regions follow it, so it cannot change while the device is
        // up. The settings layer already clamps the value to the range;
        // the clamp here guards any other caller.
        static_assert(rendering_engine::graphics_settings::max_frames_in_flight == k_max_frames_in_flight,
                      "the settings range and the backend ring must agree");
        m_frames_in_flight = std::clamp<uint32_t>(frames_in_flight, 1, k_max_frames_in_flight);
        m_frame_slot = 0;
        m_in_frame = false;
        m_submit_serial = 0;
        m_completed_submit_serial = 0;

        create_instance(surface.vulkan_instance_extensions);
        load_debug_utils_functions();
        create_debug_messenger();
        create_surface(surface);
        pick_physical_device();
        resolve_depth_formats();
        create_logical_device();
        query_capabilities();
        create_pipeline_cache();
        create_allocator();
        create_command_pools();
        if (!create_staging_ring())
        {
            throw std::runtime_error{"vk_device::init: staging ring allocation failed"};
        }
        if (!create_descriptor_pool())
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
        const VkResult caps_result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &caps);
        if (caps_result != VK_SUCCESS)
        {
            LOG_ERR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %s", vk_result_to_string(caps_result));
            throw std::runtime_error{"vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed"};
        }
        const VkExtent2D extent = choose_swapchain_extent(caps, m_window_width, m_window_height);
        if (extent.width == 0 || extent.height == 0 || !create_swapchain(caps, extent))
        {
            throw std::runtime_error{"vk_device::init: swapchain creation failed"};
        }
        create_sync_objects();

        vk_render_target swap{};
        swap.is_swapchain = true;
        swap.width = m_swapchain_extent.width;
        swap.height = m_swapchain_extent.height;
        swap.samples = 1;
        swap.has_depth = true;
        swap.has_stencil =
            (aspect_for_vk_format(vk_format_for(m_swapchain_depth_format)) & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u;
        m_swapchain_target.id = m_render_targets.insert(swap);

        create_default_textures();

        LOG_INF("Vulkan frames in flight: %u", m_frames_in_flight);
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
        if (m_device != VK_NULL_HANDLE && !m_device_lost)
        {
            // A lost device has nothing left to wait for; its objects
            // may still be destroyed, which is all that follows.
            check_queue_result(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle (quit)");
        }
        // Every pipeline this run built is in the cache by now; write it
        // out before anything is torn down.
        save_and_destroy_pipeline_cache();
        // Idle or lost: nothing executes any more, so every deferred
        // destroy — including those stamped with a submission that
        // never happened — may run.
        m_in_frame = false;
        note_device_idle();
        m_completed_submit_serial = UINT64_MAX;

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
        discard_transfer_batches();
        drain_pending_destroys();

        m_pipelines.for_each(
            [&](vk_pipeline& p)
            {
                for (auto& v : p.graphics_variants)
                {
                    if (v.object != VK_NULL_HANDLE)
                    {
                        vkDestroyPipeline(m_device, v.object, nullptr);
                        v.object = VK_NULL_HANDLE;
                    }
                }
                p.graphics_variants.clear();
                if (p.compute_object != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(m_device, p.compute_object, nullptr);
                    p.compute_object = VK_NULL_HANDLE;
                }
                if (p.layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(m_device, p.layout, nullptr);
                    p.layout = VK_NULL_HANDLE;
                }
            });
        m_bind_group_layouts.for_each(
            [&](vk_bind_group_layout& l)
            {
                if (l.object != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(m_device, l.object, nullptr);
                    l.object = VK_NULL_HANDLE;
                }
            });
        m_shader_modules.for_each(
            [&](vk_shader_module& s)
            {
                if (s.object != VK_NULL_HANDLE)
                {
                    vkDestroyShaderModule(m_device, s.object, nullptr);
                    s.object = VK_NULL_HANDLE;
                }
            });
        m_samplers.for_each(
            [&](vk_sampler& s)
            {
                if (s.object != VK_NULL_HANDLE)
                {
                    vkDestroySampler(m_device, s.object, nullptr);
                    s.object = VK_NULL_HANDLE;
                }
            });
        m_textures.for_each(
            [&](vk_texture& t)
            {
                if (t.default_sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(m_device, t.default_sampler, nullptr);
                    t.default_sampler = VK_NULL_HANDLE;
                }
                if (t.view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_device, t.view, nullptr);
                    t.view = VK_NULL_HANDLE;
                }
                for (VkImageView storage_view : t.storage_views)
                {
                    if (storage_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(m_device, storage_view, nullptr);
                    }
                }
                t.storage_views.clear();
                for (VkImageView attachment_view : t.attachment_views)
                {
                    if (attachment_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(m_device, attachment_view, nullptr);
                    }
                }
                t.attachment_views.clear();
                if (!t.external && t.image != VK_NULL_HANDLE)
                {
                    vmaDestroyImage(m_allocator, t.image, t.allocation);
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
                    vmaDestroyBuffer(m_allocator, b.object, b.allocation);
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
                            vkDestroyFramebuffer(m_device, fb, nullptr);
                        }
                    }
                    v.framebuffers.clear();
                    if (v.render_pass != VK_NULL_HANDLE)
                    {
                        vkDestroyRenderPass(m_device, v.render_pass, nullptr);
                        v.render_pass = VK_NULL_HANDLE;
                    }
                }
                rt.variants.clear();
            });
        m_query_sets.for_each(
            [&](vk_query_set& q)
            {
                if (q.pool != VK_NULL_HANDLE)
                {
                    vkDestroyQueryPool(m_device, q.pool, nullptr);
                    q.pool = VK_NULL_HANDLE;
                }
            });

        m_pipelines.clear();
        m_shader_modules.clear();
        m_bind_group_layouts.clear();
        m_bind_groups.clear();
        m_samplers.clear();
        m_textures.clear();
        m_buffers.clear();
        m_render_targets.clear();
        m_query_sets.clear();

        destroy_sync_objects();
        destroy_swapchain();

        // Every descriptor set was freed above (or belongs to a leaked
        // bind group), so the whole chain goes at once.
        for (VkDescriptorPool pool : m_descriptor_pools)
        {
            if (pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_device, pool, nullptr);
            }
        }
        m_descriptor_pools.clear();
        if (m_fallback_sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_device, m_fallback_sampler, nullptr);
            m_fallback_sampler = VK_NULL_HANDLE;
        }
        // The batches' dedicated staging buffers and the ring are VMA
        // allocations, so both go before the allocator, which goes
        // before the device.
        destroy_command_pools();
        destroy_staging_ring();
        destroy_allocator();
        if (m_device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        if (m_surface != VK_NULL_HANDLE)
        {
            if (m_destroy_surface != nullptr)
            {
                m_destroy_surface(m_instance, &m_surface);
            }
            m_surface = VK_NULL_HANDLE;
        }
        m_destroy_surface = nullptr;
        destroy_debug_messenger();
        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }

        m_have_current_image = false;
        m_acquire_attempted = false;
        m_present_pending = false;
        m_in_flight_fence_armed.fill(false);
        m_fence_submit_serial.fill(0);
        m_submit_serial = 0;
        m_completed_submit_serial = 0;
        m_frame_slot = 0;
        m_frames_in_flight = 1;
        m_in_frame = false;
        m_next_transfer_batch_id = 1;
        m_swapchain_suspended = false;
        m_device_lost = false;
        m_device_lost_thrown = false;
        m_has_portability_subset = false;
        m_features = {};
        m_limits = {};
        m_debug_utils_enabled = false;
        m_cmd_begin_debug_label = nullptr;
        m_cmd_end_debug_label = nullptr;
        m_set_debug_object_name = nullptr;
        m_timestamp_valid_bits = 0;
        m_depth_formats.fill(VK_FORMAT_UNDEFINED);
        m_initialised = false;
        LOG_INF("Quit gpu::backend::vulkan::vk_device");
    }

    // -- Instance / debug messenger / surface ---------------------------

    namespace
    {
        // The Vulkan loader does not look beside the executable for
        // explicit-layer JSON manifests by default. CI ships the
        // validation layer alongside @c AlphaEngine.exe (via the
        // build-vulkan job in @c ci.yml); pointing @c VK_LAYER_PATH at
        // the executable directory before @c vkCreateInstance lets the
        // loader discover @c VkLayer_khronos_validation.json from
        // there. Skip if @c VK_LAYER_PATH is already set so the user
        // can override with their own SDK install.
        void publish_layer_path_if_bundled()
        {
            if (core::os::environment_variable("VK_LAYER_PATH").has_value())
            {
                return;
            }
            // Without a trailing separator, so the loader's path
            // concatenation produces a well-formed lookup.
            const std::filesystem::path base_path = platform::base_path();
            if (base_path.empty() || !core::os::file_exists(base_path / "VkLayer_khronos_validation.json"))
            {
                return;
            }
            const std::string layer_path = core::os::path_to_utf8(base_path);
            platform::set_environment_variable("VK_LAYER_PATH", layer_path.c_str());
            LOG_INF("Published VK_LAYER_PATH=%s for bundled validation layer", layer_path.c_str());
        }
    } // namespace

    void vk_device::create_instance(const std::vector<const char*>& window_extensions)
    {
        publish_layer_path_if_bundled();

        // The backend needs the 1.1 core feature queries
        // (vkGetPhysicalDeviceFeatures2) and asks for up to 1.2, which
        // is what it was written against; a newer loader is asked for
        // 1.2 (it accepts any version it supports), an older one for
        // exactly what it has, and a 1.0 loader is refused outright
        // rather than failing later inside vkCreateInstance.
        const uint32_t instance_version = probe_instance_version();
        if (instance_version < VK_API_VERSION_1_1)
        {
            LOG_FTL("Vulkan loader supports API %u.%u only; the Vulkan backend needs 1.1 or newer "
                    "(update the graphics driver / Vulkan runtime)",
                    VK_VERSION_MAJOR(instance_version),
                    VK_VERSION_MINOR(instance_version));
            throw std::runtime_error{"Vulkan 1.1 or newer is required"};
        }
        const uint32_t api_version = std::min<uint32_t>(instance_version, VK_API_VERSION_1_2);

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "AlphaEngine";
        app.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
        app.pEngineName = "AlphaEngine";
        app.engineVersion = VK_MAKE_VERSION(0, 0, 1);
        app.apiVersion = api_version;

        std::vector<const char*> extensions = window_extensions;
        VkInstanceCreateFlags flags = 0;
        // A layered implementation (MoltenVK on macOS / iOS) only
        // enumerates its non-conformant physical devices when the
        // application opts in with the portability extension + flag;
        // without them vkEnumeratePhysicalDevices finds nothing.
        const bool portability_enumeration = instance_extension_available(k_portability_enumeration_extension);
        if (portability_enumeration)
        {
            extensions.push_back(k_portability_enumeration_extension);
            flags |= k_enumerate_portability_flag;
        }
        std::vector<const char*> layers;
        // VK_EXT_debug_utils carries the validation layer's messages
        // and, independently of validation, the command labels and
        // object names a graphics debugger shows. It is requested
        // whenever the instance exposes it, confirmed first so a
        // missing extension never fails vkCreateInstance.
        m_debug_utils_enabled = instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#ifdef _DEBUG
        if (layer_available(k_validation_layer))
        {
            // The layer itself may be what provides the extension.
            if (!m_debug_utils_enabled &&
                instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, k_validation_layer))
            {
                m_debug_utils_enabled = true;
            }
            if (m_debug_utils_enabled)
            {
                layers.push_back(k_validation_layer);
                m_validation_enabled = true;
            }
            else
            {
                LOG_WRN("Vulkan validation layer is available but VK_EXT_debug_utils is not; "
                        "debug-build run will not be validated");
            }
        }
        else
        {
            LOG_WRN("Vulkan validation layer not available; debug-build run will not be validated");
        }
#endif
        if (m_debug_utils_enabled)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.flags = flags;
        info.pApplicationInfo = &app;
        info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        const VkResult create_result = vkCreateInstance(&info, nullptr, &m_instance);
        if (create_result != VK_SUCCESS)
        {
            LOG_FTL("vkCreateInstance failed: %s", vk_result_to_string(create_result));
            throw std::runtime_error{"vkCreateInstance failed"};
        }
        m_api_version = api_version;
        LOG_INF("Vulkan instance: loader %u.%u.%u, requested api %u.%u, portability enumeration %s, validation %s, "
                "debug utils %s",
                VK_VERSION_MAJOR(instance_version),
                VK_VERSION_MINOR(instance_version),
                VK_VERSION_PATCH(instance_version),
                VK_VERSION_MAJOR(api_version),
                VK_VERSION_MINOR(api_version),
                portability_enumeration ? "on" : "off",
                m_validation_enabled ? "on" : "off",
                m_debug_utils_enabled ? "on" : "off");
    }

    void vk_device::load_debug_utils_functions()
    {
        m_cmd_begin_debug_label = nullptr;
        m_cmd_end_debug_label = nullptr;
        m_set_debug_object_name = nullptr;
        if (!m_debug_utils_enabled || m_instance == VK_NULL_HANDLE)
        {
            return;
        }
        m_cmd_begin_debug_label = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT"));
        m_cmd_end_debug_label = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT"));
        m_set_debug_object_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectNameEXT"));
        // Labels and names go together: a loader that resolves only
        // part of the extension gets neither, so the feature flag is
        // an honest answer.
        if (m_cmd_begin_debug_label == nullptr || m_cmd_end_debug_label == nullptr ||
            m_set_debug_object_name == nullptr)
        {
            LOG_WRN("VK_EXT_debug_utils is enabled but its label entry points did not resolve; "
                    "debug groups and object names are off");
            m_cmd_begin_debug_label = nullptr;
            m_cmd_end_debug_label = nullptr;
            m_set_debug_object_name = nullptr;
        }
    }

    void vk_device::name_object(VkObjectType type, uint64_t object_handle, const char* name)
    {
        if (m_set_debug_object_name == nullptr || m_device == VK_NULL_HANDLE || object_handle == 0 || name == nullptr)
        {
            return;
        }
        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = object_handle;
        info.pObjectName = name;
        m_set_debug_object_name(m_device, &info);
    }

    void vk_device::create_debug_messenger()
    {
        if (!m_validation_enabled)
        {
            return;
        }
        auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create_fn == nullptr)
        {
            return;
        }
        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = debug_callback;
        if (create_fn(m_instance, &info, nullptr, &m_debug_messenger) != VK_SUCCESS)
        {
            m_debug_messenger = VK_NULL_HANDLE;
        }
    }

    void vk_device::destroy_debug_messenger()
    {
        if (m_debug_messenger == VK_NULL_HANDLE)
        {
            return;
        }
        auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_fn != nullptr)
        {
            destroy_fn(m_instance, m_debug_messenger, nullptr);
        }
        m_debug_messenger = VK_NULL_HANDLE;
    }

    void vk_device::create_surface(const surface_desc& surface)
    {
        if (surface.native_window == nullptr || surface.create_vulkan_surface == nullptr)
        {
            LOG_FTL("vk_device::create_surface: window subsystem missing");
            throw std::runtime_error{"vk_device: window missing"};
        }
        if (!surface.create_vulkan_surface(surface.native_window, m_instance, &m_surface))
        {
            // The window system has logged its reason.
            throw std::runtime_error{"vk_device: window surface creation failed"};
        }
        m_destroy_surface = surface.destroy_vulkan_surface;
    }

    // -- Physical / logical device --------------------------------------

    void vk_device::pick_physical_device()
    {
        uint32_t count = 0;
        if (!vk_check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "vkEnumeratePhysicalDevices") ||
            count == 0)
        {
            throw std::runtime_error{"no Vulkan-capable GPUs"};
        }
        std::vector<VkPhysicalDevice> gpus(count);
        const VkResult enumerate_result = vkEnumeratePhysicalDevices(m_instance, &count, gpus.data());
        if (enumerate_result != VK_SUCCESS && enumerate_result != VK_INCOMPLETE)
        {
            vk_check(enumerate_result, "vkEnumeratePhysicalDevices");
            throw std::runtime_error{"vkEnumeratePhysicalDevices failed"};
        }

        VkPhysicalDevice best = VK_NULL_HANDLE;
        bool best_discrete = false;
        uint32_t best_graphics = 0;
        uint32_t best_present = 0;
        uint32_t best_timestamp_bits = 0;

        for (auto gpu : gpus)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(gpu, &props);
            // The device-level 1.1 functionality the backend relies on
            // (vkGetPhysicalDeviceFeatures2 against this device) is
            // gated by the physical device's own version, not the
            // instance's.
            if (props.apiVersion < VK_API_VERSION_1_1)
            {
                LOG_WRN("Vulkan GPU %s skipped: api %u.%u, the backend needs 1.1",
                        props.deviceName,
                        VK_VERSION_MAJOR(props.apiVersion),
                        VK_VERSION_MINOR(props.apiVersion));
                continue;
            }

            uint32_t qf_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qf_count, nullptr);
            std::vector<VkQueueFamilyProperties> qfs(qf_count);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qf_count, qfs.data());
            std::optional<uint32_t> graphics_family;
            std::optional<uint32_t> present_family;
            for (uint32_t i = 0; i < qf_count; ++i)
            {
                if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && !graphics_family.has_value())
                {
                    graphics_family = i;
                }
                VkBool32 present_supported = VK_FALSE;
                if (!vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, m_surface, &present_supported),
                              "vkGetPhysicalDeviceSurfaceSupportKHR"))
                {
                    present_supported = VK_FALSE;
                }
                if (present_supported == VK_TRUE && !present_family.has_value())
                {
                    present_family = i;
                }
            }
            if (!graphics_family.has_value() || !present_family.has_value())
            {
                continue;
            }

            uint32_t ext_count = 0;
            if (!vk_check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr),
                          "vkEnumerateDeviceExtensionProperties"))
            {
                continue;
            }
            std::vector<VkExtensionProperties> exts(ext_count);
            const VkResult ext_result = vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, exts.data());
            if (ext_result != VK_SUCCESS && ext_result != VK_INCOMPLETE)
            {
                vk_check(ext_result, "vkEnumerateDeviceExtensionProperties");
                continue;
            }
            bool has_swap = false;
            bool has_extended_dynamic_state = false;
            bool has_portability_subset = false;
            for (const auto& e : exts)
            {
                if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                {
                    has_swap = true;
                }
                else if (std::strcmp(e.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0)
                {
                    has_extended_dynamic_state = true;
                }
                else if (std::strcmp(e.extensionName, k_portability_subset_extension) == 0)
                {
                    has_portability_subset = true;
                }
            }
            if (!has_swap)
            {
                continue;
            }

            const bool discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (best == VK_NULL_HANDLE || (discrete && !best_discrete))
            {
                best = gpu;
                best_discrete = discrete;
                best_graphics = *graphics_family;
                best_present = *present_family;
                best_timestamp_bits = qfs[*graphics_family].timestampValidBits;
                m_extended_dynamic_state_enabled = has_extended_dynamic_state;
                m_has_portability_subset = has_portability_subset;
            }
        }
        if (best == VK_NULL_HANDLE)
        {
            LOG_FTL("No suitable Vulkan GPU: needs api 1.1, a graphics queue that can present to the window, "
                    "and VK_KHR_swapchain");
            throw std::runtime_error{"no suitable Vulkan GPU"};
        }

        m_physical_device = best;
        m_graphics_queue_family = best_graphics;
        m_present_queue_family = best_present;
        m_timestamp_valid_bits = best_timestamp_bits;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &props);
        LOG_INF("Vulkan GPU: %s (api %u.%u.%u)",
                props.deviceName,
                VK_VERSION_MAJOR(props.apiVersion),
                VK_VERSION_MINOR(props.apiVersion),
                VK_VERSION_PATCH(props.apiVersion));

        // Every staging-ring reservation starts at a multiple of the
        // device's preferred copy alignment (a power of two on every
        // known driver; rounded up in case), never below 16 so any
        // texel block the engine's formats have divides it, and capped
        // so a driver that prefers page alignment does not waste a page
        // per small upload.
        VkDeviceSize alignment = 16;
        while (alignment < props.limits.optimalBufferCopyOffsetAlignment && alignment < 4096)
        {
            alignment *= 2;
        }
        m_staging_alignment = alignment;
    }

    void vk_device::create_logical_device()
    {
        const float queue_priority = 1.0f;
        std::set<uint32_t> unique_families{m_graphics_queue_family, m_present_queue_family};
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        for (uint32_t family : unique_families)
        {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = family;
            qi.queueCount = 1;
            qi.pQueuePriorities = &queue_priority;
            queue_infos.push_back(qi);
        }

        // Query the chained feature of the optional extension —
        // extended_dynamic_state exposes its feature bit through
        // @c VkPhysicalDeviceFeatures2.
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds_query{};
        eds_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 features2_query{};
        features2_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        void** features2_query_pnext = &features2_query.pNext;
        if (m_extended_dynamic_state_enabled)
        {
            *features2_query_pnext = &eds_query;
            features2_query_pnext = &eds_query.pNext;
        }
        vkGetPhysicalDeviceFeatures2(m_physical_device, &features2_query);
        if (m_extended_dynamic_state_enabled && eds_query.extendedDynamicState != VK_TRUE)
        {
            m_extended_dynamic_state_enabled = false;
            LOG_WRN("VK_EXT_extended_dynamic_state extension exposed but feature unavailable; "
                    "vertex_layout.stride==0 will collapse meshes to a point");
        }

        // Core features are requested only when the device reports
        // them: vkCreateDevice fails with VK_ERROR_FEATURE_NOT_PRESENT
        // for any unsupported feature that is asked for, and none of
        // these is universal (MoltenVK has no geometry shaders or
        // fillModeNonSolid, much mobile hardware no tessellation). What
        // was granted is recorded in m_features for the consumers to
        // gate on, and every gap is logged once here.
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(m_physical_device, &supported);
        VkPhysicalDeviceFeatures features{};
        const auto request = [](VkBool32 available, VkBool32& requested, bool& granted, const char* consequence)
        {
            granted = available == VK_TRUE;
            requested = granted ? VK_TRUE : VK_FALSE;
            if (!granted)
            {
                LOG_WRN("Vulkan feature unavailable on this device: %s", consequence);
            }
        };
        request(supported.fillModeNonSolid,
                features.fillModeNonSolid,
                m_features.fill_mode_non_solid,
                "fillModeNonSolid (wireframe / point materials rasterise filled)");
        request(supported.geometryShader,
                features.geometryShader,
                m_features.geometry_shader,
                "geometryShader (pipelines with a geometry stage are refused)");
        request(supported.tessellationShader,
                features.tessellationShader,
                m_features.tessellation_shader,
                "tessellationShader (pipelines with tessellation stages are refused)");
        request(supported.multiDrawIndirect,
                features.multiDrawIndirect,
                m_features.multi_draw_indirect,
                "multiDrawIndirect (multi-draw indirect is issued as one draw per record)");
        request(supported.samplerAnisotropy,
                features.samplerAnisotropy,
                m_features.sampler_anisotropy,
                "samplerAnisotropy (anisotropic filtering is unavailable)");
        request(supported.depthBiasClamp,
                features.depthBiasClamp,
                m_features.depth_bias_clamp,
                "depthBiasClamp (depth bias is applied unclamped)");
        request(supported.independentBlend,
                features.independentBlend,
                m_features.independent_blend,
                "independentBlend (every colour attachment of a pipeline blends alike)");
        // The block-compressed families are enabled wherever the device
        // has them, without a warning when it does not: a desktop GPU
        // lacks ASTC and a mobile one BCn as a matter of course, and
        // format_support steers the texture loaders to what is there.
        features.textureCompressionBC = supported.textureCompressionBC;
        features.textureCompressionASTC_LDR = supported.textureCompressionASTC_LDR;

        std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (m_extended_dynamic_state_enabled)
        {
            device_extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
        }
        if (m_has_portability_subset)
        {
            // A device that lists the portability subset is a layered
            // implementation, and the spec requires the extension to
            // be enabled on it.
            device_extensions.push_back(k_portability_subset_extension);
            m_features.portability_subset = true;
        }

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds_feature{};
        eds_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        eds_feature.extendedDynamicState = VK_TRUE;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.features = features;
        void** features2_pnext = &features2.pNext;
        if (m_extended_dynamic_state_enabled)
        {
            *features2_pnext = &eds_feature;
            features2_pnext = &eds_feature.pNext;
        }

        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        info.pQueueCreateInfos = queue_infos.data();
        info.pNext = &features2;
        info.pEnabledFeatures = nullptr;
        info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
        info.ppEnabledExtensionNames = device_extensions.data();
        if (!vk_check(vkCreateDevice(m_physical_device, &info, nullptr, &m_device), "vkCreateDevice"))
        {
            m_device = VK_NULL_HANDLE;
            throw std::runtime_error{"vkCreateDevice failed"};
        }
        vkGetDeviceQueue(m_device, m_graphics_queue_family, 0, &m_graphics_queue);
        vkGetDeviceQueue(m_device, m_present_queue_family, 0, &m_present_queue);

        if (m_extended_dynamic_state_enabled)
        {
            m_cmd_bind_vertex_buffers2 = reinterpret_cast<PFN_vkCmdBindVertexBuffers2EXT>(
                vkGetDeviceProcAddr(m_device, "vkCmdBindVertexBuffers2EXT"));
            if (m_cmd_bind_vertex_buffers2 == nullptr)
            {
                m_extended_dynamic_state_enabled = false;
                LOG_WRN("vkGetDeviceProcAddr returned null for vkCmdBindVertexBuffers2EXT; "
                        "falling back to non-dynamic stride");
            }
        }

        LOG_INF("Vulkan logical device created (extended_dynamic_state: %s, "
                "portability_subset: %s; features: fillModeNonSolid %s, geometryShader %s, tessellationShader %s, "
                "multiDrawIndirect %s, samplerAnisotropy %s, depthBiasClamp %s, independentBlend %s)",
                m_extended_dynamic_state_enabled ? "on" : "off",
                m_features.portability_subset ? "on" : "off",
                m_features.fill_mode_non_solid ? "on" : "off",
                m_features.geometry_shader ? "on" : "off",
                m_features.tessellation_shader ? "on" : "off",
                m_features.multi_draw_indirect ? "on" : "off",
                m_features.sampler_anisotropy ? "on" : "off",
                m_features.depth_bias_clamp ? "on" : "off",
                m_features.independent_blend ? "on" : "off");
    }

    void vk_device::query_capabilities()
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &props);
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
        m_features.timestamp_queries = m_timestamp_valid_bits > 0 && limits.timestampPeriod > 0.0f;
        m_features.debug_labels = m_debug_utils_enabled && m_set_debug_object_name != nullptr;
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
        if (m_physical_device == VK_NULL_HANDLE)
        {
            return 0;
        }
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(m_physical_device, vk_format_for(format), &props);
        return to_texture_usage(props.optimalTilingFeatures, is_depth_format(format));
    }

    void vk_device::create_allocator()
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &props);
        // VMA imports the entry points of the version it is told, which
        // may be neither higher than the instance asked for nor higher
        // than the physical device implements; only major.minor count.
        const uint32_t api = std::min(m_api_version, props.apiVersion);
        const uint32_t api_major_minor = VK_MAKE_VERSION(VK_VERSION_MAJOR(api), VK_VERSION_MINOR(api), 0);

        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo info{};
        info.vulkanApiVersion = api_major_minor;
        info.instance = m_instance;
        info.physicalDevice = m_physical_device;
        info.device = m_device;
        info.pVulkanFunctions = &functions;
        if (!vk_check(vmaCreateAllocator(&info, &m_allocator), "vmaCreateAllocator"))
        {
            m_allocator = VK_NULL_HANDLE;
            throw std::runtime_error{"vmaCreateAllocator failed"};
        }

        const VkPhysicalDeviceMemoryProperties* memory = nullptr;
        vmaGetMemoryProperties(m_allocator, &memory);
        LOG_INF("Vulkan memory allocator: VMA %u.%u.%u against api %u.%u, %u memory heaps, %u memory types",
                VK_VERSION_MAJOR(VMA_VERSION),
                VK_VERSION_MINOR(VMA_VERSION),
                VK_VERSION_PATCH(VMA_VERSION),
                VK_VERSION_MAJOR(api_major_minor),
                VK_VERSION_MINOR(api_major_minor),
                memory != nullptr ? memory->memoryHeapCount : 0u,
                memory != nullptr ? memory->memoryTypeCount : 0u);
    }

    void vk_device::destroy_allocator()
    {
        if (m_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }
    }

    void vk_device::create_command_pools()
    {
        // Transfer batches reset their own buffer when a slot is reused
        // (the others may be in flight), so that pool allows it; a frame
        // pool is reset whole, which is the cheaper operation and needs
        // no per-buffer flag. Both are transient: every buffer is
        // recorded once and reset.
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = m_graphics_queue_family;
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (!vk_check(vkCreateCommandPool(m_device, &info, nullptr, &m_transfer_command_pool),
                      "vkCreateCommandPool (transfer)"))
        {
            m_transfer_command_pool = VK_NULL_HANDLE;
            throw std::runtime_error{"vkCreateCommandPool failed"};
        }
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        for (uint32_t slot = 0; slot < m_frames_in_flight; ++slot)
        {
            frame_command_slot& frame_slot = m_frame_command_slots[slot];
            if (!vk_check(vkCreateCommandPool(m_device, &info, nullptr, &frame_slot.pool),
                          "vkCreateCommandPool (frame)"))
            {
                frame_slot.pool = VK_NULL_HANDLE;
                throw std::runtime_error{"vkCreateCommandPool failed"};
            }
        }
    }

    void vk_device::destroy_command_pools()
    {
        // Under vkDeviceWaitIdle (quit), after discard_transfer_batches:
        // no batch is executing and none holds staging memory, so the
        // fences can go and destroying the pools frees their command
        // buffers.
        discard_transfer_batches();
        for (transfer_batch& batch : m_transfer_batches)
        {
            if (batch.fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(m_device, batch.fence, nullptr);
            }
        }
        m_transfer_batches.clear();
        if (m_transfer_command_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_device, m_transfer_command_pool, nullptr);
            m_transfer_command_pool = VK_NULL_HANDLE;
        }
        for (frame_command_slot& slot : m_frame_command_slots)
        {
            slot.buffers.clear();
            slot.next = 0;
            if (slot.pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(m_device, slot.pool, nullptr);
                slot.pool = VK_NULL_HANDLE;
            }
            for (frame_command_slot::lane& lane : slot.lanes)
            {
                if (lane.pool != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(m_device, lane.pool, nullptr);
                }
            }
            slot.lanes.clear();
        }
    }

    bool vk_device::create_staging_ring()
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = k_staging_ring_bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const VmaAllocationCreateInfo ai = host_mapped_allocation(/*prefer_host=*/true);
        VmaAllocationInfo info{};
        if (!vk_check(vmaCreateBuffer(m_allocator, &bi, &ai, &m_staging_buffer, &m_staging_allocation, &info),
                      "vmaCreateBuffer (staging ring)"))
        {
            m_staging_buffer = VK_NULL_HANDLE;
            m_staging_allocation = VK_NULL_HANDLE;
            return false;
        }
        if (info.pMappedData == nullptr)
        {
            LOG_ERR("vk_device::create_staging_ring: the staging ring allocation is not mapped");
            destroy_staging_ring();
            return false;
        }
        m_staging_mapped = static_cast<uint8_t*>(info.pMappedData);
        m_staging_ring = staging_ring{k_staging_ring_bytes};
        LOG_INF("Vulkan staging ring: %llu MiB, %llu-byte copy alignment",
                static_cast<unsigned long long>(k_staging_ring_bytes / (1024ull * 1024ull)),
                static_cast<unsigned long long>(m_staging_alignment));
        return true;
    }

    void vk_device::destroy_staging_ring()
    {
        if (m_staging_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(m_allocator, m_staging_buffer, m_staging_allocation);
        }
        m_staging_buffer = VK_NULL_HANDLE;
        m_staging_allocation = VK_NULL_HANDLE;
        m_staging_mapped = nullptr;
        m_staging_ring = staging_ring{};
    }

    bool vk_device::create_descriptor_pool()
    {
        const uint32_t index = static_cast<uint32_t>(m_descriptor_pools.size());
        const descriptor_pool_budget budget = descriptor_pool_budget_for(index);
        std::array<VkDescriptorPoolSize, 5> sizes{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = budget.uniform_buffers;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = budget.combined_image_samplers;
        sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[2].descriptorCount = budget.storage_buffers;
        sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[3].descriptorCount = budget.storage_images;
        sizes[4].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        sizes[4].descriptorCount = budget.dynamic_uniform_buffers;

        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets = budget.max_sets;
        info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (!vk_check(vkCreateDescriptorPool(m_device, &info, nullptr, &pool), "vkCreateDescriptorPool"))
        {
            return false;
        }
        m_descriptor_pools.push_back(pool);
        LOG_INF("Vulkan descriptor pool %u: %u sets (%u uniform buffers, %u dynamic uniform buffers, "
                "%u combined image samplers, %u storage buffers, %u storage images)",
                index,
                budget.max_sets,
                budget.uniform_buffers,
                budget.dynamic_uniform_buffers,
                budget.combined_image_samplers,
                budget.storage_buffers,
                budget.storage_images);
        return true;
    }

    bool vk_device::allocate_descriptor_set(VkDescriptorSetLayout layout,
                                            VkDescriptorSet& out_set,
                                            VkDescriptorPool& out_pool)
    {
        out_set = VK_NULL_HANDLE;
        out_pool = VK_NULL_HANDLE;
        if (m_descriptor_pools.empty() && !create_descriptor_pool())
        {
            return false;
        }
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = m_descriptor_pools.back();
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;
            const VkResult r = vkAllocateDescriptorSets(m_device, &ai, &out_set);
            if (r == VK_SUCCESS)
            {
                out_pool = m_descriptor_pools.back();
                return true;
            }
            out_set = VK_NULL_HANDLE;
            // Out-of-pool-memory and fragmentation are the two "this
            // pool is full" answers; anything else is a real failure.
            // The fresh pool is larger than the one that ran out, so a
            // second refusal from it is not retried.
            const bool exhausted = r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL;
            if (!exhausted || attempt == 1)
            {
                vk_check(r, "vkAllocateDescriptorSets");
                return false;
            }
            if (!create_descriptor_pool())
            {
                return false;
            }
        }
        return false;
    }

    // -- Swapchain ------------------------------------------------------

    bool vk_device::create_swapchain(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D extent)
    {
        m_surface_format = pick_surface_format(m_physical_device, m_surface);
        m_present_mode = pick_present_mode(m_physical_device, m_surface, m_vsync);

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        {
            image_count = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = m_surface;
        info.minImageCount = image_count;
        info.imageFormat = m_surface_format.format;
        info.imageColorSpace = m_surface_format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::array<uint32_t, 2> families{m_graphics_queue_family, m_present_queue_family};
        if (m_graphics_queue_family != m_present_queue_family)
        {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = static_cast<uint32_t>(families.size());
            info.pQueueFamilyIndices = families.data();
        }
        else
        {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = m_present_mode;
        info.clipped = VK_TRUE;
        // Hand the previous swapchain over instead of destroying it
        // first: the presentation engine can carry its resources across
        // (no teardown-then-setup hitch, and no window-in-use failure on
        // platforms that refuse a second swapchain on a surface that
        // still has one). The call retires it whether or not it
        // succeeds, so it is released unconditionally right after.
        info.oldSwapchain = m_swapchain;
        VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
        const VkResult create_result = vkCreateSwapchainKHR(m_device, &info, nullptr, &new_swapchain);
        destroy_swapchain();
        if (create_result != VK_SUCCESS)
        {
            LOG_ERR("vkCreateSwapchainKHR failed: %s (%ux%u)",
                    vk_result_to_string(create_result),
                    extent.width,
                    extent.height);
            return false;
        }
        m_swapchain = new_swapchain;
        m_swapchain_extent = extent;

        uint32_t actual = 0;
        if (!vk_check(vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual, nullptr), "vkGetSwapchainImagesKHR"))
        {
            destroy_swapchain();
            return false;
        }
        m_swapchain_images.resize(actual);
        if (!vk_check(vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual, m_swapchain_images.data()),
                      "vkGetSwapchainImagesKHR"))
        {
            destroy_swapchain();
            return false;
        }

        // Every failure below releases the partially built swapchain
        // through destroy_swapchain, which skips null handles, so the
        // vectors are sized with null placeholders up front.
        m_swapchain_image_views.assign(actual, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < actual; ++i)
        {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = m_swapchain_images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = m_surface_format.format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            const VkResult view_result = vkCreateImageView(m_device, &vi, nullptr, &m_swapchain_image_views[i]);
            if (view_result != VK_SUCCESS)
            {
                LOG_ERR("vkCreateImageView (swapchain image %u) failed: %s", i, vk_result_to_string(view_result));
                destroy_swapchain();
                return false;
            }
        }

        // The depth format resolved against this device at init (see
        // resolve_depth_formats), not the nominal translation. One
        // depth image per frame slot (see m_swapchain_depth_images).
        const VkFormat depth_fmt = vk_format_for(m_swapchain_depth_format);
        for (uint32_t slot = 0; slot < m_frames_in_flight; ++slot)
        {
            VkImageCreateInfo di{};
            di.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            di.imageType = VK_IMAGE_TYPE_2D;
            di.format = depth_fmt;
            di.extent = {extent.width, extent.height, 1};
            di.mipLevels = 1;
            di.arrayLayers = 1;
            di.samples = VK_SAMPLE_COUNT_1_BIT;
            di.tiling = VK_IMAGE_TILING_OPTIMAL;
            di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            di.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            const VmaAllocationCreateInfo depth_alloc = device_local_allocation();
            const VkResult depth_result = vmaCreateImage(m_allocator,
                                                         &di,
                                                         &depth_alloc,
                                                         &m_swapchain_depth_images[slot],
                                                         &m_swapchain_depth_allocations[slot],
                                                         nullptr);
            if (depth_result != VK_SUCCESS)
            {
                LOG_ERR("vmaCreateImage (swapchain depth %u) failed: %s", slot, vk_result_to_string(depth_result));
                m_swapchain_depth_images[slot] = VK_NULL_HANDLE;
                m_swapchain_depth_allocations[slot] = VK_NULL_HANDLE;
                destroy_swapchain();
                return false;
            }
            VkImageViewCreateInfo dvi{};
            dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            dvi.image = m_swapchain_depth_images[slot];
            dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dvi.format = depth_fmt;
            // The swapchain depth is never sampled, so the view carries
            // every aspect the resolved format has.
            dvi.subresourceRange.aspectMask = aspect_for_vk_format(depth_fmt);
            dvi.subresourceRange.levelCount = 1;
            dvi.subresourceRange.layerCount = 1;
            const VkResult depth_view_result =
                vkCreateImageView(m_device, &dvi, nullptr, &m_swapchain_depth_views[slot]);
            if (depth_view_result != VK_SUCCESS)
            {
                LOG_ERR(
                    "vkCreateImageView (swapchain depth %u) failed: %s", slot, vk_result_to_string(depth_view_result));
                m_swapchain_depth_views[slot] = VK_NULL_HANDLE;
                destroy_swapchain();
                return false;
            }
        }

        // One render-finished semaphore per swapchain image (see the field
        // declaration). Sized to the actual image count so it tracks resize.
        VkSemaphoreCreateInfo rfi{};
        rfi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        m_render_finished.assign(actual, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < actual; ++i)
        {
            const VkResult semaphore_result = vkCreateSemaphore(m_device, &rfi, nullptr, &m_render_finished[i]);
            if (semaphore_result != VK_SUCCESS)
            {
                LOG_ERR("vkCreateSemaphore (render-finished %u) failed: %s", i, vk_result_to_string(semaphore_result));
                destroy_swapchain();
                return false;
            }
        }
        // Fresh images: no frame has drawn into any of them yet.
        m_image_last_slot.assign(actual, k_no_slot);

        ++m_swapchain_generation;
        LOG_INF("Vulkan swapchain: %ux%u images=%u generation=%llu",
                extent.width,
                extent.height,
                actual,
                static_cast<unsigned long long>(m_swapchain_generation));
        return true;
    }

    void vk_device::destroy_swapchain()
    {
        if (m_device == VK_NULL_HANDLE)
        {
            return;
        }
        for (uint32_t slot = 0; slot < k_max_frames_in_flight; ++slot)
        {
            if (m_swapchain_depth_views[slot] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_device, m_swapchain_depth_views[slot], nullptr);
                m_swapchain_depth_views[slot] = VK_NULL_HANDLE;
            }
            if (m_swapchain_depth_images[slot] != VK_NULL_HANDLE)
            {
                vmaDestroyImage(m_allocator, m_swapchain_depth_images[slot], m_swapchain_depth_allocations[slot]);
                m_swapchain_depth_images[slot] = VK_NULL_HANDLE;
                m_swapchain_depth_allocations[slot] = VK_NULL_HANDLE;
            }
        }
        m_image_last_slot.clear();
        for (auto v : m_swapchain_image_views)
        {
            if (v != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_device, v, nullptr);
            }
        }
        m_swapchain_image_views.clear();
        m_swapchain_images.clear();
        for (auto s : m_render_finished)
        {
            if (s != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_device, s, nullptr);
            }
        }
        m_render_finished.clear();
        if (m_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    void vk_device::create_sync_objects()
    {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        // The render-finished semaphores live with the swapchain (one per
        // image); the per-slot image-available semaphores and in-flight
        // fences are owned here.
        for (uint32_t slot = 0; slot < m_frames_in_flight; ++slot)
        {
            if (!vk_check(vkCreateSemaphore(m_device, &si, nullptr, &m_image_available[slot]),
                          "vkCreateSemaphore (image-available)") ||
                !vk_check(vkCreateFence(m_device, &fi, nullptr, &m_in_flight_fences[slot]),
                          "vkCreateFence (in-flight)"))
            {
                throw std::runtime_error{"vk sync objects"};
            }
        }
        m_in_flight_fence_armed.fill(false);
        m_fence_submit_serial.fill(0);
    }

    void vk_device::destroy_sync_objects()
    {
        if (m_device == VK_NULL_HANDLE)
        {
            return;
        }
        for (uint32_t slot = 0; slot < k_max_frames_in_flight; ++slot)
        {
            if (m_image_available[slot] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_device, m_image_available[slot], nullptr);
                m_image_available[slot] = VK_NULL_HANDLE;
            }
            if (m_in_flight_fences[slot] != VK_NULL_HANDLE)
            {
                vkDestroyFence(m_device, m_in_flight_fences[slot], nullptr);
                m_in_flight_fences[slot] = VK_NULL_HANDLE;
            }
        }
        m_in_flight_fence_armed.fill(false);
    }

    // -- Render targets / swapchain accessors ---------------------------

    render_target vk_device::swapchain_target()
    {
        return m_swapchain_target;
    }

    void vk_device::resize_swapchain(uint32_t width, uint32_t height)
    {
        m_window_width = width;
        m_window_height = height;
        if (!m_initialised || m_device == VK_NULL_HANDLE)
        {
            return;
        }
        if (width == 0 || height == 0)
        {
            // Minimised. Nothing is torn down: the existing swapchain
            // stays (possibly out of date) as the oldSwapchain for the
            // rebuild that resumes presentation, once a later hint or
            // the per-frame surface poll reports a real extent.
            suspend_swapchain("the window reports a 0x0 backbuffer (minimised)", false);
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
        if (!m_swapchain_suspended && m_swapchain != VK_NULL_HANDLE && width == m_swapchain_extent.width &&
            height == m_swapchain_extent.height)
        {
            return;
        }
        recreate_swapchain();
    }

    bool vk_device::swapchain_suspended() const noexcept
    {
        return m_swapchain_suspended;
    }

    void vk_device::suspend_swapchain(const char* reason, bool is_error)
    {
        if (m_swapchain_suspended)
        {
            return;
        }
        m_swapchain_suspended = true;
        if (is_error)
        {
            LOG_ERR("Vulkan swapchain suspended: %s; presentation resumes once a rebuild succeeds", reason);
        }
        else
        {
            LOG_INF("Vulkan swapchain suspended: %s; presentation resumes once the surface has an extent", reason);
        }
    }

    bool vk_device::recreate_swapchain()
    {
        if (m_device == VK_NULL_HANDLE || m_surface == VK_NULL_HANDLE || m_device_lost)
        {
            return false;
        }
        // The surface, not the cached window size, is the authority on
        // the extent. An OS-driven out-of-date (display change, a
        // compositor decision) arrives without any resize hint, so
        // rebuilding at the cached size would leave the swapchain out
        // of date for every following acquire.
        VkSurfaceCapabilitiesKHR caps{};
        const VkResult caps_result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &caps);
        if (caps_result != VK_SUCCESS)
        {
            suspend_swapchain(vk_result_to_string(caps_result), true);
            return false;
        }
        const VkExtent2D extent = choose_swapchain_extent(caps, m_window_width, m_window_height);
        if (extent.width == 0 || extent.height == 0)
        {
            // Minimised: vkCreateSwapchainKHR rejects a zero extent, so
            // keep whatever swapchain exists (it becomes oldSwapchain
            // on resume) and stop acquiring. begin_frame polls the
            // surface until this branch stops being taken.
            suspend_swapchain("the surface extent is 0x0 (window minimised)", false);
            return false;
        }

        // Everything that references the old images has to be idle:
        // every frame in flight, and the present that may still be
        // reading the image it was handed. A rebuild is rare, so the
        // full drain is affordable.
        if (!check_queue_result(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle (swapchain rebuild)"))
        {
            suspend_swapchain("the device could not be waited idle before the rebuild", true);
            return false;
        }
        // The idle wait covered every frame the fences track and every
        // submitted transfer batch; the batch still recording, if any,
        // was never submitted and stays open.
        note_device_idle();
        retire_transfer_batches();

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

        if (!create_swapchain(caps, extent))
        {
            // create_swapchain released the old swapchain (passing it
            // as oldSwapchain retired it whether or not the call
            // succeeded), so there is nothing to present into until a
            // later poll succeeds; the next one starts from scratch.
            suspend_swapchain("the swapchain could not be rebuilt", true);
            return false;
        }
        if (swap != nullptr)
        {
            swap->width = m_swapchain_extent.width;
            swap->height = m_swapchain_extent.height;
        }
        if (m_swapchain_suspended)
        {
            m_swapchain_suspended = false;
            LOG_INF("Vulkan swapchain resumed at %ux%u", m_swapchain_extent.width, m_swapchain_extent.height);
        }
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

        const VkDevice dev = m_device;
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
        if (!m_features.timestamp_queries)
        {
            LOG_WRN("create_query_set: the graphics queue writes no timestamps; no query set created");
            return {};
        }
        if (descriptor.count == 0)
        {
            LOG_ERR("create_query_set: a query set needs at least one query");
            return {};
        }
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = descriptor.count;
        vk_query_set record{};
        record.count = descriptor.count;
        if (!vk_check(vkCreateQueryPool(m_device, &info, nullptr, &record.pool), "vkCreateQueryPool"))
        {
            return {};
        }
        query_set h{};
        h.id = m_query_sets.insert(record);
        return h;
    }

    void vk_device::destroy(query_set handle)
    {
        auto* record = m_query_sets.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        // The frame's command buffer may still write or reset the
        // pool; it goes with the rest at the next fence wait.
        const VkDevice dev = m_device;
        const VkQueryPool pool = record->pool;
        if (pool != VK_NULL_HANDLE)
        {
            enqueue_destroy([dev, pool] { vkDestroyQueryPool(dev, pool, nullptr); });
        }
        record->pool = VK_NULL_HANDLE;
        m_query_sets.remove(handle.id);
    }

    bool vk_device::resolve_queries(query_set set, uint32_t first, uint32_t count, uint64_t* out_ticks)
    {
        auto* record = m_query_sets.lookup(set.id);
        if (record == nullptr || record->pool == VK_NULL_HANDLE || out_ticks == nullptr || count == 0)
        {
            return false;
        }
        if (first > record->count || count > record->count - first)
        {
            LOG_WRN("resolve_queries: %u queries from %u exceed the %u-query set", count, first, record->count);
            return false;
        }
        if (m_device_lost)
        {
            return false;
        }
        // No wait: VK_NOT_READY means a query has not completed (or was
        // reset and never written) and the caller keeps its previous
        // values.
        const VkResult r = vkGetQueryPoolResults(m_device,
                                                 record->pool,
                                                 first,
                                                 count,
                                                 static_cast<size_t>(count) * sizeof(uint64_t),
                                                 out_ticks,
                                                 sizeof(uint64_t),
                                                 VK_QUERY_RESULT_64_BIT);
        if (r == VK_NOT_READY)
        {
            return false;
        }
        return check_queue_result(r, "vkGetQueryPoolResults");
    }

    // -- Debug names ---------------------------------------------------

    void vk_device::set_debug_name(buffer handle, const char* name)
    {
        if (const auto* record = m_buffers.lookup(handle.id))
        {
            name_object(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(record->object), name);
        }
    }

    void vk_device::set_debug_name(texture handle, const char* name)
    {
        if (const auto* record = m_textures.lookup(handle.id))
        {
            name_object(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(record->image), name);
            name_object(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(record->view), name);
        }
    }

    void vk_device::set_debug_name(sampler handle, const char* name)
    {
        if (const auto* record = m_samplers.lookup(handle.id))
        {
            name_object(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(record->object), name);
        }
    }

    void vk_device::set_debug_name(pipeline handle, const char* name)
    {
        // The VkPipeline objects are built lazily per render pass, so
        // the layout — shared by every variant — carries the name.
        if (const auto* record = m_pipelines.lookup(handle.id))
        {
            name_object(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<uint64_t>(record->layout), name);
            name_object(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(record->compute_object), name);
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

    void vk_device::note_render_pass_opened(bool is_swapchain, bool use_depth)
    {
        if (is_swapchain)
        {
            ++m_frame_stats.passes_swapchain;
        }
        else
        {
            ++m_frame_stats.passes_offscreen;
        }
        (void)use_depth;
    }

    void vk_device::note_draw(uint32_t vertex_count)
    {
        ++m_frame_stats.draws;
        m_frame_stats.vertices += vertex_count;
    }

    void vk_device::note_draw_indexed(uint32_t index_count)
    {
        ++m_frame_stats.draws_indexed;
        m_frame_stats.indices += index_count;
    }

    void vk_device::note_draws(uint32_t draws, uint32_t vertices, uint32_t draws_indexed, uint32_t indices)
    {
        m_frame_stats.draws += draws;
        m_frame_stats.vertices += vertices;
        m_frame_stats.draws_indexed += draws_indexed;
        m_frame_stats.indices += indices;
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
        if (m_device_lost)
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
        flush_transfer_batch();

        const uint32_t slot = m_frame_slot;
        VkFence fence = m_in_flight_fences[slot];
        if (!m_have_current_image)
        {
            // No swapchain image this frame: either work submitted
            // outside a frame bracket (the IBL prefilter at start-up)
            // or a frame whose passes never reached the swapchain. It
            // runs on the current slot's fence like a frame submission,
            // with no semaphores and no present: the next begin_frame
            // of that slot waits for it before the pool reset and the
            // deferred-destroy drain, so the command buffer is not
            // reused and nothing it references (the IBL scaffold is
            // destroyed right after its submit) is freed while it
            // executes. A fence still armed by an earlier such
            // submission is waited first — for that one submission, not
            // the whole queue.
            if (!wait_slot_fence(slot))
            {
                return;
            }
            if (!vk_check(vkResetFences(m_device, 1, &fence), "vkResetFences (no-image)"))
            {
                return;
            }
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            if (check_queue_result(vkQueueSubmit(m_graphics_queue, 1, &si, fence), "vkQueueSubmit (no-image)"))
            {
                m_in_flight_fence_armed[slot] = true;
                m_fence_submit_serial[slot] = ++m_submit_serial;
            }
            return;
        }

        // begin_frame already waited this slot's fence for the frame
        // that last used it, so it is signaled and idle unless a
        // no-image submission armed it again this frame. Reset it
        // here, right before the one submission that signals it again,
        // rather than at acquire time: a reset at acquire time would
        // leave the fence unsignaled whenever the acquire fails
        // (out-of-date swapchain), and the next begin_frame would then
        // block forever.
        if (!wait_slot_fence(slot))
        {
            return;
        }
        if (!vk_check(vkResetFences(m_device, 1, &fence), "vkResetFences"))
        {
            return;
        }

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &m_image_available[slot];
        si.pWaitDstStageMask = &wait_stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &m_render_finished[m_current_image_index];
        if (!check_queue_result(vkQueueSubmit(m_graphics_queue, 1, &si, fence), "vkQueueSubmit"))
        {
            // Nothing signals the fence or the render-finished
            // semaphore now: the fence stays disarmed so the next
            // begin_frame does not wait on it forever, and the frame
            // is not presented (the present would wait forever on the
            // semaphore).
            return;
        }
        m_in_flight_fence_armed[slot] = true;
        m_fence_submit_serial[slot] = ++m_submit_serial;
        // This frame now owns the image until its fence retires; the
        // acquire that hands the image back checks (see
        // acquire_swapchain_image).
        if (m_current_image_index < m_image_last_slot.size())
        {
            m_image_last_slot[m_current_image_index] = slot;
        }
        // The present itself belongs to the frame boundary; end_frame
        // issues it once the renderer has closed the frame.
        m_present_pending = true;
    }

    bool vk_device::wait_slot_fence(uint32_t slot)
    {
        if (slot >= k_max_frames_in_flight || !m_in_flight_fence_armed[slot])
        {
            return true;
        }
        // Disarmed before the wait: after a failure nothing would ever
        // signal it, and a lost device is done anyway.
        m_in_flight_fence_armed[slot] = false;
        if (!check_queue_result(vkWaitForFences(m_device, 1, &m_in_flight_fences[slot], VK_TRUE, UINT64_MAX),
                                "vkWaitForFences"))
        {
            return false;
        }
        // The queue completes submissions in order, so this one
        // retiring proves every earlier one retired too.
        m_completed_submit_serial = std::max(m_completed_submit_serial, m_fence_submit_serial[slot]);
        return true;
    }

    void vk_device::note_device_idle()
    {
        m_in_flight_fence_armed.fill(false);
        m_completed_submit_serial = std::max(m_completed_submit_serial, m_submit_serial);
    }

    void vk_device::wait_slot_before_host_write()
    {
        if (!m_in_frame)
        {
            // Between frames the current slot's region still belongs to
            // the frame that last recorded into the slot until its fence
            // retires — the wait begin_frame would do next, brought
            // forward. It disarms the fence, so the frame's own wait is
            // then free.
            wait_slot_fence(m_frame_slot);
        }
    }

    void vk_device::reset_frame_command_pool()
    {
        frame_command_slot& slot = m_frame_command_slots[m_frame_slot];
        if (slot.pool != VK_NULL_HANDLE)
        {
            vk_check(vkResetCommandPool(m_device, slot.pool, 0), "vkResetCommandPool (frame)");
        }
        slot.next = 0;
        // The secondaries of this slot's frame were executed by its
        // primary, so the same fence wait proved them complete.
        for (frame_command_slot::lane& lane : slot.lanes)
        {
            if (lane.pool != VK_NULL_HANDLE)
            {
                vk_check(vkResetCommandPool(m_device, lane.pool, 0), "vkResetCommandPool (lane)");
            }
            lane.next = 0;
        }
    }

    VkCommandBuffer vk_device::acquire_secondary_command_buffer(uint32_t lane_index)
    {
        if (m_device_lost || m_device == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        frame_command_slot& slot = m_frame_command_slots[m_frame_slot];
        // Lanes are appended as a wider fork asks for them; a pool that
        // failed to create leaves its lane empty and every later request
        // for it retries.
        if (lane_index >= slot.lanes.size())
        {
            slot.lanes.resize(static_cast<size_t>(lane_index) + 1);
        }
        frame_command_slot::lane& lane = slot.lanes[lane_index];
        if (lane.pool == VK_NULL_HANDLE)
        {
            // Transient and reset whole, like the frame's primary pool.
            VkCommandPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.queueFamilyIndex = m_graphics_queue_family;
            info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (!vk_check(vkCreateCommandPool(m_device, &info, nullptr, &lane.pool), "vkCreateCommandPool (lane)"))
            {
                lane.pool = VK_NULL_HANDLE;
                return VK_NULL_HANDLE;
            }
        }
        if (lane.next < lane.buffers.size())
        {
            return lane.buffers[lane.next++];
        }
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = lane.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (!vk_check(vkAllocateCommandBuffers(m_device, &ai, &cmd), "vkAllocateCommandBuffers (lane)"))
        {
            return VK_NULL_HANDLE;
        }
        lane.buffers.push_back(cmd);
        ++lane.next;
        return cmd;
    }

    VkCommandBuffer vk_device::acquire_frame_command_buffer()
    {
        if (m_device_lost)
        {
            return VK_NULL_HANDLE;
        }
        frame_command_slot& slot = m_frame_command_slots[m_frame_slot];
        if (slot.pool == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        if (slot.next < slot.buffers.size())
        {
            return slot.buffers[slot.next++];
        }
        // The slot has handed out every buffer it owns since the last
        // reset (one per frame in the steady state, more only when
        // several encoders are recorded between two frames); grow it.
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = slot.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (!vk_check(vkAllocateCommandBuffers(m_device, &ai, &cmd), "vkAllocateCommandBuffers (frame)"))
        {
            return VK_NULL_HANDLE;
        }
        slot.buffers.push_back(cmd);
        ++slot.next;
        return cmd;
    }

    void vk_device::enqueue_destroy(std::function<void()> fn)
    {
        if (fn)
        {
            // Inside a frame the frame's own submission, still to come,
            // may reference the resource; between frames only the
            // submissions already queued can. The newest batch — open,
            // or submitted and maybe still executing — is the last one
            // that can hold a copy into the resource; a batch begun
            // later never sees its handle.
            const uint64_t submit_serial = m_in_frame ? m_submit_serial + 1 : m_submit_serial;
            m_pending_destroys.push_back({submit_serial, m_next_transfer_batch_id - 1, std::move(fn)});
        }
    }

    void vk_device::drain_pending_destroys()
    {
        // Move out first so a destroy callback that itself enqueues
        // is captured into the next drain rather than running here.
        // An entry whose submission or transfer batch has not retired
        // yet goes back in the queue for a later drain.
        std::vector<pending_destroy> drain;
        drain.swap(m_pending_destroys);
        for (pending_destroy& entry : drain)
        {
            if (entry.submit_serial > m_completed_submit_serial || transfer_batch_live_up_to(entry.transfer_batch_id))
            {
                m_pending_destroys.push_back(std::move(entry));
                continue;
            }
            entry.fn();
        }
    }

    void vk_device::flush_pending_destroys()
    {
        if (m_device == VK_NULL_HANDLE || m_device_lost)
        {
            return;
        }
        check_queue_result(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle (flush_pending_destroys)");
        // Idle: nothing executes any more, so every deferred destroy —
        // including one stamped for a submission that never happened —
        // may run now, same reasoning as quit()'s own final drain.
        note_device_idle();
        m_completed_submit_serial = UINT64_MAX;
        retire_transfer_batches();
        drain_pending_destroys();
    }

    // -- Frame boundary ------------------------------------------------

    void vk_device::begin_frame()
    {
        if (!m_initialised || m_device_lost)
        {
            // A lost device has no frame to wait for; the renderer's
            // recording is dropped by submit and end_frame raises the
            // loss.
            return;
        }
        m_in_frame = true;
        // Block until the command buffer of the frame that last used
        // this slot — frames_in_flight frames ago — has finished
        // executing. This runs before the renderer records anything for
        // the new frame, so every host write that follows — this slot's
        // regions of the per-frame camera / light / shadow UBOs, instance
        // re-uploads — lands in memory the GPU is no longer reading.
        // Waiting lazily at the first swapchain pass instead, after every
        // off-screen pass had already written its UBOs, would race those
        // host writes against the GPU's reads. The fence is only waited
        // when a submission armed it: after a failed submit nothing would
        // ever signal it.
        if (!wait_slot_fence(m_frame_slot) && m_device_lost)
        {
            return;
        }
        // Every transfer batch submitted before that frame completed
        // ahead of its fence in queue order, so polling reclaims them
        // (ring bytes, dedicated staging buffers); one submitted since
        // — a ring-full flush during loading between frames — may
        // still be executing and is left alone, as are the deferred
        // destroys it gates.
        retire_transfer_batches();
        if (m_device_lost)
        {
            return;
        }
        // Nothing from this slot's pool is pending any more: reclaim
        // the command buffers for this frame's encoders.
        reset_frame_command_pool();
        // Everything enqueued for destruction up to the submission the
        // fence retired is no longer referenced by the GPU — and no
        // command buffer is open yet that could reference what a
        // material rebuilds this frame. This is the one in-frame point
        // where freeing is safe (entries a later submission or a live
        // transfer batch still gates stay queued).
        drain_pending_destroys();

        if (m_swapchain_suspended)
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
        if (m_have_current_image && m_present_pending && !m_device_lost)
        {
            VkPresentInfoKHR pi{};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &m_render_finished[m_current_image_index];
            pi.swapchainCount = 1;
            pi.pSwapchains = &m_swapchain;
            pi.pImageIndices = &m_current_image_index;
            const VkResult r = vkQueuePresentKHR(m_present_queue, &pi);
            if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            {
                // The semaphore wait was consumed either way (a rejected
                // present still executes it), so rebuild now against the
                // surface's current extent and the next frame acquires
                // from a swapchain that matches it. recreate_swapchain
                // waits the device idle first, which covers the command
                // buffer submitted a moment ago.
                recreate_swapchain();
            }
            else
            {
                check_queue_result(r, "vkQueuePresentKHR");
            }
        }
        // An image acquired without a submission (the encoder failed to
        // record) is dropped rather than presented: its render-finished
        // semaphore was never signaled, so a present would wait forever.
        m_have_current_image = false;
        m_acquire_attempted = false;
        m_present_pending = false;

        if (m_device_lost && !m_device_lost_thrown)
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

        if (m_frame_index < k_diagnostic_frames)
        {
            LOG_INF("Vulkan frame %u: passes(off=%u, swap=%u) draws(non_indexed=%u, indexed=%u) verts=%u idxs=%u",
                    m_frame_index,
                    m_frame_stats.passes_offscreen,
                    m_frame_stats.passes_swapchain,
                    m_frame_stats.draws,
                    m_frame_stats.draws_indexed,
                    m_frame_stats.vertices,
                    m_frame_stats.indices);
        }
        m_frame_stats = {};
        ++m_frame_index;
        // The next frame records into the next slot; its begin_frame
        // waits that slot's fence.
        m_frame_slot = (m_frame_slot + 1) % m_frames_in_flight;
        m_in_frame = false;
    }

    void vk_device::acquire_swapchain_image()
    {
        if (m_have_current_image || m_acquire_attempted)
        {
            return;
        }
        m_acquire_attempted = true;
        if (m_swapchain_suspended || m_swapchain == VK_NULL_HANDLE || m_device_lost)
        {
            return;
        }
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const VkResult r = vkAcquireNextImageKHR(m_device,
                                                     m_swapchain,
                                                     UINT64_MAX,
                                                     m_image_available[m_frame_slot],
                                                     VK_NULL_HANDLE,
                                                     &m_current_image_index);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            {
                // A suboptimal acquire still hands out an image and
                // signals the semaphore, so the frame has to use it;
                // the present's own result triggers the rebuild.
                m_have_current_image = true;
                // The images-in-flight guard: the frame that last drew
                // this image may still be executing on another slot,
                // and its submission signals the image's render-finished
                // semaphore, which this frame's submission would signal
                // again. Wait for that frame first; the presentation
                // engine normally hands an image back only after its
                // present consumed the semaphore, so this rarely blocks.
                if (m_current_image_index < m_image_last_slot.size())
                {
                    const uint32_t last_slot = m_image_last_slot[m_current_image_index];
                    if (last_slot != k_no_slot && last_slot != m_frame_slot)
                    {
                        wait_slot_fence(last_slot);
                    }
                }
                return;
            }
            if (r == VK_ERROR_OUT_OF_DATE_KHR)
            {
                // No image was acquired and the semaphore stays
                // unsignaled, so it can be reused. Rebuild against the
                // surface's current extent and retry once so the frame
                // lands in the new swapchain instead of being dropped;
                // if the rebuild suspended (minimised) or the retry is
                // out of date again, the frame skips the swapchain.
                if (attempt == 0 && recreate_swapchain())
                {
                    continue;
                }
                return;
            }
            check_queue_result(r, "vkAcquireNextImageKHR");
            return;
        }
    }

    // -- Transfer batches ----------------------------------------------

    vk_device::transfer_batch* vk_device::open_transfer_batch()
    {
        if (m_device_lost || m_transfer_command_pool == VK_NULL_HANDLE)
        {
            return nullptr;
        }
        if (m_open_transfer_batch != k_no_batch)
        {
            return &m_transfer_batches[m_open_transfer_batch];
        }

        // An idle slot, or a new one. Slots are only ever appended, so
        // an index stays valid; a pointer does not survive the
        // push_back below, which is why callers re-fetch after anything
        // that may open a batch.
        size_t index = k_no_batch;
        for (size_t i = 0; i < m_transfer_batches.size(); ++i)
        {
            if (m_transfer_batches[i].state == transfer_batch::batch_state::idle)
            {
                index = i;
                break;
            }
        }
        if (index == k_no_batch)
        {
            transfer_batch batch{};
            VkCommandBufferAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.commandPool = m_transfer_command_pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            if (!vk_check(vkAllocateCommandBuffers(m_device, &ai, &batch.cmd), "vkAllocateCommandBuffers (transfer)"))
            {
                return nullptr;
            }
            VkFenceCreateInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (!vk_check(vkCreateFence(m_device, &fi, nullptr, &batch.fence), "vkCreateFence (transfer)"))
            {
                vkFreeCommandBuffers(m_device, m_transfer_command_pool, 1, &batch.cmd);
                return nullptr;
            }
            m_transfer_batches.push_back(batch);
            index = m_transfer_batches.size() - 1;
        }

        transfer_batch& batch = m_transfer_batches[index];
        // An idle slot's buffer has completed (its batch was retired)
        // or was never submitted; either way it can be reset and
        // begun again.
        if (!vk_check(vkResetCommandBuffer(batch.cmd, 0), "vkResetCommandBuffer (transfer)"))
        {
            return nullptr;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!vk_check(vkBeginCommandBuffer(batch.cmd, &bi), "vkBeginCommandBuffer (transfer)"))
        {
            return nullptr;
        }
        // Whatever the queue is still executing when this batch reaches
        // it — the previous frame, when a ring-full flush submits
        // between frames — may read or write the buffers and images the
        // batch copies into. One barrier at the top orders every
        // transfer behind that work and makes its writes available; the
        // per-image layout transitions add the image-specific
        // dependencies on top of it.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(batch.cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
        batch.id = m_next_transfer_batch_id++;
        batch.state = transfer_batch::batch_state::recording;
        batch.recorded = false;
        batch.submitted = false;
        batch.written_buffers.clear();
        m_open_transfer_batch = index;
        return &batch;
    }

    VkCommandBuffer vk_device::transfer_command_buffer()
    {
        transfer_batch* batch = open_transfer_batch();
        if (batch == nullptr)
        {
            return VK_NULL_HANDLE;
        }
        batch->recorded = true;
        return batch->cmd;
    }

    bool vk_device::stage_upload(const void* data, size_t size, staged_upload& out)
    {
        out = {};
        if (data == nullptr || size == 0)
        {
            LOG_ERR("vk_device::stage_upload: no source data (%zu bytes)", size);
            return false;
        }
        if (m_device_lost || m_staging_buffer == VK_NULL_HANDLE)
        {
            return false;
        }

        // An upload of more than half the ring takes a dedicated buffer:
        // it would otherwise wait for nearly every earlier upload to
        // complete before it could even be staged, for one resource.
        if (size <= m_staging_ring.capacity() / 2)
        {
            for (;;)
            {
                transfer_batch* batch = open_transfer_batch();
                if (batch == nullptr)
                {
                    return false;
                }
                if (const std::optional<uint64_t> offset = m_staging_ring.allocate(size, m_staging_alignment))
                {
                    std::memcpy(m_staging_mapped + *offset, data, size);
                    batch->recorded = true;
                    out.buffer = m_staging_buffer;
                    out.offset = *offset;
                    out.cmd = batch->cmd;
                    return true;
                }
                // The ring is full of bytes that submitted batches (or
                // the open one) still own. Submit what is open so it
                // can complete, then block on the oldest batch — the
                // one wait on the upload path, for one batch rather
                // than the whole queue — and try again with its bytes
                // reclaimed. Each round retires at least one batch, and
                // an empty ring fits anything up to half its capacity,
                // so the loop ends.
                LOG_DBG("vk_device::stage_upload: staging ring full (%llu of %llu bytes in use, %zu batches "
                        "submitted); waiting for the oldest batch",
                        static_cast<unsigned long long>(m_staging_ring.used()),
                        static_cast<unsigned long long>(m_staging_ring.capacity()),
                        m_staging_ring.sealed_count());
                flush_transfer_batch();
                if (!wait_oldest_transfer_batch())
                {
                    // Nothing was in flight (or the wait failed): the
                    // ring cannot be freed any further, so stage this
                    // one aside.
                    break;
                }
            }
        }

        transfer_batch* batch = open_transfer_batch();
        if (batch == nullptr)
        {
            return false;
        }
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const VmaAllocationCreateInfo ai = host_mapped_allocation(/*prefer_host=*/true);
        VmaAllocationInfo info{};
        transfer_batch::dedicated_staging staging{};
        if (!vk_check(vmaCreateBuffer(m_allocator, &bi, &ai, &staging.buffer, &staging.allocation, &info),
                      "vmaCreateBuffer (dedicated staging)"))
        {
            return false;
        }
        if (info.pMappedData == nullptr)
        {
            LOG_ERR("vk_device::stage_upload: the dedicated staging allocation is not mapped");
            vmaDestroyBuffer(m_allocator, staging.buffer, staging.allocation);
            return false;
        }
        std::memcpy(info.pMappedData, data, size);
        // Released with the batch, once its fence proves the copy done.
        batch->dedicated.push_back(staging);
        batch->recorded = true;
        LOG_DBG("vk_device::stage_upload: %zu bytes staged in a dedicated buffer for transfer batch %llu",
                size,
                static_cast<unsigned long long>(batch->id));
        out.buffer = staging.buffer;
        out.offset = 0;
        out.cmd = batch->cmd;
        return true;
    }

    void
    vk_device::record_buffer_copy(const staged_upload& source, VkBuffer dst, VkDeviceSize dst_offset, VkDeviceSize size)
    {
        if (source.cmd == VK_NULL_HANDLE)
        {
            return;
        }
        if (m_open_transfer_batch == k_no_batch || m_transfer_batches[m_open_transfer_batch].cmd != source.cmd)
        {
            // stage_upload's batch is always the open one (nothing
            // flushes in between); a staged upload from anywhere else
            // would copy into a batch that is gone.
            LOG_ERR("vk_device::record_buffer_copy: the staged upload's transfer batch is no longer open; %llu bytes "
                    "not copied",
                    static_cast<unsigned long long>(size));
            return;
        }
        transfer_batch& batch = m_transfer_batches[m_open_transfer_batch];
        // Two copies into one buffer within a batch are a
        // write-after-write hazard between transfer commands; the second
        // waits for the first. A buffer written once needs nothing.
        if (std::find(batch.written_buffers.begin(), batch.written_buffers.end(), dst) != batch.written_buffers.end())
        {
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(source.cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 1,
                                 &mb,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr);
        }
        else
        {
            batch.written_buffers.push_back(dst);
        }
        VkBufferCopy region{};
        region.srcOffset = source.offset;
        region.dstOffset = dst_offset;
        region.size = size;
        vkCmdCopyBuffer(source.cmd, source.buffer, dst, 1, &region);
    }

    bool vk_device::flush_transfer_batch()
    {
        if (m_open_transfer_batch == k_no_batch)
        {
            return true;
        }
        transfer_batch& batch = m_transfer_batches[m_open_transfer_batch];
        m_open_transfer_batch = k_no_batch;
        batch.written_buffers.clear();

        if (!batch.recorded)
        {
            // Begun for nothing: back to the pool, no submission. The
            // next open resets the buffer, so the begin needs no end.
            batch.state = transfer_batch::batch_state::idle;
            return true;
        }

        // Every copy above is a transfer write; make all of them
        // available to whatever follows in queue order — vertex and
        // index fetches, uniform and storage reads, indirect reads,
        // later copies — in one barrier per batch. Image uploads also
        // carry their own layout transitions with the sampling stages
        // as destination.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(batch.cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);

        // From here on the batch is in flight whether or not the
        // submission succeeds: its ring bytes were sealed to it, and
        // retiring it in order (without a fence wait when nothing was
        // submitted) is what keeps the ring's FIFO consistent.
        batch.state = transfer_batch::batch_state::in_flight;
        m_staging_ring.seal(batch.id);
        const bool ended = vk_check(vkEndCommandBuffer(batch.cmd), "vkEndCommandBuffer (transfer)");
        if (!ended || m_device_lost)
        {
            LOG_ERR("vk_device: transfer batch %llu dropped; the uploads it carried never reach the GPU",
                    static_cast<unsigned long long>(batch.id));
            return false;
        }
        if (!vk_check(vkResetFences(m_device, 1, &batch.fence), "vkResetFences (transfer)"))
        {
            LOG_ERR("vk_device: transfer batch %llu dropped; the uploads it carried never reach the GPU",
                    static_cast<unsigned long long>(batch.id));
            return false;
        }
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &batch.cmd;
        if (!check_queue_result(vkQueueSubmit(m_graphics_queue, 1, &si, batch.fence), "vkQueueSubmit (transfer)"))
        {
            LOG_ERR("vk_device: transfer batch %llu dropped; the uploads it carried never reach the GPU",
                    static_cast<unsigned long long>(batch.id));
            return false;
        }
        batch.submitted = true;
        return true;
    }

    vk_device::transfer_batch* vk_device::oldest_transfer_batch()
    {
        transfer_batch* oldest = nullptr;
        for (transfer_batch& batch : m_transfer_batches)
        {
            if (batch.state == transfer_batch::batch_state::in_flight && (oldest == nullptr || batch.id < oldest->id))
            {
                oldest = &batch;
            }
        }
        return oldest;
    }

    void vk_device::retire_transfer_batch(transfer_batch& batch)
    {
        m_staging_ring.retire(batch.id);
        for (const transfer_batch::dedicated_staging& staging : batch.dedicated)
        {
            vmaDestroyBuffer(m_allocator, staging.buffer, staging.allocation);
        }
        batch.dedicated.clear();
        batch.submitted = false;
        batch.recorded = false;
        batch.state = transfer_batch::batch_state::idle;
    }

    void vk_device::retire_transfer_batches()
    {
        // Oldest first: a fence that has signaled proves every earlier
        // submission on the queue complete, and one that has not stops
        // the walk, since nothing after it can be done either.
        for (transfer_batch* batch = oldest_transfer_batch(); batch != nullptr; batch = oldest_transfer_batch())
        {
            if (batch->submitted)
            {
                const VkResult status = vkGetFenceStatus(m_device, batch->fence);
                if (status == VK_NOT_READY)
                {
                    return;
                }
                if (status != VK_SUCCESS)
                {
                    check_queue_result(status, "vkGetFenceStatus (transfer)");
                    return;
                }
            }
            retire_transfer_batch(*batch);
        }
    }

    bool vk_device::transfer_batch_live_up_to(uint64_t batch_id) const
    {
        for (const transfer_batch& batch : m_transfer_batches)
        {
            if (batch.state != transfer_batch::batch_state::idle && batch.id <= batch_id)
            {
                return true;
            }
        }
        return false;
    }

    void vk_device::discard_transfer_batches()
    {
        for (transfer_batch& batch : m_transfer_batches)
        {
            if (batch.state != transfer_batch::batch_state::idle)
            {
                retire_transfer_batch(batch);
            }
        }
        m_open_transfer_batch = k_no_batch;
    }

    bool vk_device::wait_oldest_transfer_batch()
    {
        transfer_batch* batch = oldest_transfer_batch();
        if (batch == nullptr)
        {
            return false;
        }
        if (batch->submitted && !check_queue_result(vkWaitForFences(m_device, 1, &batch->fence, VK_TRUE, UINT64_MAX),
                                                    "vkWaitForFences (transfer)"))
        {
            return false;
        }
        retire_transfer_batch(*batch);
        return true;
    }

    void vk_device::resolve_depth_formats()
    {
        const auto supports_depth_attachment = [this](VkFormat format)
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
        };
        struct slot
        {
            texture_format format;
            const char* name;
            size_t index;
        };
        const std::array<slot, 3> slots{{{texture_format::depth24, "depth24", 0},
                                         {texture_format::depth32_float, "depth32_float", 1},
                                         {texture_format::depth24_stencil8, "depth24_stencil8", 2}}};
        for (const slot& s : slots)
        {
            const VkFormat preferred = to_vk_format(s.format);
            const VkFormat chosen = select_depth_format(s.format, supports_depth_attachment);
            if (chosen == VK_FORMAT_UNDEFINED)
            {
                LOG_FTL("Vulkan: no depth attachment format available for %s (the spec mandates D32_SFLOAT)", s.name);
                throw std::runtime_error{"no Vulkan depth attachment format"};
            }
            m_depth_formats[s.index] = chosen;
            if (chosen == preferred)
            {
                LOG_INF("Vulkan depth format %s: %s", s.name, depth_format_name(chosen));
            }
            else
            {
                LOG_INF("Vulkan depth format %s: %s (%s is not a depth attachment format on this device)",
                        s.name,
                        depth_format_name(chosen),
                        depth_format_name(preferred));
            }
        }
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
        if (!vk_check(vkCreateSampler(m_device, &si, nullptr, &m_fallback_sampler), "vkCreateSampler (fallback)"))
        {
            m_fallback_sampler = VK_NULL_HANDLE;
            throw std::runtime_error{"vkCreateSampler (fallback) failed"};
        }
    }

    void vk_device::mark_device_lost(const char* what)
    {
        if (m_device_lost)
        {
            return;
        }
        m_device_lost = true;
        LOG_FTL("%s reported VK_ERROR_DEVICE_LOST: the Vulkan device is gone. No further work is submitted or "
                "presented; the engine shuts down at the end of this frame",
                what);
    }

    bool vk_device::check_queue_result(VkResult result, const char* what)
    {
        if (result == VK_SUCCESS)
        {
            return true;
        }
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mark_device_lost(what);
            return false;
        }
        return vk_check(result, what);
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
                attachment.format = m_surface_format.format;
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
                attachment.format = vk_format_for(m_swapchain_depth_format);
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
        const VkResult rp_result = vkCreateRenderPass(m_device, &rpi, nullptr, &new_render_pass);
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
            const size_t image_count = m_swapchain_image_views.size();
            v.framebuffers.resize(image_count * m_frames_in_flight, VK_NULL_HANDLE);
            for (size_t i = 0; i < image_count; ++i)
            {
                for (uint32_t slot = 0; slot < m_frames_in_flight; ++slot)
                {
                    std::array<VkImageView, 2> views{m_swapchain_image_views[i], m_swapchain_depth_views[slot]};
                    VkFramebufferCreateInfo fbi{};
                    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    fbi.renderPass = new_render_pass;
                    fbi.attachmentCount = variant_uses_depth ? 2 : 1;
                    fbi.pAttachments = views.data();
                    fbi.width = m_swapchain_extent.width;
                    fbi.height = m_swapchain_extent.height;
                    fbi.layers = 1;
                    VkFramebuffer& framebuffer = v.framebuffers[i * m_frames_in_flight + slot];
                    const VkResult fb_result = vkCreateFramebuffer(m_device, &fbi, nullptr, &framebuffer);
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
                const VkResult fb_result = vkCreateFramebuffer(m_device, &fbi, nullptr, &v.framebuffers[0]);
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
        return m_query_sets.lookup(h.id);
    }
    PFN_vkCmdBeginDebugUtilsLabelEXT vk_device::cmd_begin_debug_label() const noexcept
    {
        return m_cmd_begin_debug_label;
    }
    PFN_vkCmdEndDebugUtilsLabelEXT vk_device::cmd_end_debug_label() const noexcept
    {
        return m_cmd_end_debug_label;
    }
    VkInstance vk_device::instance() const noexcept
    {
        return m_instance;
    }
    VkDevice vk_device::vk_handle() const noexcept
    {
        return m_device;
    }
    VkPhysicalDevice vk_device::physical_device() const noexcept
    {
        return m_physical_device;
    }
    VkQueue vk_device::graphics_queue() const noexcept
    {
        return m_graphics_queue;
    }
    uint32_t vk_device::graphics_queue_family() const noexcept
    {
        return m_graphics_queue_family;
    }
    VmaAllocator vk_device::allocator() const noexcept
    {
        return m_allocator;
    }
    VkPipelineCache vk_device::pipeline_cache() const noexcept
    {
        return m_pipeline_cache;
    }
    bool vk_device::device_lost() const noexcept
    {
        return m_device_lost;
    }
    VkFormat vk_device::vk_format_for(texture_format format) const noexcept
    {
        VkFormat resolved = VK_FORMAT_UNDEFINED;
        switch (format)
        {
        case texture_format::depth24:
            resolved = m_depth_formats[0];
            break;
        case texture_format::depth32_float:
            resolved = m_depth_formats[1];
            break;
        case texture_format::depth24_stencil8:
            resolved = m_depth_formats[2];
            break;
        default:
            break;
        }
        // Before resolve_depth_formats has run (or for a colour
        // format) the nominal translation stands.
        return resolved != VK_FORMAT_UNDEFINED ? resolved : to_vk_format(format);
    }
    uint64_t vk_device::swapchain_generation() const noexcept
    {
        return m_swapchain_generation;
    }
    uint32_t vk_device::swapchain_image_count() const noexcept
    {
        return static_cast<uint32_t>(m_swapchain_images.size());
    }
    uint32_t vk_device::current_swapchain_image_index() const noexcept
    {
        return m_current_image_index;
    }
    bool vk_device::have_current_swapchain_image() const noexcept
    {
        return m_have_current_image;
    }
    uint32_t vk_device::swapchain_framebuffer_index(uint32_t image_index) const noexcept
    {
        return image_index * m_frames_in_flight + m_frame_slot;
    }
    uint32_t vk_device::frames_in_flight() const noexcept
    {
        return m_frames_in_flight;
    }
    uint32_t vk_device::frame_slot() const noexcept
    {
        return m_frame_slot;
    }
    bool vk_device::extended_dynamic_state_enabled() const noexcept
    {
        return m_extended_dynamic_state_enabled;
    }
    PFN_vkCmdBindVertexBuffers2EXT vk_device::cmd_bind_vertex_buffers2() const noexcept
    {
        return m_cmd_bind_vertex_buffers2;
    }
} // namespace rendering_engine::gpu::backend::vulkan
