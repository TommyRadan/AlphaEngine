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
#include <vector>

#include <cstdio>
#include <string>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_vulkan.h>

#include <core/log.hpp>
#include <core/settings.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_command_encoder.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/engine.hpp>

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

    namespace
    {
        constexpr const char* k_validation_layer = "VK_LAYER_KHRONOS_validation";

        bool layer_available(const char* name)
        {
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            for (const auto& layer : layers)
            {
                if (std::strcmp(layer.layerName, name) == 0)
                {
                    return true;
                }
            }
            return false;
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
            // guaranteed FIFO when neither is exposed. Mirrors the OpenGL
            // path, which sets a swap interval of 0 when vsync is disabled.
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

    void vk_device::init()
    {
        LOG_INF("Init gpu::backend::vulkan::vk_device");

        auto& eng = runtime::current_engine();
        // Fallback extent for a surface that leaves the size to the
        // application (see choose_swapchain_extent): the drawable's
        // pixel size, which is what the swapchain follows on a scaled
        // display; the logical settings size only when the window
        // cannot report one.
        if (eng.window != nullptr)
        {
            const window_extent drawable = eng.window->pixel_size();
            m_window_width = drawable.width;
            m_window_height = drawable.height;
        }
        if ((m_window_width == 0 || m_window_height == 0) && eng.settings != nullptr)
        {
            m_window_width = eng.settings->window.width;
            m_window_height = eng.settings->window.height;
        }

        create_instance();
        create_debug_messenger();
        create_surface();
        pick_physical_device();
        create_logical_device();
        create_command_pool();
        create_descriptor_pool();

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
        swap.has_depth = true;
        m_swapchain_target.id = m_render_targets.insert(swap);

        create_default_textures();

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
        if (m_device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_device);
        }

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

        // GPU is idle: anything queued by destroy() during the run
        // is now safe to actually free, and the active handle pools
        // below need to release whatever is still resident.
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
                if (!t.external && t.image != VK_NULL_HANDLE)
                {
                    vkDestroyImage(m_device, t.image, nullptr);
                    t.image = VK_NULL_HANDLE;
                }
                if (t.memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(m_device, t.memory, nullptr);
                    t.memory = VK_NULL_HANDLE;
                }
            });
        m_buffers.for_each(
            [&](vk_buffer& b)
            {
                if (b.mapped != nullptr)
                {
                    vkUnmapMemory(m_device, b.memory);
                    b.mapped = nullptr;
                }
                if (b.object != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(m_device, b.object, nullptr);
                    b.object = VK_NULL_HANDLE;
                }
                if (b.memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(m_device, b.memory, nullptr);
                    b.memory = VK_NULL_HANDLE;
                }
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

        m_pipelines.clear();
        m_shader_modules.clear();
        m_bind_group_layouts.clear();
        m_bind_groups.clear();
        m_samplers.clear();
        m_textures.clear();
        m_buffers.clear();
        m_render_targets.clear();

        destroy_sync_objects();
        destroy_swapchain();

        if (m_descriptor_pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
            m_descriptor_pool = VK_NULL_HANDLE;
        }
        if (m_command_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_device, m_command_pool, nullptr);
            m_command_pool = VK_NULL_HANDLE;
        }
        if (m_device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        if (m_surface != VK_NULL_HANDLE)
        {
            SDL_Vulkan_DestroySurface(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
        destroy_debug_messenger();
        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }

        m_have_current_image = false;
        m_acquire_attempted = false;
        m_present_pending = false;
        m_swapchain_suspended = false;
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
            if (SDL_getenv_unsafe("VK_LAYER_PATH") != nullptr)
            {
                return;
            }
            const char* base_path = SDL_GetBasePath();
            if (base_path == nullptr)
            {
                return;
            }
            const std::string manifest = std::string{base_path} + "VkLayer_khronos_validation.json";
            std::FILE* f = std::fopen(manifest.c_str(), "rb");
            if (f == nullptr)
            {
                return;
            }
            std::fclose(f);
            // Trim trailing slash so the loader's path concatenation
            // produces a well-formed lookup; SDL_GetBasePath returns a
            // path with a trailing separator on every platform.
            std::string layer_path{base_path};
            if (!layer_path.empty() && (layer_path.back() == '/' || layer_path.back() == '\\'))
            {
                layer_path.pop_back();
            }
            SDL_setenv_unsafe("VK_LAYER_PATH", layer_path.c_str(), 1);
            LOG_INF("Published VK_LAYER_PATH=%s for bundled validation layer", layer_path.c_str());
        }
    } // namespace

    void vk_device::create_instance()
    {
        publish_layer_path_if_bundled();

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "AlphaEngine";
        app.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
        app.pEngineName = "AlphaEngine";
        app.engineVersion = VK_MAKE_VERSION(0, 0, 1);
        app.apiVersion = VK_API_VERSION_1_2;

        uint32_t sdl_ext_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
        std::vector<const char*> extensions;
        if (sdl_extensions != nullptr)
        {
            for (uint32_t i = 0; i < sdl_ext_count; ++i)
            {
                extensions.push_back(sdl_extensions[i]);
            }
        }
        std::vector<const char*> layers;
#ifdef _DEBUG
        if (layer_available(k_validation_layer))
        {
            layers.push_back(k_validation_layer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            m_validation_enabled = true;
        }
        else
        {
            LOG_WRN("Vulkan validation layer not available; debug-build run will not be validated");
        }
#endif

        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (vkCreateInstance(&info, nullptr, &m_instance) != VK_SUCCESS)
        {
            LOG_FTL("vkCreateInstance failed");
            throw std::runtime_error{"vkCreateInstance failed"};
        }
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

    void vk_device::create_surface()
    {
        auto& eng = runtime::current_engine();
        if (eng.window == nullptr || eng.window->sdl_window() == nullptr)
        {
            LOG_FTL("vk_device::create_surface: window subsystem missing");
            throw std::runtime_error{"vk_device: window missing"};
        }
        if (!SDL_Vulkan_CreateSurface(eng.window->sdl_window(), m_instance, nullptr, &m_surface))
        {
            LOG_FTL("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
            throw std::runtime_error{"SDL_Vulkan_CreateSurface failed"};
        }
    }

    // -- Physical / logical device --------------------------------------

    void vk_device::pick_physical_device()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        if (count == 0)
        {
            throw std::runtime_error{"no Vulkan-capable GPUs"};
        }
        std::vector<VkPhysicalDevice> gpus(count);
        vkEnumeratePhysicalDevices(m_instance, &count, gpus.data());

        VkPhysicalDevice best = VK_NULL_HANDLE;
        bool best_discrete = false;
        uint32_t best_graphics = 0;
        uint32_t best_present = 0;

        for (auto gpu : gpus)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(gpu, &props);

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
                vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, m_surface, &present_supported);
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
            vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr);
            std::vector<VkExtensionProperties> exts(ext_count);
            vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, exts.data());
            bool has_swap = false;
            bool has_depth_clip_control = false;
            bool has_extended_dynamic_state = false;
            for (const auto& e : exts)
            {
                if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                {
                    has_swap = true;
                }
                else if (std::strcmp(e.extensionName, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME) == 0)
                {
                    has_depth_clip_control = true;
                }
                else if (std::strcmp(e.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0)
                {
                    has_extended_dynamic_state = true;
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
                m_depth_clip_control_enabled = has_depth_clip_control;
                m_extended_dynamic_state_enabled = has_extended_dynamic_state;
            }
        }
        if (best == VK_NULL_HANDLE)
        {
            throw std::runtime_error{"no suitable Vulkan GPU"};
        }

        m_physical_device = best;
        m_graphics_queue_family = best_graphics;
        m_present_queue_family = best_present;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &props);
        LOG_INF("Vulkan GPU: %s (api %u.%u.%u)",
                props.deviceName,
                VK_VERSION_MAJOR(props.apiVersion),
                VK_VERSION_MINOR(props.apiVersion),
                VK_VERSION_PATCH(props.apiVersion));
        vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_memory_properties);
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

        // Query the chained features for the optional extensions —
        // depth_clip_control and extended_dynamic_state both expose
        // their feature bit through @c VkPhysicalDeviceFeatures2.
        VkPhysicalDeviceDepthClipControlFeaturesEXT dcc_query{};
        dcc_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds_query{};
        eds_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 features2_query{};
        features2_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        void** features2_query_pnext = &features2_query.pNext;
        if (m_depth_clip_control_enabled)
        {
            *features2_query_pnext = &dcc_query;
            features2_query_pnext = &dcc_query.pNext;
        }
        if (m_extended_dynamic_state_enabled)
        {
            *features2_query_pnext = &eds_query;
            features2_query_pnext = &eds_query.pNext;
        }
        vkGetPhysicalDeviceFeatures2(m_physical_device, &features2_query);
        if (m_depth_clip_control_enabled && dcc_query.depthClipControl != VK_TRUE)
        {
            // Extension exposed but the feature bit is off — fall
            // back to building pipelines without the negativeOneToOne
            // hint and let the GL Z range fight Vulkan's clipping.
            m_depth_clip_control_enabled = false;
            LOG_WRN("VK_EXT_depth_clip_control extension exposed but depthClipControl feature unavailable; "
                    "GL-style projection matrices may be clipped");
        }
        if (m_extended_dynamic_state_enabled && eds_query.extendedDynamicState != VK_TRUE)
        {
            m_extended_dynamic_state_enabled = false;
            LOG_WRN("VK_EXT_extended_dynamic_state extension exposed but feature unavailable; "
                    "vertex_layout.stride==0 will collapse meshes to a point");
        }

        VkPhysicalDeviceFeatures features{};
        features.fillModeNonSolid = VK_TRUE;
        features.tessellationShader = VK_TRUE;
        features.multiDrawIndirect = VK_TRUE;

        std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (m_depth_clip_control_enabled)
        {
            device_extensions.push_back(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
        }
        if (m_extended_dynamic_state_enabled)
        {
            device_extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
        }

        VkPhysicalDeviceDepthClipControlFeaturesEXT dcc_feature{};
        dcc_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
        dcc_feature.depthClipControl = VK_TRUE;
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds_feature{};
        eds_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        eds_feature.extendedDynamicState = VK_TRUE;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.features = features;
        void** features2_pnext = &features2.pNext;
        if (m_depth_clip_control_enabled)
        {
            *features2_pnext = &dcc_feature;
            features2_pnext = &dcc_feature.pNext;
        }
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
        if (vkCreateDevice(m_physical_device, &info, nullptr, &m_device) != VK_SUCCESS)
        {
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

        LOG_INF("Vulkan logical device created (depth_clip_control: %s, extended_dynamic_state: %s)",
                m_depth_clip_control_enabled ? "on" : "off",
                m_extended_dynamic_state_enabled ? "on" : "off");
    }

    void vk_device::create_command_pool()
    {
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = m_graphics_queue_family;
        if (vkCreateCommandPool(m_device, &info, nullptr, &m_command_pool) != VK_SUCCESS)
        {
            throw std::runtime_error{"vkCreateCommandPool failed"};
        }
    }

    void vk_device::create_descriptor_pool()
    {
        const uint32_t max_sets = 256;
        std::array<VkDescriptorPoolSize, 4> sizes{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = 256;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = 256;
        sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[2].descriptorCount = 64;
        sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[3].descriptorCount = 64;

        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets = max_sets;
        info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        if (vkCreateDescriptorPool(m_device, &info, nullptr, &m_descriptor_pool) != VK_SUCCESS)
        {
            throw std::runtime_error{"vkCreateDescriptorPool failed"};
        }
    }

    // -- Swapchain ------------------------------------------------------

    bool vk_device::create_swapchain(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D extent)
    {
        m_surface_format = pick_surface_format(m_physical_device, m_surface);
        const bool vsync_enabled = runtime::current_engine().settings->window.vsync;
        m_present_mode = pick_present_mode(m_physical_device, m_surface, vsync_enabled);

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
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual, nullptr);
        m_swapchain_images.resize(actual);
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual, m_swapchain_images.data());

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

        const VkFormat depth_fmt = to_vk_format(m_swapchain_depth_format);
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
        const VkResult depth_result = vkCreateImage(m_device, &di, nullptr, &m_swapchain_depth_image);
        if (depth_result != VK_SUCCESS)
        {
            LOG_ERR("vkCreateImage (swapchain depth) failed: %s", vk_result_to_string(depth_result));
            destroy_swapchain();
            return false;
        }
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(m_device, m_swapchain_depth_image, &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const VkResult alloc_result = vkAllocateMemory(m_device, &mai, nullptr, &m_swapchain_depth_memory);
        if (alloc_result != VK_SUCCESS)
        {
            LOG_ERR("vkAllocateMemory (swapchain depth) failed: %s", vk_result_to_string(alloc_result));
            destroy_swapchain();
            return false;
        }
        vkBindImageMemory(m_device, m_swapchain_depth_image, m_swapchain_depth_memory, 0);
        VkImageViewCreateInfo dvi{};
        dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dvi.image = m_swapchain_depth_image;
        dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dvi.format = depth_fmt;
        dvi.subresourceRange.aspectMask = aspect_for_format(m_swapchain_depth_format);
        dvi.subresourceRange.levelCount = 1;
        dvi.subresourceRange.layerCount = 1;
        const VkResult depth_view_result = vkCreateImageView(m_device, &dvi, nullptr, &m_swapchain_depth_view);
        if (depth_view_result != VK_SUCCESS)
        {
            LOG_ERR("vkCreateImageView (swapchain depth) failed: %s", vk_result_to_string(depth_view_result));
            destroy_swapchain();
            return false;
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
        if (m_swapchain_depth_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_device, m_swapchain_depth_view, nullptr);
            m_swapchain_depth_view = VK_NULL_HANDLE;
        }
        if (m_swapchain_depth_image != VK_NULL_HANDLE)
        {
            vkDestroyImage(m_device, m_swapchain_depth_image, nullptr);
            m_swapchain_depth_image = VK_NULL_HANDLE;
        }
        if (m_swapchain_depth_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, m_swapchain_depth_memory, nullptr);
            m_swapchain_depth_memory = VK_NULL_HANDLE;
        }
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
        // image); only the image-available semaphore and the in-flight fence
        // are owned here.
        if (vkCreateSemaphore(m_device, &si, nullptr, &m_image_available) != VK_SUCCESS ||
            vkCreateFence(m_device, &fi, nullptr, &m_in_flight_fence) != VK_SUCCESS)
        {
            throw std::runtime_error{"vk sync objects"};
        }
    }

    void vk_device::destroy_sync_objects()
    {
        if (m_device == VK_NULL_HANDLE)
        {
            return;
        }
        if (m_image_available != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, m_image_available, nullptr);
            m_image_available = VK_NULL_HANDLE;
        }
        if (m_in_flight_fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_device, m_in_flight_fence, nullptr);
            m_in_flight_fence = VK_NULL_HANDLE;
        }
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
        // (plus context::init, once, with the drawable's pixel size). A
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
        if (m_device == VK_NULL_HANDLE || m_surface == VK_NULL_HANDLE)
        {
            return false;
        }
        // The surface, not the cached window size, is the authority on
        // the extent. An OS-driven out-of-date (display change, a
        // compositor decision) arrives without any resize hint, and
        // rebuilding at the cached size used to be a no-op that left
        // the swapchain out of date for every following acquire.
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
        // the previous frame's command buffer, and the present that may
        // still be reading the image it was handed. One frame in
        // flight makes this cheap, and a rebuild is rare.
        vkDeviceWaitIdle(m_device);

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
        texture_descriptor color_descriptor{};
        color_descriptor.dimension = texture_dimension::d2;
        color_descriptor.format = descriptor.color_format;
        color_descriptor.width = descriptor.width;
        color_descriptor.height = descriptor.height;
        color_descriptor.mipmaps = false;
        color_descriptor.min_filter = filter_mode::linear;
        color_descriptor.mag_filter = filter_mode::linear;
        color_descriptor.mipmap_filter = mipmap_mode::none;
        color_descriptor.address_u = address_mode::clamp_edge;
        color_descriptor.address_v = address_mode::clamp_edge;
        color_descriptor.address_w = address_mode::clamp_edge;
        const texture color = create_texture(color_descriptor);

        texture depth{};
        if (descriptor.with_depth)
        {
            texture_descriptor depth_descriptor{};
            depth_descriptor.dimension = texture_dimension::d2;
            depth_descriptor.format = descriptor.depth_format;
            depth_descriptor.width = descriptor.width;
            depth_descriptor.height = descriptor.height;
            depth_descriptor.mipmaps = false;
            depth_descriptor.min_filter = filter_mode::nearest;
            depth_descriptor.mag_filter = filter_mode::nearest;
            depth_descriptor.mipmap_filter = mipmap_mode::none;
            depth_descriptor.address_u = address_mode::clamp_edge;
            depth_descriptor.address_v = address_mode::clamp_edge;
            depth_descriptor.address_w = address_mode::clamp_edge;
            depth = create_texture(depth_descriptor);
        }

        vk_render_target record{};
        record.is_swapchain = false;
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.has_depth = descriptor.with_depth;
        record.color_attachment = color;
        record.depth_attachment = depth;

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
            if (record->color_attachment.valid())
            {
                destroy(record->color_attachment);
                record->color_attachment = {};
            }
            if (record->depth_attachment.valid())
            {
                destroy(record->depth_attachment);
                record->depth_attachment = {};
            }
            m_render_targets.remove(handle.id);
        }
    }

    texture vk_device::render_target_color_texture(render_target handle)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            return record->color_attachment;
        }
        return {};
    }

    texture vk_device::render_target_depth_texture(render_target handle)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            return record->depth_attachment;
        }
        return {};
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
        VkCommandBuffer cmd = vk_enc->release_command_buffer();
        if (cmd == VK_NULL_HANDLE)
        {
            encoder.reset();
            return;
        }
        const VkResult end_result = vkEndCommandBuffer(cmd);
        if (end_result != VK_SUCCESS)
        {
            LOG_ERR("vkEndCommandBuffer failed: %s", vk_result_to_string(end_result));
            encoder.reset();
            return;
        }

        if (!m_have_current_image)
        {
            // No swapchain image this frame: either work submitted
            // outside a frame bracket (the IBL prefilter at start-up)
            // or a frame whose passes never reached the swapchain.
            // Execute it synchronously. The fence and semaphores stay
            // untouched, so the next begin_frame has nothing to wait
            // for and end_frame nothing to present.
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            const VkResult submit_result = vkQueueSubmit(m_graphics_queue, 1, &si, VK_NULL_HANDLE);
            if (submit_result != VK_SUCCESS)
            {
                LOG_ERR("vkQueueSubmit (no-image) failed: %s", vk_result_to_string(submit_result));
            }
            vkQueueWaitIdle(m_graphics_queue);
            // The queue is idle, so this one-off (off-screen-only) command
            // buffer is finished and can be returned to the pool now.
            vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
            encoder.reset();
            return;
        }

        // begin_frame already waited this fence for the previous
        // frame, so it is signaled and idle. Reset it here, right
        // before the one submission that signals it again, rather
        // than at acquire time: a reset at acquire left the fence
        // unsignaled whenever the acquire failed (out-of-date
        // swapchain), and the next begin_frame then blocked forever
        // (issue #206).
        vkResetFences(m_device, 1, &m_in_flight_fence);

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &m_image_available;
        si.pWaitDstStageMask = &wait_stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &m_render_finished[m_current_image_index];
        const VkResult submit_result = vkQueueSubmit(m_graphics_queue, 1, &si, m_in_flight_fence);
        if (submit_result != VK_SUCCESS)
        {
            LOG_ERR("vkQueueSubmit failed: %s", vk_result_to_string(submit_result));
        }
        // The present itself belongs to the frame boundary; end_frame
        // issues it once the renderer has closed the frame.
        m_present_pending = true;
        encoder.reset();

        // The command buffer is in flight until this frame's fence
        // signals. Hand it back to the pool through the deferred queue,
        // which the next begin_frame drains only after vkWaitForFences —
        // so it is freed once the GPU is done, not leaked for the
        // lifetime of the run. (Previously release_command_buffer
        // detached it from the encoder but nothing ever freed it, so the
        // pool grew by one command buffer per frame and teardown of the
        // whole pile stalled shutdown for tens of seconds.)
        const VkDevice device = m_device;
        const VkCommandPool pool = m_command_pool;
        enqueue_destroy([device, pool, cmd] { vkFreeCommandBuffers(device, pool, 1, &cmd); });
    }

    void vk_device::enqueue_destroy(std::function<void()> fn)
    {
        if (fn)
        {
            m_pending_destroys.push_back(std::move(fn));
        }
    }

    void vk_device::drain_pending_destroys()
    {
        // Move out first so a destroy callback that itself enqueues
        // is captured into the next drain rather than running here.
        std::vector<std::function<void()>> drain;
        drain.swap(m_pending_destroys);
        for (auto& fn : drain)
        {
            fn();
        }
    }

    // -- Frame boundary ------------------------------------------------

    void vk_device::begin_frame()
    {
        if (!m_initialised)
        {
            return;
        }
        // Block until the previous frame's command buffer has finished
        // executing. This runs before the renderer records anything for
        // the new frame, so every host write that follows — the
        // per-frame camera / light / shadow UBOs, the per-draw model
        // matrices, instance re-uploads — lands in memory the GPU is no
        // longer reading. (The wait used to run lazily at the first
        // swapchain pass, after every off-screen pass had already
        // written its UBOs: the host-write / device-read race of
        // issue #169, still open at one frame in flight.)
        vkWaitForFences(m_device, 1, &m_in_flight_fence, VK_TRUE, UINT64_MAX);
        // The fence is signaled, so nothing enqueued for destruction
        // during the previous frame is still referenced by the GPU —
        // and no command buffer is open yet that could reference what
        // a material rebuilds this frame. This is the one in-frame
        // point where freeing is safe.
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
        if (m_have_current_image && m_present_pending)
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
            else if (r != VK_SUCCESS)
            {
                LOG_ERR("vkQueuePresentKHR failed: %s", vk_result_to_string(r));
            }
        }
        // An image acquired without a submission (the encoder failed to
        // record) is dropped rather than presented: its render-finished
        // semaphore was never signaled, so a present would wait forever.
        m_have_current_image = false;
        m_acquire_attempted = false;
        m_present_pending = false;

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
    }

    void vk_device::acquire_swapchain_image()
    {
        if (m_have_current_image || m_acquire_attempted)
        {
            return;
        }
        m_acquire_attempted = true;
        if (m_swapchain_suspended || m_swapchain == VK_NULL_HANDLE)
        {
            return;
        }
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const VkResult r = vkAcquireNextImageKHR(
                m_device, m_swapchain, UINT64_MAX, m_image_available, VK_NULL_HANDLE, &m_current_image_index);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            {
                // A suboptimal acquire still hands out an image and
                // signals the semaphore, so the frame has to use it;
                // the present's own result triggers the rebuild.
                m_have_current_image = true;
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
            LOG_ERR("vkAcquireNextImageKHR failed: %s", vk_result_to_string(r));
            return;
        }
    }

    VkCommandBuffer vk_device::begin_one_shot()
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_command_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(m_device, &ai, &cmd) != VK_SUCCESS)
        {
            return VK_NULL_HANDLE;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        return cmd;
    }

    void vk_device::end_one_shot(VkCommandBuffer cmd)
    {
        if (cmd == VK_NULL_HANDLE)
        {
            return;
        }
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(m_graphics_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphics_queue);
        vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
    }

    uint32_t vk_device::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const
    {
        for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; ++i)
        {
            if ((type_filter & (1u << i)) != 0u &&
                (m_memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        return 0;
    }

    // -- Render-pass cache ---------------------------------------------

    VkRenderPass vk_device::acquire_render_pass(vk_render_target& target,
                                                VkAttachmentLoadOp color_load,
                                                VkAttachmentLoadOp depth_load,
                                                bool use_depth)
    {
        // Reuse a matching variant if one already exists.
        for (const auto& v : target.variants)
        {
            if (v.color_load == color_load && v.depth_load == depth_load && v.use_depth == use_depth &&
                v.render_pass != VK_NULL_HANDLE)
            {
                return v.render_pass;
            }
        }

        const bool variant_uses_depth = use_depth && target.has_depth;

        std::array<VkAttachmentDescription, 2> attachments{};
        std::array<VkAttachmentReference, 2> refs{};
        uint32_t attachment_count = 1;

        attachments[0] = {};
        if (target.is_swapchain)
        {
            attachments[0].format = m_surface_format.format;
        }
        else if (auto* color_tex = m_textures.lookup(target.color_attachment.id))
        {
            attachments[0].format = color_tex->vk_format;
        }
        else
        {
            attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        }
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = color_load;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout =
            color_load == VK_ATTACHMENT_LOAD_OP_LOAD
                ? (target.is_swapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                : VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout =
            target.is_swapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        refs[0].attachment = 0;
        refs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if (variant_uses_depth)
        {
            attachment_count = 2;
            attachments[1] = {};
            if (target.is_swapchain)
            {
                attachments[1].format = to_vk_format(m_swapchain_depth_format);
            }
            else if (auto* depth_tex = m_textures.lookup(target.depth_attachment.id))
            {
                attachments[1].format = depth_tex->vk_format;
            }
            else
            {
                attachments[1].format = VK_FORMAT_D32_SFLOAT;
            }
            // Off-screen depth is sampled by later post passes (the velocity
            // pass reads sceneDepth), so it leaves the pass in the
            // shader-read layout the combined-image-sampler descriptor
            // expects, mirroring the off-screen colour attachment above. A
            // LOAD therefore resumes from that same layout. The swapchain
            // depth is never sampled, so it stays in the attachment layout.
            const VkImageLayout depth_rest_layout = target.is_swapchain
                                                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp = depth_load;
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout =
                depth_load == VK_ATTACHMENT_LOAD_OP_LOAD ? depth_rest_layout : VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[1].finalLayout = depth_rest_layout;
            refs[1].attachment = 1;
            refs[1].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &refs[0];
        subpass.pDepthStencilAttachment = variant_uses_depth ? &refs[1] : nullptr;

        // Two subpass dependencies. The EXTERNAL → 0 incoming dep
        // synchronises color/depth writes from a previous render
        // pass against this pass's writes — needed when consecutive
        // passes target the same swapchain image. The 0 → EXTERNAL
        // outgoing dep releases color writes from this pass for
        // FRAGMENT_SHADER reads in a subsequent pass — without it
        // tonemap's fragment shader can sample the scene HDR target
        // before scene_pass's color writes are visible, and the
        // sample silently returns undefined data (typically zeros).
        // That's exactly what was producing a black off-screen
        // target on NVIDIA: the cube draw issued, validation was
        // happy, but tonemap saw black because the writes hadn't
        // committed by the time the sample fired.
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask = deps[0].srcStageMask;
        deps[0].srcAccessMask = 0;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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
            LOG_ERR("vkCreateRenderPass failed: %s (color_load=%i depth_load=%i use_depth=%i)",
                    vk_result_to_string(rp_result),
                    static_cast<int>(color_load),
                    static_cast<int>(depth_load),
                    static_cast<int>(use_depth));
            return VK_NULL_HANDLE;
        }
        vk_render_target::variant new_variant{};
        new_variant.color_load = color_load;
        new_variant.depth_load = depth_load;
        new_variant.use_depth = use_depth;
        new_variant.render_pass = new_render_pass;
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
            v.framebuffers.resize(m_swapchain_image_views.size(), VK_NULL_HANDLE);
            for (size_t i = 0; i < m_swapchain_image_views.size(); ++i)
            {
                std::array<VkImageView, 2> views{m_swapchain_image_views[i], m_swapchain_depth_view};
                VkFramebufferCreateInfo fbi{};
                fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbi.renderPass = new_render_pass;
                fbi.attachmentCount = variant_uses_depth ? 2 : 1;
                fbi.pAttachments = views.data();
                fbi.width = m_swapchain_extent.width;
                fbi.height = m_swapchain_extent.height;
                fbi.layers = 1;
                const VkResult fb_result = vkCreateFramebuffer(m_device, &fbi, nullptr, &v.framebuffers[i]);
                if (fb_result != VK_SUCCESS)
                {
                    LOG_ERR("vkCreateFramebuffer (swapchain image %u) failed: %s",
                            static_cast<unsigned>(i),
                            vk_result_to_string(fb_result));
                }
            }
        }
        else
        {
            v.framebuffers.resize(1, VK_NULL_HANDLE);
            std::array<VkImageView, 2> views{};
            uint32_t view_count = 1;
            if (auto* color_tex = m_textures.lookup(target.color_attachment.id))
            {
                views[0] = color_tex->view;
            }
            if (variant_uses_depth)
            {
                if (auto* depth_tex = m_textures.lookup(target.depth_attachment.id))
                {
                    views[1] = depth_tex->view;
                    view_count = 2;
                }
            }
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
    VkCommandPool vk_device::command_pool() const noexcept
    {
        return m_command_pool;
    }
    VkDescriptorPool vk_device::descriptor_pool() const noexcept
    {
        return m_descriptor_pool;
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
    bool vk_device::depth_clip_control_enabled() const noexcept
    {
        return m_depth_clip_control_enabled;
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
