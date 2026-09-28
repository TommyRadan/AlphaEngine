// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device_bind_group.cpp
 * @brief @c vk_device member functions that build descriptor-set
 *        layouts and descriptor sets.
 *
 * The shader sources arrive as Vulkan-style GLSL with explicit
 * @c layout(set, binding) annotations and are cross-compiled to
 * SPIR-V upstream. The bind-group kinds map directly:
 *
 *   uniform_buffer  -> VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
 *                      (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC when
 *                       the layout entry sets has_dynamic_offset)
 *   storage_buffer  -> VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
 *   storage_texture -> VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
 *   texture         -> VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
 *                      (sampler taken from the standalone sampler
 *                       entry at the same binding when the bind group
 *                       has one, else from the texture's built-in
 *                       VkSampler, so a texture carries its own
 *                       sampler state and a sampler entry on the same
 *                       binding overrides it)
 *   sampler         -> no descriptor binding of its own: folded into
 *                      the combined image sampler of the texture at
 *                      the same binding number.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        VkDescriptorType to_descriptor_type(binding_kind kind, bool dynamic_offset)
        {
            switch (kind)
            {
            case binding_kind::uniform_buffer:
                return dynamic_offset ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
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
            b.descriptorType = to_descriptor_type(entry.kind, entry.has_dynamic_offset);
            b.descriptorCount = 1;
            b.stageFlags = to_vk_stage_flags(entry.stages);
            bindings.push_back(b);
        }

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        if (!vk_check(vkCreateDescriptorSetLayout(m_device.handle(), &info, nullptr, &record.object),
                      "vkCreateDescriptorSetLayout"))
        {
            throw std::runtime_error{"vkCreateDescriptorSetLayout failed"};
        }

        bind_group_layout h{};
        h.id = m_bind_group_layouts.insert(record);
        return h;
    }

    void vk_device::destroy(bind_group_layout handle)
    {
        auto* record = m_bind_group_layouts.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        // Deferred with the rest so a layout released mid-frame (a
        // material rebuild) stays valid for the pipeline-layout build
        // or descriptor-set allocation the same frame still names it
        // in; the handle-pool slot is freed now.
        const VkDevice dev = m_device.handle();
        const VkDescriptorSetLayout object = record->object;
        if (object != VK_NULL_HANDLE)
        {
            enqueue_destroy([dev, object] { vkDestroyDescriptorSetLayout(dev, object, nullptr); });
        }
        record->object = VK_NULL_HANDLE;
        m_bind_group_layouts.remove(handle.id);
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

        // Whether the layout declares @p binding as a dynamic uniform
        // buffer: its descriptor is then written with the _DYNAMIC type
        // the set layout baked, and every bind supplies its offset.
        const auto dynamic_binding = [&](uint32_t binding)
        {
            for (const auto& layout_entry : layout_record->descriptor.entries)
            {
                if (layout_entry.binding == binding)
                {
                    return layout_entry.kind == binding_kind::uniform_buffer && layout_entry.has_dynamic_offset;
                }
            }
            return false;
        };
        for (const auto& layout_entry : layout_record->descriptor.entries)
        {
            if (layout_entry.kind == binding_kind::uniform_buffer && layout_entry.has_dynamic_offset)
            {
                ++record.dynamic_count;
            }
        }

        // One descriptor set per frame slot when the group binds a
        // multi-buffered buffer (a dynamic_data buffer on a device with
        // several frames in flight), so each slot's set points at that
        // slot's copy; otherwise one set serves every slot. See
        // vk_bind_group::descriptor_sets.
        record.set_count = 1;
        for (const auto& entry : descriptor.entries)
        {
            if (entry.kind != binding_kind::uniform_buffer && entry.kind != binding_kind::storage_buffer)
            {
                continue;
            }
            const auto* buf = m_buffers.lookup(entry.buffer_value.id);
            if (buf != nullptr && buf->region_count > 1)
            {
                record.set_count = buf->region_count;
                break;
            }
        }

        // From the pool chain: an exhausted pool grows the chain and
        // the allocation is retried, so the per-draw / per-material
        // churn of a larger scene never silently produces an invalid
        // group. Each set's pool is recorded so destroy() frees it to
        // the pool it came from.
        for (uint32_t set_index = 0; set_index < record.set_count; ++set_index)
        {
            if (!m_descriptors.allocate_descriptor_set(
                    layout_record->object, record.descriptor_sets[set_index], record.pools[set_index]))
            {
                LOG_ERR("vk_device::create_bind_group: no descriptor set could be allocated; the bind group is "
                        "invalid");
                for (uint32_t freed = 0; freed < set_index; ++freed)
                {
                    vk_check(
                        vkFreeDescriptorSets(m_device.handle(), record.pools[freed], 1, &record.descriptor_sets[freed]),
                        "vkFreeDescriptorSets");
                }
                return {};
            }
        }

        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        std::vector<VkWriteDescriptorSet> writes;
        const size_t write_capacity = descriptor.entries.size() * record.set_count;
        buffer_infos.reserve(write_capacity);
        image_infos.reserve(write_capacity);
        writes.reserve(write_capacity);

        // A standalone sampler entry pairs with the texture entry at
        // the same binding number: the texture's combined image
        // sampler then carries that sampler (its full descriptor —
        // comparison mode, anisotropy, LOD, border) instead of the
        // state baked onto the texture.
        const auto standalone_sampler = [&](uint32_t binding) -> VkSampler
        {
            for (const auto& candidate : descriptor.entries)
            {
                if (candidate.kind != binding_kind::sampler || candidate.binding != binding)
                {
                    continue;
                }
                if (const auto* record_sampler = m_samplers.lookup(candidate.sampler_value.id))
                {
                    return record_sampler->object;
                }
            }
            return VK_NULL_HANDLE;
        };

        // Every set gets the same descriptors, except that a buffer
        // with several regions is bound at set s through region s. The
        // per-entry problems are reported once, from the first set.
        for (uint32_t set_index = 0; set_index < record.set_count; ++set_index)
        {
            const bool report = set_index == 0;
            for (const auto& entry : descriptor.entries)
            {
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = record.descriptor_sets[set_index];
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
                    const bool dynamic = entry.kind == binding_kind::uniform_buffer && dynamic_binding(entry.binding);
                    if (report && dynamic && entry.size == 0)
                    {
                        // VK_WHOLE_SIZE resolves against the base offset, so
                        // any non-zero dynamic offset would run past the end.
                        LOG_WRN("vk_device::create_bind_group: dynamic uniform buffer at binding %u has no size; "
                                "only a zero dynamic offset stays inside the buffer",
                                entry.binding);
                    }
                    VkDescriptorBufferInfo bi{};
                    bi.buffer = buf->object;
                    bi.offset = entry.offset;
                    bi.range = entry.size != 0 ? entry.size : VK_WHOLE_SIZE;
                    if (buf->region_count > 1)
                    {
                        // Set s reads region s. VK_WHOLE_SIZE would run to
                        // the end of the last region, so "the rest of the
                        // buffer" is the rest of this one copy.
                        bi.offset += set_index * buf->region_stride;
                        if (entry.size == 0)
                        {
                            const auto copy_size = static_cast<VkDeviceSize>(buf->size);
                            bi.range = copy_size - std::min<VkDeviceSize>(entry.offset, copy_size);
                        }
                    }
                    buffer_infos.push_back(bi);
                    w.descriptorType = to_descriptor_type(entry.kind, dynamic);
                    w.pBufferInfo = &buffer_infos.back();
                    writes.push_back(w);
                    break;
                }
                case binding_kind::texture:
                {
                    auto* tex = m_textures.lookup(entry.texture_value.id);
                    if (tex == nullptr || tex->view == VK_NULL_HANDLE)
                    {
                        // Unset/invalid sampler slot. Vulkan requires every
                        // statically-used descriptor to reference a valid
                        // resource, so substitute the 1x1 placeholder of the
                        // dimension this binding declares (a material may
                        // leave maps unbound — e.g. no albedo, or no IBL
                        // cube when no environment is attached).
                        texture_dimension dim = texture_dimension::d2;
                        for (const auto& layout_entry : layout_record->descriptor.entries)
                        {
                            if (layout_entry.binding == entry.binding)
                            {
                                dim = layout_entry.dimension;
                                break;
                            }
                        }
                        tex = m_textures.lookup(default_texture(dim).id);
                        if (tex == nullptr || tex->view == VK_NULL_HANDLE)
                        {
                            continue;
                        }
                    }
                    VkSampler sampler = standalone_sampler(entry.binding);
                    if (sampler == VK_NULL_HANDLE)
                    {
                        sampler = tex->default_sampler;
                    }
                    if (sampler == VK_NULL_HANDLE)
                    {
                        // The texture's own sampler failed to create (logged
                        // then). A combined-image-sampler descriptor must
                        // name a valid sampler, so the device's fallback
                        // stands in rather than a null handle reaching
                        // vkUpdateDescriptorSets.
                        if (report)
                        {
                            LOG_ERR("vk_device::create_bind_group: texture %llu bound at %u has no sampler; "
                                    "using the device fallback sampler",
                                    static_cast<unsigned long long>(entry.texture_value.id),
                                    entry.binding);
                        }
                        sampler = m_fallback_sampler;
                    }
                    VkDescriptorImageInfo ii{};
                    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    ii.imageView = tex->view;
                    ii.sampler = sampler;
                    image_infos.push_back(ii);
                    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    w.pImageInfo = &image_infos.back();
                    writes.push_back(w);
                    break;
                }
                case binding_kind::storage_texture:
                {
                    auto* tex = m_textures.lookup(entry.texture_value.id);
                    if (tex == nullptr || tex->image == VK_NULL_HANDLE)
                    {
                        continue;
                    }
                    // A storage descriptor must name exactly one mip level,
                    // so it binds the lazily-built single-level view rather
                    // than the whole-chain sampling view.
                    const VkImageView storage_view = storage_image_view(*tex, entry.storage_level);
                    if (storage_view == VK_NULL_HANDLE)
                    {
                        continue;
                    }
                    VkDescriptorImageInfo ii{};
                    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    ii.imageView = storage_view;
                    image_infos.push_back(ii);
                    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    w.pImageInfo = &image_infos.back();
                    writes.push_back(w);
                    break;
                }
                case binding_kind::sampler:
                    // Folded into the combined image sampler of the texture
                    // bound at the same binding (standalone_sampler above);
                    // see the comment at the top of the file.
                    break;
                }
            }
        }
        if (!writes.empty())
        {
            vkUpdateDescriptorSets(m_device.handle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
        VkDevice dev = m_device.handle();
        // Each set goes back to the pool it was allocated from, which
        // need not be the chain's current pool.
        const std::array<VkDescriptorPool, k_max_frames_in_flight> pools = record->pools;
        const std::array<VkDescriptorSet, k_max_frames_in_flight> sets = record->descriptor_sets;
        const uint32_t set_count = record->set_count;
        // Same deferred-destroy rationale as in destroy(buffer): the
        // descriptor sets might still be referenced by an in-flight
        // command buffer.
        enqueue_destroy(
            [dev, pools, sets, set_count]
            {
                for (uint32_t set_index = 0; set_index < set_count; ++set_index)
                {
                    if (sets[set_index] != VK_NULL_HANDLE && pools[set_index] != VK_NULL_HANDLE)
                    {
                        vk_check(vkFreeDescriptorSets(dev, pools[set_index], 1, &sets[set_index]),
                                 "vkFreeDescriptorSets");
                    }
                }
            });
        record->descriptor_sets.fill(VK_NULL_HANDLE);
        record->pools.fill(VK_NULL_HANDLE);
        record->set_count = 0;
        m_bind_groups.remove(handle.id);
    }
} // namespace rendering_engine::gpu::backend::vulkan
