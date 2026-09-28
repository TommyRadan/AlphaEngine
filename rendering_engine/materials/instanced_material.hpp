// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <memory>

#include <assets/color.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    // Built-in unlit material for @ref instanced_mesh. Position vertex
    // stream like @ref basic_material, but the model matrix and per-instance
    // tint come from a second, per-instance vertex stream (bound at slot 1)
    // rather than a per-draw model block — so one draw paints every instance
    // with its own transform and colour.
    //
    // The per-instance stream is a tightly packed record of a @c mat4 model
    // (four @c vec4 columns at locations 1..4) followed by a @c vec4 colour
    // (location 5); see @ref instance_buffer_stride. It is fed through a
    // per-instance vertex binding (@c VK_VERTEX_INPUT_RATE_INSTANCE), so
    // the shader reads ordinary attributes rather than indexing by
    // @c gl_InstanceIndex. An instanced mesh proxy's @ref mesh_instance
    // records are this layout; the material is meant to be fronted by
    // @ref instanced_mesh.
    //
    // Slot layout: per-frame group at slot 0 (camera, owned by the
    // @ref scene_pass) and the per-material group at slot 2 (a flat tint
    // multiplied onto every instance, owned by each instance). The per-draw
    // group (slot 1) is unused.
    struct instanced_material : public material
    {
        // @p tmpl is the shared instanced template (see @ref describe).
        explicit instanced_material(std::shared_ptr<material_template> tmpl);
        ~instanced_material() override;

        // The descriptor of the template every instanced_material shares: the
        // material library builds it (material_library::create_template)
        // over the scene pass's per-frame set and registers it as
        // "instanced"; material_library::create_material makes a instanced_material
        // of it. Touches no gpu::device.
        static material_template_descriptor describe();

        // Byte stride of one per-instance record: a mat4 model (64 bytes)
        // followed by a vec4 colour (16 bytes): one @ref mesh_instance.
        static constexpr uint32_t instance_buffer_stride = 16u * sizeof(float) + 4u * sizeof(float);

        // Flat tint multiplied onto every instance's own colour (white
        // leaves the per-instance colours unchanged).
        void set_color(const assets::color& color);

    private:
        // Push the tint into the per-material UBO.
        void upload_params();

        assets::color m_color{255, 255, 255, 255};
        gpu::buffer m_material_ubo{};
    };
} // namespace rendering_engine
