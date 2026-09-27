// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_overlay_renderer.cpp
 * @brief The Vulkan @ref rendering_engine::gpu::overlay_renderer: Dear
 *        ImGui's Vulkan renderer backend (every @c ImGui_ImplVulkan_*
 *        call in the engine) driven against @c vk_device, and
 *        @c vk_device::create_overlay_renderer, which builds it.
 *
 * Compiled only when the build links ImGui (@c ALPHAENGINE_HAS_IMGUI,
 * Debug configurations); otherwise the device has no overlay renderer.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#ifdef ALPHAENGINE_HAS_IMGUI

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_command_encoder.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/overlay_renderer.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        void check_vk_result(VkResult result)
        {
            if (result != VK_SUCCESS)
            {
                LOG_ERR("overlay: Vulkan error in ImGui backend (VkResult=%d)", static_cast<int>(result));
            }
        }

        struct vk_overlay_renderer final : overlay_renderer
        {
            explicit vk_overlay_renderer(vk_device& device) : m_device(device) {}

            vk_overlay_renderer(const vk_overlay_renderer&) = delete;
            vk_overlay_renderer& operator=(const vk_overlay_renderer&) = delete;

            bool init() override;
            void shutdown() override;
            void new_frame() override;
            void render(render_pass_encoder& encoder) override;
            overlay_texture texture(gpu::texture handle) override;
            void retain_textures(std::span<const uint64_t> ids) override;

        private:
            // A texture registered with ImGui_ImplVulkan_AddTexture: the
            // image view the descriptor set was written for, and the set
            // ImGui's image widgets draw it through.
            struct texture_binding
            {
                VkImageView view{VK_NULL_HANDLE};
                VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
            };

            VkRenderPass acquire_ui_render_pass();
            bool refresh_pipeline(VkRenderPass native_pass);
            void release_binding(texture_binding& binding);
            void defer_release_binding(texture_binding& binding);
            void clear_bindings();

            vk_device& m_device;

            // Set once init succeeds; every entry point early-outs until
            // then and after shutdown.
            bool m_live{false};

            // Set by new_frame, cleared once render has recorded that
            // frame's draw data. Guards against recording a frame that
            // was never built, or the same frame twice.
            bool m_frame_open{false};

            // The VkRenderPass the ImGui Vulkan pipeline was last built
            // for. The debug pass's own render pass can be retired and
            // rebuilt independently of a swapchain change (any acquire
            // through vk_device::acquire_render_pass may hand back a
            // different object); render reads the pass it is actually
            // recording into off the encoder and rebuilds the pipeline
            // whenever that differs from this, rather than re-deriving
            // the pass's load/store arguments and guessing they still
            // match what the debug pass begins.
            VkRenderPass m_render_pass{VK_NULL_HANDLE};

            // ImGui::Image wants an ImTextureID, which on Vulkan is a
            // VkDescriptorSet registered through
            // ImGui_ImplVulkan_AddTexture. Every off-screen texture in
            // this engine is transitioned to
            // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL immediately on
            // creation and rests there between render passes (see
            // vk_device::create_texture), so that layout is always the
            // right one to bake into the descriptor — including on the
            // very first frame, before any pass has touched the texture.
            //
            // Cached per gpu::texture (keyed by its handle id) so a
            // texture drawn several frames in a row reuses one descriptor
            // set; rebuilt when the texture is recreated (its image view
            // changes, e.g. on a resize) and pruned once nothing asks for
            // it any more (see retain_textures), so a window resize does
            // not leak a descriptor set per resize event.
            std::unordered_map<uint64_t, texture_binding> m_texture_bindings;
        };

        // The render pass the debug pass draws into: swapchain target,
        // colour loaded (the UI/scene already composited), no depth.
        // ImGui builds its pipeline against this pass, so it must match
        // what the debug pass begins. The debug pass leaves depth.load
        // at its default (clear) and acquire_render_pass keys on it even
        // when depth is unused, so the same value is passed here and
        // the cache hands back the very VkRenderPass the pass records
        // into. Null when the device has no swapchain target.
        VkRenderPass vk_overlay_renderer::acquire_ui_render_pass()
        {
            const gpu::render_target swapchain = m_device.swapchain_target();
            auto* target = m_device.lookup_render_target(swapchain);
            if (target == nullptr)
            {
                LOG_ERR("overlay: no swapchain render target for the ImGui Vulkan pipeline");
                return VK_NULL_HANDLE;
            }
            return m_device.acquire_render_pass(
                *target, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR, /*use_depth=*/false);
        }

        bool vk_overlay_renderer::init()
        {
            if (m_live)
            {
                return true;
            }

            // Acquire the same render pass the debug pass draws into
            // (see acquire_ui_render_pass) and remember it, so a later
            // rebuild is noticed before the first record against the new
            // one.
            VkRenderPass ui_render_pass = acquire_ui_render_pass();
            if (ui_render_pass == VK_NULL_HANDLE)
            {
                LOG_ERR("overlay: acquire_render_pass returned null for ImGui Vulkan init");
                return false;
            }

            // ImGui cycles its own host-visible vertex / index buffers
            // through ImageCount sets, one per RenderDrawData call, so the
            // count must cover every frame the device keeps in flight: a
            // set is rewritten only once the frame that read it has
            // retired. The backend also requires at least two.
            const uint32_t image_count = std::max(m_device.swapchain_image_count(), m_device.frames_in_flight());
            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion = VK_API_VERSION_1_0;
            init_info.Instance = m_device.instance();
            init_info.PhysicalDevice = m_device.physical_device();
            init_info.Device = m_device.vk_handle();
            init_info.QueueFamily = m_device.graphics_queue_family();
            init_info.Queue = m_device.graphics_queue();
            // Leave DescriptorPool null and let the backend own a pool
            // sized for the font atlas and every texture the overlay
            // registers through ImGui_ImplVulkan_AddTexture (see
            // texture); avoids depending on the engine pool's descriptor
            // budget / flags.
            init_info.DescriptorPool = VK_NULL_HANDLE;
            init_info.DescriptorPoolSize = 64;
            init_info.MinImageCount = image_count < 2 ? 2 : image_count;
            init_info.ImageCount = image_count < 2 ? 2 : image_count;
            // The device's pipeline cache, so the overlay's pipeline (and
            // its rebuild after every swapchain rebuild) is served from
            // and persisted with the engine's own.
            init_info.PipelineCache = m_device.pipeline_cache();
            init_info.PipelineInfoMain.RenderPass = ui_render_pass;
            init_info.PipelineInfoMain.Subpass = 0;
            init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering = false;
            init_info.Allocator = nullptr;
            init_info.CheckVkResultFn = check_vk_result;
            if (!ImGui_ImplVulkan_Init(&init_info))
            {
                LOG_ERR("overlay: ImGui_ImplVulkan_Init failed");
                return false;
            }

            m_render_pass = ui_render_pass;
            m_live = true;
            return true;
        }

        void vk_overlay_renderer::shutdown()
        {
            if (!m_live)
            {
                return;
            }

            // The render queue must be idle before tearing the backend's
            // GPU resources down, and every descriptor-set release
            // retain_textures deferred while the run was live (see
            // defer_release_binding) must have actually run by now too,
            // or its captured VkDescriptorSet dangles once
            // ImGui_ImplVulkan_Shutdown reclaims the pool it came from.
            m_device.flush_pending_destroys();
            // Release the remaining descriptor sets before the backend's
            // descriptor pool goes with ImGui_ImplVulkan_Shutdown.
            clear_bindings();
            ImGui_ImplVulkan_Shutdown();
            m_live = false;
            m_frame_open = false;
            m_render_pass = VK_NULL_HANDLE;
        }

        void vk_overlay_renderer::new_frame()
        {
            if (!m_live)
            {
                return;
            }
            ImGui_ImplVulkan_NewFrame();
            m_frame_open = true;
        }

        // Rebuild ImGui's main pipeline against @p native_pass, the render
        // pass the encoder is actually recording into, if that differs
        // from the one the pipeline was last built for. Called right
        // before recording, inside the debug pass: the pass only changes
        // when the swapchain was rebuilt, which waited the device idle, so
        // no frame in flight still binds the old pipeline and this frame
        // has not bound it yet, so ImGui may destroy it here. Only the
        // pipeline is rebuilt — the font texture, vertex / index buffers
        // and descriptor pool survive. Returns false when no pipeline
        // could be built (the pass is not open this frame); the caller
        // then skips this frame's overlay.
        bool vk_overlay_renderer::refresh_pipeline(VkRenderPass native_pass)
        {
            if (native_pass == VK_NULL_HANDLE)
            {
                return false;
            }
            if (native_pass == m_render_pass)
            {
                return true;
            }
            ImGui_ImplVulkan_PipelineInfo pipeline_info{};
            pipeline_info.RenderPass = native_pass;
            pipeline_info.Subpass = 0;
            pipeline_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            ImGui_ImplVulkan_CreateMainPipeline(&pipeline_info);
            m_render_pass = native_pass;
            LOG_INF("overlay: ImGui Vulkan pipeline rebuilt for a new debug-pass render pass");
            return true;
        }

        void vk_overlay_renderer::render(render_pass_encoder& encoder)
        {
            if (!m_live || !m_frame_open)
            {
                return;
            }
            ImDrawData* draw_data = ImGui::GetDrawData();
            if (draw_data == nullptr)
            {
                return;
            }

            // Every render pass encoder this backend hands out is a
            // vk_render_pass_encoder. Its command buffer is null while
            // the pass is not open — no swapchain image this frame
            // (minimised) — so nothing is recorded outside a render pass.
            const auto& vk_encoder = static_cast<const vk_render_pass_encoder&>(encoder);
            VkCommandBuffer cmd = vk_encoder.open_command_buffer();
            if (cmd != VK_NULL_HANDLE && refresh_pipeline(vk_encoder.open_render_pass()))
            {
                ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
            }
            m_frame_open = false;
        }

        overlay_texture vk_overlay_renderer::texture(gpu::texture handle)
        {
            overlay_texture result{};
            if (!m_live || !handle.valid())
            {
                return result;
            }

            vk_texture* tex = m_device.lookup_texture(handle);
            if (tex == nullptr || tex->view == VK_NULL_HANDLE)
            {
                return result;
            }
            result.width = tex->width;
            result.height = tex->height;

            texture_binding& binding = m_texture_bindings[handle.id];
            if (binding.descriptor_set != VK_NULL_HANDLE && binding.view != tex->view)
            {
                defer_release_binding(binding);
            }
            if (binding.descriptor_set == VK_NULL_HANDLE)
            {
                binding.view = tex->view;
                binding.descriptor_set =
                    ImGui_ImplVulkan_AddTexture(tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            result.id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(binding.descriptor_set));
            return result;
        }

        // Runs every frame the overlay registers textures, so a dropped
        // binding may still be drawn by a frame or two in flight; the
        // release defers.
        void vk_overlay_renderer::retain_textures(std::span<const uint64_t> ids)
        {
            for (auto it = m_texture_bindings.begin(); it != m_texture_bindings.end();)
            {
                if (std::find(ids.begin(), ids.end(), it->first) == ids.end())
                {
                    defer_release_binding(it->second);
                    it = m_texture_bindings.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // Immediate release: only safe once nothing can still be reading
        // the descriptor set, i.e. with the queue already idle (see
        // clear_bindings).
        void vk_overlay_renderer::release_binding(texture_binding& binding)
        {
            if (binding.descriptor_set != VK_NULL_HANDLE)
            {
                ImGui_ImplVulkan_RemoveTexture(binding.descriptor_set);
            }
            binding = texture_binding{};
        }

        // Same, but for a binding dropped mid-run (a texture the overlay
        // no longer draws, or one rebuilt because the texture it names
        // was recreated by a resize): the draw data of a frame or two
        // still in flight may reference the descriptor set through
        // ImGui_ImplVulkan_RenderDrawData, so freeing it right here — as
        // release_binding does — races the GPU once several frames are
        // in flight (VUID-vkFreeDescriptorSets-pDescriptorSets-00309).
        // ImGui_ImplVulkan_RemoveTexture runs instead through the
        // device's own deferred-destroy queue, gated on the same
        // submission serial every other Vulkan resource is.
        void vk_overlay_renderer::defer_release_binding(texture_binding& binding)
        {
            if (binding.descriptor_set != VK_NULL_HANDLE)
            {
                const VkDescriptorSet descriptor_set = binding.descriptor_set;
                m_device.enqueue_destroy([descriptor_set] { ImGui_ImplVulkan_RemoveTexture(descriptor_set); });
            }
            binding = texture_binding{};
        }

        // Called once, at shutdown, after the queue has already been
        // waited idle: nothing can still be reading the descriptor sets,
        // so releasing them immediately — ahead of
        // ImGui_ImplVulkan_Shutdown reclaiming the pool they came from —
        // is safe.
        void vk_overlay_renderer::clear_bindings()
        {
            for (auto& [id, binding] : m_texture_bindings)
            {
                release_binding(binding);
            }
            m_texture_bindings.clear();
        }
    } // namespace

    std::unique_ptr<overlay_renderer> vk_device::create_overlay_renderer()
    {
        return std::make_unique<vk_overlay_renderer>(*this);
    }
} // namespace rendering_engine::gpu::backend::vulkan

#else // ALPHAENGINE_HAS_IMGUI

namespace rendering_engine::gpu::backend::vulkan
{
    std::unique_ptr<overlay_renderer> vk_device::create_overlay_renderer()
    {
        return nullptr;
    }
} // namespace rendering_engine::gpu::backend::vulkan

#endif // ALPHAENGINE_HAS_IMGUI
