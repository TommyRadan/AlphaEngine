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
 * @file vk_device_pipeline.cpp
 * @brief @c vk_device member functions that build descriptor-set
 *        layouts, descriptor sets, and graphics / compute pipelines.
 *
 * The shader sources arrive as Vulkan-style GLSL with explicit
 * @c layout(set, binding) annotations and are cross-compiled to
 * SPIR-V upstream. The bind-group kinds map directly:
 *
 *   uniform_buffer  -> VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
 *   storage_buffer  -> VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
 *   storage_texture -> VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
 *   texture         -> VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
 *                      (sampler taken from the texture's built-in
 *                       VkSampler — matches the GL backend, which
 *                       bakes sampler state into the texture as well)
 *   sampler         -> ignored at the descriptor-set level for the
 *                      same reason; explicit sampler resources stand
 *                      in the API for future explicit-binding
 *                      backends.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <array>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        constexpr VkShaderStageFlags k_all_stages =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
            VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorType to_descriptor_type(binding_kind kind)
        {
            switch (kind)
            {
            case binding_kind::uniform_buffer:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case binding_kind::storage_buffer:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case binding_kind::storage_texture:
                return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case binding_kind::texture:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case binding_kind::sampler:
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            }
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
    } // namespace

    bind_group_layout vk_device::create_bind_group_layout(const bind_group_layout_descriptor& descriptor)
    {
        vk_bind_group_layout record{};
        record.descriptor = descriptor;

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(descriptor.entries.size());
        for (const auto& entry : descriptor.entries)
        {
            // Standalone sampler entries don't materialise as their
            // own descriptor binding — textures already carry a
            // built-in sampler. Keeping them in the layout is a
            // forward-compat hook for explicit binding backends.
            if (entry.kind == binding_kind::sampler)
            {
                continue;
            }
            VkDescriptorSetLayoutBinding b{};
            b.binding = entry.binding;
            b.descriptorType = to_descriptor_type(entry.kind);
            b.descriptorCount = 1;
            b.stageFlags = k_all_stages;
            bindings.push_back(b);
        }

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &record.object) != VK_SUCCESS)
        {
            LOG_FTL("vkCreateDescriptorSetLayout failed");
            return {};
        }

        bind_group_layout h{};
        h.id = m_bind_group_layouts.insert(record);
        return h;
    }

    void vk_device::destroy(bind_group_layout handle)
    {
        if (auto* record = m_bind_group_layouts.lookup(handle.id))
        {
            if (record->object != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_device, record->object, nullptr);
                record->object = VK_NULL_HANDLE;
            }
            m_bind_group_layouts.remove(handle.id);
        }
    }

    namespace
    {
        VkPipelineLayout
        build_pipeline_layout(VkDevice dev, vk_device& device, const std::vector<bind_group_layout>& layouts)
        {
            std::vector<VkDescriptorSetLayout> set_layouts;
            set_layouts.reserve(layouts.size());
            for (const auto& bgl : layouts)
            {
                if (auto* layout_record = device.lookup_bind_group_layout(bgl))
                {
                    set_layouts.push_back(layout_record->object);
                }
            }
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
            pli.pSetLayouts = set_layouts.data();
            VkPipelineLayout layout = VK_NULL_HANDLE;
            const VkResult r = vkCreatePipelineLayout(dev, &pli, nullptr, &layout);
            if (r != VK_SUCCESS)
            {
                LOG_ERR("vkCreatePipelineLayout failed: %s (sets=%u)",
                        vk_result_to_string(r),
                        static_cast<unsigned>(set_layouts.size()));
            }
            return layout;
        }
    } // namespace

    namespace
    {
        // Build a graphics VkPipeline against a specific render
        // pass. The caller (vk_device::graphics_pipeline_for) holds
        // the pipeline_descriptor and pipeline layout — this function
        // is the per-render-pass instantiation.
        VkPipeline build_graphics_pipeline(vk_device& device,
                                           const pipeline_descriptor& descriptor,
                                           VkPipelineLayout layout,
                                           VkRenderPass render_pass,
                                           bool y_flipped)
        {
            std::vector<VkPipelineShaderStageCreateInfo> stages;
            const auto attach = [&](shader_module module, VkShaderStageFlagBits stage_bit)
            {
                if (!module.valid())
                {
                    return;
                }
                auto* sm = device.lookup_shader_module(module);
                if (sm == nullptr || sm->object == VK_NULL_HANDLE)
                {
                    return;
                }
                VkPipelineShaderStageCreateInfo si{};
                si.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                si.stage = stage_bit;
                si.module = sm->object;
                si.pName = "main";
                stages.push_back(si);
            };
            attach(descriptor.vertex_shader, VK_SHADER_STAGE_VERTEX_BIT);
            attach(descriptor.fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT);
            attach(descriptor.geometry_shader, VK_SHADER_STAGE_GEOMETRY_BIT);
            attach(descriptor.tessellation_control_shader, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
            attach(descriptor.tessellation_evaluation_shader, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

            std::vector<VkVertexInputBindingDescription> vk_bindings;
            std::vector<VkVertexInputAttributeDescription> vk_attributes;
            for (uint32_t slot = 0; slot < descriptor.vertex_buffers.size(); ++slot)
            {
                const auto& vbl = descriptor.vertex_buffers[slot];
                VkVertexInputBindingDescription bd{};
                bd.binding = slot;
                bd.stride = vbl.stride;
                bd.inputRate = vbl.step_mode == vertex_step_mode::instance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                                           : VK_VERTEX_INPUT_RATE_VERTEX;
                vk_bindings.push_back(bd);
                for (const auto& attr : vbl.attributes)
                {
                    VkVertexInputAttributeDescription ad{};
                    ad.binding = slot;
                    ad.location = attr.location;
                    ad.format = to_vk_vertex_format(attr.type, attr.components);
                    ad.offset = attr.offset;
                    vk_attributes.push_back(ad);
                }
            }
            VkPipelineVertexInputStateCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vi.vertexBindingDescriptionCount = static_cast<uint32_t>(vk_bindings.size());
            vi.pVertexBindingDescriptions = vk_bindings.data();
            vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(vk_attributes.size());
            vi.pVertexAttributeDescriptions = vk_attributes.data();

            VkPipelineInputAssemblyStateCreateInfo ia{};
            ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            ia.topology = to_vk_topology(descriptor.topology);

            VkPipelineTessellationStateCreateInfo ts{};
            ts.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
            ts.patchControlPoints = descriptor.patch_control_points;
            const bool uses_tess =
                descriptor.topology == primitive_topology::patches && descriptor.patch_control_points > 0;

            // GL-style projection matrices (the engine ships these
            // through glm::perspective) emit clip-space Z in [-w, w].
            // VK_EXT_depth_clip_control's negativeOneToOne == VK_TRUE
            // tells Vulkan to use that range instead of the default
            // [0, w]; without it everything in the front half of the
            // view frustum is clipped before reaching rasterization.
            VkPipelineViewportDepthClipControlCreateInfoEXT dcc_info{};
            dcc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT;
            dcc_info.negativeOneToOne = VK_TRUE;

            VkPipelineViewportStateCreateInfo vp{};
            vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            vp.viewportCount = 1;
            vp.scissorCount = 1;
            if (device.depth_clip_control_enabled())
            {
                vp.pNext = &dcc_info;
            }

            VkPipelineRasterizationStateCreateInfo rs{};
            rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rs.polygonMode = to_vk_polygon_mode(descriptor.rasterizer.polygon);
            rs.cullMode = to_vk_cull_mode(descriptor.rasterizer.cull);
            rs.frontFace = to_vk_front_face(descriptor.rasterizer.front);
            // Caller intent is "CCW = front" in math-Y-up (world Z up
            // ⇒ NDC +Y). Swapchain targets render through a
            // negative-height viewport, which keeps that NDC winding
            // intact in framebuffer space — so the direct
            // CCW → VK_FRONT_FACE_COUNTER_CLOCKWISE mapping from
            // to_vk_front_face is what we want. Off-screen targets
            // render through Vulkan's default viewport, which has
            // framebuffer Y pointing the opposite way to the GL-style
            // NDC the engine produces; that single reflection flips
            // every triangle's screen-space winding, so the off-screen
            // variant inverts the front-face enum to compensate.
            if (!y_flipped)
            {
                rs.frontFace =
                    rs.frontFace == VK_FRONT_FACE_CLOCKWISE ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
            }
            rs.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo ds{};
            ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            ds.depthTestEnable = descriptor.depth.test_enabled ? VK_TRUE : VK_FALSE;
            ds.depthWriteEnable = descriptor.depth.write_enabled ? VK_TRUE : VK_FALSE;
            ds.depthCompareOp = to_vk_compare(descriptor.depth.compare);

            VkPipelineColorBlendAttachmentState cba{};
            cba.blendEnable = descriptor.blend.enabled ? VK_TRUE : VK_FALSE;
            cba.srcColorBlendFactor = to_vk_blend_factor(descriptor.blend.src);
            cba.dstColorBlendFactor = to_vk_blend_factor(descriptor.blend.dst);
            cba.colorBlendOp = to_vk_blend_op(descriptor.blend.op);
            cba.srcAlphaBlendFactor = to_vk_blend_factor(descriptor.blend.src);
            cba.dstAlphaBlendFactor = to_vk_blend_factor(descriptor.blend.dst);
            cba.alphaBlendOp = to_vk_blend_op(descriptor.blend.op);
            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1;
            cb.pAttachments = &cba;

            // Materials author their pipelines with @c stride==0 and
            // pass the real stride per draw via @c set_vertex_buffer.
            // Vulkan bakes stride into the pipeline by default, so
            // we mark it dynamic when VK_EXT_extended_dynamic_state is
            // available; the encoder then supplies the runtime stride
            // through @c vkCmdBindVertexBuffers2EXT. Without this,
            // stride==0 collapses every vertex onto vertex 0 and
            // the mesh draws as a single point.
            std::vector<VkDynamicState> dyn_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            if (device.extended_dynamic_state_enabled())
            {
                dyn_states.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT);
            }
            VkPipelineDynamicStateCreateInfo dyn{};
            dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dyn.dynamicStateCount = static_cast<uint32_t>(dyn_states.size());
            dyn.pDynamicStates = dyn_states.data();

            VkGraphicsPipelineCreateInfo gpi{};
            gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gpi.stageCount = static_cast<uint32_t>(stages.size());
            gpi.pStages = stages.data();
            gpi.pVertexInputState = &vi;
            gpi.pInputAssemblyState = &ia;
            gpi.pTessellationState = uses_tess ? &ts : nullptr;
            gpi.pViewportState = &vp;
            gpi.pRasterizationState = &rs;
            gpi.pMultisampleState = &ms;
            gpi.pDepthStencilState = &ds;
            gpi.pColorBlendState = &cb;
            gpi.pDynamicState = &dyn;
            gpi.layout = layout;
            gpi.renderPass = render_pass;

            VkPipeline result = VK_NULL_HANDLE;
            const VkResult r = vkCreateGraphicsPipelines(device.vk_handle(), VK_NULL_HANDLE, 1, &gpi, nullptr, &result);
            if (r != VK_SUCCESS)
            {
                LOG_ERR("vkCreateGraphicsPipelines failed: %s (stages=%u dcc=%s)",
                        vk_result_to_string(r),
                        static_cast<unsigned>(stages.size()),
                        device.depth_clip_control_enabled() ? "on" : "off");
            }
            return result;
        }
    } // namespace

    pipeline vk_device::create_pipeline(const pipeline_descriptor& descriptor)
    {
        vk_pipeline record{};
        record.descriptor = descriptor;

        record.layout = build_pipeline_layout(m_device, *this, descriptor.bind_group_layouts);
        if (record.layout == VK_NULL_HANDLE)
        {
            return {};
        }

        // Defer VkPipeline creation to the first render-pass that
        // binds it; we don't yet know which render pass(es) the
        // caller will use this pipeline against. graphics_pipeline_for
        // builds and caches a variant on demand.
        pipeline h{};
        h.id = m_pipelines.insert(record);
        return h;
    }

    VkPipeline vk_device::graphics_pipeline_for(pipeline handle, VkRenderPass render_pass, bool y_flipped)
    {
        auto* record = m_pipelines.lookup(handle.id);
        if (record == nullptr || record->is_compute || render_pass == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        for (const auto& v : record->graphics_variants)
        {
            if (v.render_pass == render_pass && v.y_flipped == y_flipped && v.object != VK_NULL_HANDLE)
            {
                return v.object;
            }
        }
        VkPipeline pipe = build_graphics_pipeline(*this, record->descriptor, record->layout, render_pass, y_flipped);
        if (pipe == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_device::graphics_pipeline_for: vkCreateGraphicsPipelines failed");
            return VK_NULL_HANDLE;
        }
        record->graphics_variants.push_back({render_pass, pipe, y_flipped});
        return pipe;
    }

    pipeline vk_device::create_compute_pipeline(const compute_pipeline_descriptor& descriptor)
    {
        vk_pipeline record{};
        record.is_compute = true;
        record.layout = build_pipeline_layout(m_device, *this, descriptor.bind_group_layouts);
        if (record.layout == VK_NULL_HANDLE)
        {
            return {};
        }

        auto* sm = m_shader_modules.lookup(descriptor.compute_shader.id);
        if (sm == nullptr)
        {
            vkDestroyPipelineLayout(m_device, record.layout, nullptr);
            return {};
        }

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = sm->object;
        stage.pName = "main";

        VkComputePipelineCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage = stage;
        cpi.layout = record.layout;
        if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpi, nullptr, &record.compute_object) != VK_SUCCESS)
        {
            vkDestroyPipelineLayout(m_device, record.layout, nullptr);
            return {};
        }

        pipeline h{};
        h.id = m_pipelines.insert(record);
        return h;
    }

    void vk_device::destroy(pipeline handle)
    {
        auto* record = m_pipelines.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        VkDevice dev = m_device;
        std::vector<VkPipeline> graphics_objects;
        graphics_objects.reserve(record->graphics_variants.size());
        for (auto& v : record->graphics_variants)
        {
            if (v.object != VK_NULL_HANDLE)
            {
                graphics_objects.push_back(v.object);
            }
        }
        VkPipeline compute_object = record->compute_object;
        VkPipelineLayout layout = record->layout;
        // The pipeline objects might still be bound by an in-flight
        // command buffer; defer the actual vkDestroy* until after
        // the next fence wait.
        enqueue_destroy(
            [dev, graphics_objects = std::move(graphics_objects), compute_object, layout]
            {
                for (VkPipeline p : graphics_objects)
                {
                    vkDestroyPipeline(dev, p, nullptr);
                }
                if (compute_object != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(dev, compute_object, nullptr);
                }
                if (layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(dev, layout, nullptr);
                }
            });
        record->graphics_variants.clear();
        record->compute_object = VK_NULL_HANDLE;
        record->layout = VK_NULL_HANDLE;
        m_pipelines.remove(handle.id);
    }

    bind_group vk_device::create_bind_group(const bind_group_descriptor& descriptor)
    {
        auto* layout_record = m_bind_group_layouts.lookup(descriptor.layout.id);
        if (layout_record == nullptr || layout_record->object == VK_NULL_HANDLE)
        {
            return {};
        }

        vk_bind_group record{};
        record.layout = descriptor.layout;
        record.entries = descriptor.entries;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_descriptor_pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout_record->object;
        const VkResult alloc_result = vkAllocateDescriptorSets(m_device, &ai, &record.descriptor_set);
        if (alloc_result != VK_SUCCESS)
        {
            LOG_ERR("vkAllocateDescriptorSets failed: %s", vk_result_to_string(alloc_result));
            return {};
        }

        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        std::vector<VkWriteDescriptorSet> writes;
        buffer_infos.reserve(descriptor.entries.size());
        image_infos.reserve(descriptor.entries.size());
        writes.reserve(descriptor.entries.size());

        for (const auto& entry : descriptor.entries)
        {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = record.descriptor_set;
            w.dstBinding = entry.binding;
            w.descriptorCount = 1;

            switch (entry.kind)
            {
            case binding_kind::uniform_buffer:
            case binding_kind::storage_buffer:
            {
                auto* buf = m_buffers.lookup(entry.buffer_value.id);
                if (buf == nullptr || buf->object == VK_NULL_HANDLE)
                {
                    continue;
                }
                VkDescriptorBufferInfo bi{};
                bi.buffer = buf->object;
                bi.range = VK_WHOLE_SIZE;
                buffer_infos.push_back(bi);
                w.descriptorType = entry.kind == binding_kind::uniform_buffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                                              : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w.pBufferInfo = &buffer_infos.back();
                writes.push_back(w);
                break;
            }
            case binding_kind::texture:
            {
                auto* tex = m_textures.lookup(entry.texture_value.id);
                if (tex == nullptr || tex->view == VK_NULL_HANDLE)
                {
                    continue;
                }
                VkDescriptorImageInfo ii{};
                ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                ii.imageView = tex->view;
                ii.sampler = tex->default_sampler;
                image_infos.push_back(ii);
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &image_infos.back();
                writes.push_back(w);
                break;
            }
            case binding_kind::storage_texture:
            {
                auto* tex = m_textures.lookup(entry.texture_value.id);
                if (tex == nullptr || tex->view == VK_NULL_HANDLE)
                {
                    continue;
                }
                VkDescriptorImageInfo ii{};
                ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                ii.imageView = tex->view;
                image_infos.push_back(ii);
                w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w.pImageInfo = &image_infos.back();
                writes.push_back(w);
                break;
            }
            case binding_kind::sampler:
                // Folded into the texture's combined image sampler;
                // see the comment at the top of the file.
                break;
            }
        }
        if (!writes.empty())
        {
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        bind_group h{};
        h.id = m_bind_groups.insert(record);
        return h;
    }

    void vk_device::destroy(bind_group handle)
    {
        auto* record = m_bind_groups.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        VkDevice dev = m_device;
        VkDescriptorPool pool = m_descriptor_pool;
        VkDescriptorSet set = record->descriptor_set;
        // Same deferred-destroy rationale as in destroy(buffer): the
        // descriptor set might still be referenced by an in-flight
        // command buffer.
        enqueue_destroy(
            [dev, pool, set]
            {
                if (set != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
                {
                    vkFreeDescriptorSets(dev, pool, 1, &set);
                }
            });
        record->descriptor_set = VK_NULL_HANDLE;
        m_bind_groups.remove(handle.id);
    }
} // namespace rendering_engine::gpu::backend::vulkan
