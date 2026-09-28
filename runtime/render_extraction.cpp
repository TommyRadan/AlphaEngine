// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/render_extraction.hpp>

#include <cstddef>

#include <runtime/components/camera_component.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/components/ui_element_component.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>
#include <runtime/scene_manager.hpp>

void runtime::extract_render_proxies(scene_manager& scenes)
{
    for (std::size_t index = 0; index < scenes.scene_count(); ++index)
    {
        runtime::scene& target = scenes.scene_at(index);
        if (target.world() == nullptr)
        {
            continue;
        }
        target.each<light_component>([](node& owner, light_component& light) { light.extract(owner); });
        target.each<camera_component>([](node& owner, camera_component& camera) { camera.extract(owner); });
        target.each<mesh_component>([](node& owner, mesh_component& mesh) { mesh.extract(owner); });
        target.each<renderable_component>([](node& owner, renderable_component& drawn) { drawn.extract(owner); });
        target.each<ui_element_component>([](node& owner, ui_element_component& ui) { ui.extract(owner); });
    }
}
