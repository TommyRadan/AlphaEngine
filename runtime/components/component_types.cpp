// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file component_types.cpp
 * @brief Registers the built-in components, and the material descriptions a
 *        mesh component's material is saved as, with the type registry
 *        (runtime/reflection.hpp) under the names scene files use.
 *
 * - @c "camera": projection (none / perspective / orthographic), its
 *   parameters, priority, the main flag and the camera's local offset under
 *   the node. The aspect ratio follows the drawable and is not saved.
 * - @c "light": kind (none / ambient / directional / point / spot), colour,
 *   intensity, shadow casting, range, attenuation and cone angles. The light
 *   is placed by its node, so it has no position or direction of its own.
 * - @c "mesh": the mesh as an asset reference (runtime/scene_assets.hpp)
 *   and the material as an object — @c "standard" (a full description),
 *   or @c "phong" / @c "basic" (the renderer's built-in instance).
 * - @c "renderable", @c "ui_element" and @c "animator": registered so they
 *   have names, but what they hold (a mesh source or a UI element built in
 *   code, a skeleton and clips from an imported model) has no data form
 *   yet, so a populated one is saved as a placeholder.
 * - @c "rigidbody": body type, mass, friction, restitution, damping,
 *   gravity scale and, for a dynamic body, its current velocities.
 * - @c "collider": shape, fitting, dimensions, offset and the trigger flag.
 *   A convex hull's explicit point list has no field kind yet, so such a
 *   collider is saved as a placeholder.
 * - @c "audio_source": the clip as a VFS path and how it plays;
 *   @c "audio_listener" has no settings.
 */

#include <runtime/components/component_types.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include <core/audio/audio.hpp>
#include <core/audio/audio_clip.hpp>
#include <core/math/math.hpp>
#include <core/os/os.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/lighting/spot_light.hpp>
#include <rendering_engine/materials/basic_material.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>
#include <runtime/components/animator_component.hpp>
#include <runtime/components/audio_listener_component.hpp>
#include <runtime/components/audio_source_component.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/components/collider_component.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/components/rigidbody_component.hpp>
#include <runtime/components/ui_element_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/engine_settings.hpp>
#include <runtime/reflection.hpp>
#include <runtime/scene_assets.hpp>

namespace
{
    namespace math = core::math;
    using runtime::camera_component;
    using runtime::light_component;
    using runtime::mesh_component;
    using runtime::standard_material_description;

    // A saved rotation is restored bit for bit; one further than this from
    // unit length (hand-written) is normalised instead.
    constexpr float k_unit_tolerance = 1e-4f;

    void restore_rotation(core::transform& target, const math::quat& rotation)
    {
        const float length = std::sqrt(rotation.w * rotation.w + rotation.x * rotation.x + rotation.y * rotation.y +
                                       rotation.z * rotation.z);
        if (std::abs(length - 1.0f) <= k_unit_tolerance)
        {
            target.set_quaternion_exact(rotation);
        }
        else
        {
            target.set_quaternion(rotation);
        }
    }

    enum class projection : uint8_t
    {
        none,
        perspective,
        orthographic,
    };

    const rendering_engine::perspective_camera* perspective_of(const camera_component& component)
    {
        return dynamic_cast<const rendering_engine::perspective_camera*>(component.get());
    }

    const rendering_engine::orthographic_camera* orthographic_of(const camera_component& component)
    {
        return dynamic_cast<const rendering_engine::orthographic_camera*>(component.get());
    }

    projection projection_of(const camera_component& component)
    {
        if (perspective_of(component) != nullptr)
        {
            return projection::perspective;
        }
        if (orthographic_of(component) != nullptr)
        {
            return projection::orthographic;
        }
        return projection::none;
    }

    bool is_perspective(const camera_component& component)
    {
        return perspective_of(component) != nullptr;
    }

    bool is_orthographic(const camera_component& component)
    {
        return orthographic_of(component) != nullptr;
    }

    bool has_camera(const camera_component& component)
    {
        return component.get() != nullptr;
    }

    // A camera of @p kind with the engine's configured field of view; the
    // world it attaches to keeps its aspect in step with the drawable.
    std::unique_ptr<rendering_engine::camera> make_camera(projection kind)
    {
        switch (kind)
        {
        case projection::perspective:
        {
            const runtime::engine_settings& settings = *runtime::current_engine().settings;
            const float reported = runtime::current_engine().renderer->world().drawable_aspect();
            return std::make_unique<rendering_engine::perspective_camera>(
                settings.camera.field_of_view, reported > 0.0f ? reported : settings.window.aspect_ratio());
        }
        case projection::orthographic:
            return std::make_unique<rendering_engine::orthographic_camera>();
        case projection::none:
            break;
        }
        return nullptr;
    }

    bool set_projection(camera_component& component, projection kind)
    {
        if (kind != projection::none && kind != projection::perspective && kind != projection::orthographic)
        {
            return false;
        }
        if (kind != projection_of(component) || (kind == projection::none && component.get() != nullptr))
        {
            component = camera_component{make_camera(kind)};
        }
        return true;
    }

    float near_clip_of(const camera_component& component)
    {
        if (const auto* perspective = perspective_of(component))
        {
            return perspective->get_near_clip();
        }
        const auto* orthographic = orthographic_of(component);
        return orthographic != nullptr ? orthographic->get_near_clip() : 0.0f;
    }

    void set_near_clip(camera_component& component, float value)
    {
        if (auto* perspective = dynamic_cast<rendering_engine::perspective_camera*>(component.get()))
        {
            perspective->set_near_clip(value);
        }
        else if (auto* orthographic = dynamic_cast<rendering_engine::orthographic_camera*>(component.get()))
        {
            orthographic->set_near_clip(value);
        }
    }

    float far_clip_of(const camera_component& component)
    {
        if (const auto* perspective = perspective_of(component))
        {
            return perspective->get_far_clip();
        }
        const auto* orthographic = orthographic_of(component);
        return orthographic != nullptr ? orthographic->get_far_clip() : 0.0f;
    }

    void set_far_clip(camera_component& component, float value)
    {
        if (auto* perspective = dynamic_cast<rendering_engine::perspective_camera*>(component.get()))
        {
            perspective->set_far_clip(value);
        }
        else if (auto* orthographic = dynamic_cast<rendering_engine::orthographic_camera*>(component.get()))
        {
            orthographic->set_far_clip(value);
        }
    }

    enum class light_kind : uint8_t
    {
        none,
        ambient,
        directional,
        point,
        spot,
    };

    light_kind kind_of(const light_component& component)
    {
        const rendering_engine::light* light = component.get();
        if (light == nullptr)
        {
            return light_kind::none;
        }
        switch (light->type())
        {
        case rendering_engine::light_type::ambient:
            return light_kind::ambient;
        case rendering_engine::light_type::directional:
            return light_kind::directional;
        case rendering_engine::light_type::point:
            return light_kind::point;
        case rendering_engine::light_type::spot:
            return light_kind::spot;
        }
        return light_kind::none;
    }

    std::unique_ptr<rendering_engine::light> make_light(light_kind kind)
    {
        switch (kind)
        {
        case light_kind::ambient:
            return std::make_unique<rendering_engine::ambient_light>();
        case light_kind::directional:
            return std::make_unique<rendering_engine::directional_light>();
        case light_kind::point:
            return std::make_unique<rendering_engine::point_light>();
        case light_kind::spot:
            return std::make_unique<rendering_engine::spot_light>();
        case light_kind::none:
            break;
        }
        return nullptr;
    }

    bool set_light_kind(light_component& component, light_kind kind)
    {
        if (kind != light_kind::none && kind != light_kind::ambient && kind != light_kind::directional &&
            kind != light_kind::point && kind != light_kind::spot)
        {
            return false;
        }
        if (kind == kind_of(component))
        {
            return true;
        }
        // construct_only (below): this field is only ever set before the
        // component attaches, so the fresh light built here is unattached
        // like the one it replaces, and on_attach registers whichever one
        // is left when the component is actually added to a node.
        std::unique_ptr<rendering_engine::light> fresh = make_light(kind);
        if (fresh != nullptr && component.get() != nullptr)
        {
            fresh->color = component.get()->color;
            fresh->intensity = component.get()->intensity;
        }
        component = light_component{std::move(fresh)};
        return true;
    }

    bool has_light(const light_component& component)
    {
        return component.get() != nullptr;
    }

    bool is_point_or_spot(const light_component& component)
    {
        const light_kind kind = kind_of(component);
        return kind == light_kind::point || kind == light_kind::spot;
    }

    bool casts_shadows(const light_component& component)
    {
        const light_kind kind = kind_of(component);
        return kind == light_kind::directional || kind == light_kind::point || kind == light_kind::spot;
    }

    bool is_spot(const light_component& component)
    {
        return kind_of(component) == light_kind::spot;
    }

    template<typename Light>
    Light* light_as(const light_component& component)
    {
        return dynamic_cast<Light*>(component.get());
    }

    bool cast_shadow_of(const light_component& component)
    {
        if (const auto* directional = light_as<rendering_engine::directional_light>(component))
        {
            return directional->cast_shadow;
        }
        if (const auto* point = light_as<rendering_engine::point_light>(component))
        {
            return point->cast_shadow;
        }
        const auto* spot = light_as<rendering_engine::spot_light>(component);
        return spot != nullptr && spot->cast_shadow;
    }

    void set_cast_shadow(light_component& component, bool cast)
    {
        if (auto* directional = light_as<rendering_engine::directional_light>(component))
        {
            directional->cast_shadow = cast;
        }
        else if (auto* point = light_as<rendering_engine::point_light>(component))
        {
            point->cast_shadow = cast;
        }
        else if (auto* spot = light_as<rendering_engine::spot_light>(component))
        {
            spot->cast_shadow = cast;
        }
    }

    // A float both point and spot lights declare (neither inherits it from
    // the other).
    void add_attenuation_field(runtime::type_builder<light_component>& type,
                               std::string name,
                               float rendering_engine::point_light::*point_member,
                               float rendering_engine::spot_light::*spot_member)
    {
        type.field(
                std::move(name),
                [point_member, spot_member](const light_component& component)
                {
                    if (const auto* point = light_as<rendering_engine::point_light>(component))
                    {
                        return point->*point_member;
                    }
                    const auto* spot = light_as<rendering_engine::spot_light>(component);
                    return spot != nullptr ? spot->*spot_member : 0.0f;
                },
                [point_member, spot_member](light_component& component, float value)
                {
                    if (auto* point = light_as<rendering_engine::point_light>(component))
                    {
                        point->*point_member = value;
                    }
                    else if (auto* spot = light_as<rendering_engine::spot_light>(component))
                    {
                        spot->*spot_member = value;
                    }
                })
            .when(is_point_or_spot);
    }

    // The built-in, renderer-owned material the "phong" and "basic" object
    // types stand for: nothing to describe.
    struct builtin_material
    {
    };

    const runtime::type_info* material_type(const std::string& name)
    {
        const runtime::type_info* type = runtime::default_type_registry().find(name);
        return type != nullptr && type->category == runtime::type_category::object ? type : nullptr;
    }

    // A handle that points at @p material without owning it.
    std::shared_ptr<rendering_engine::material> borrowed(rendering_engine::material* material)
    {
        return std::shared_ptr<rendering_engine::material>{std::shared_ptr<rendering_engine::material>{}, material};
    }

    rendering_engine::material* builtin_material_for(const std::string& template_name)
    {
        rendering_engine::renderer& renderer = *runtime::current_engine().renderer;
        if (template_name == "phong")
        {
            return &renderer.get_phong_material();
        }
        if (template_name == "basic")
        {
            return &renderer.get_basic_material();
        }
        return nullptr;
    }

    runtime::object_value describe_material(const rendering_engine::material* material)
    {
        if (material == nullptr)
        {
            return {};
        }
        const std::string& template_name = material->get_template().name();
        if (const standard_material_description* description = runtime::find_material_description(*material))
        {
            return material_type("standard")->snapshot(description);
        }
        if (template_name == "standard")
        {
            // Only the shared params and the keywords' vertex-layout flags
            // can be read back from an instance built in code; the rest stays
            // at the standard defaults.
            using rendering_engine::keyword_bit;
            using rendering_engine::material_keyword;
            standard_material_description fallback;
            fallback.params = material->params();
            fallback.tangents = (material->keywords() & keyword_bit(material_keyword::has_tangents)) != 0;
            fallback.skinned = (material->keywords() & keyword_bit(material_keyword::skinned)) != 0;
            return material_type("standard")->snapshot(&fallback);
        }
        if (const runtime::type_info* type = material_type(template_name))
        {
            const std::shared_ptr<void> defaults = type->create();
            return type->snapshot(defaults.get());
        }
        runtime::object_value unknown;
        unknown.type = template_name;
        return unknown;
    }

    std::shared_ptr<rendering_engine::material> build_material(const runtime::object_value& value)
    {
        if (value.type == "standard")
        {
            standard_material_description description;
            if (!material_type("standard")->apply(&description, value))
            {
                return nullptr;
            }
            return runtime::acquire_standard_material(description);
        }
        return borrowed(builtin_material_for(value.type));
    }

    // The material @p component draws with, as a handle to rebuild it with.
    std::shared_ptr<rendering_engine::material> material_handle(const mesh_component& component)
    {
        return component.owned_material() != nullptr ? component.owned_material() : borrowed(component.material());
    }

    bool set_mesh_reference(mesh_component& component, const std::string& reference)
    {
        std::shared_ptr<rendering_engine::mesh_asset> mesh;
        if (!reference.empty())
        {
            mesh = runtime::resolve_mesh_reference(reference);
            if (mesh == nullptr)
            {
                return false;
            }
        }
        component = mesh_component{material_handle(component), std::move(mesh)};
        return true;
    }

    bool set_material(mesh_component& component, const runtime::object_value& value)
    {
        std::shared_ptr<rendering_engine::material> material;
        if (!value.empty())
        {
            material = build_material(value);
            if (material == nullptr)
            {
                return false;
            }
        }
        component = mesh_component{std::move(material), component.mesh()};
        return true;
    }

    std::string mesh_placeholder_reason(const mesh_component& component)
    {
        if (component.has_private_mesh())
        {
            return "its mesh is a private upload with no asset to reference";
        }
        if (component.mesh() != nullptr && runtime::mesh_reference(*component.mesh()).empty())
        {
            return "its mesh was not made by the asset cache, so it has no key to reference";
        }
        const rendering_engine::material* material = component.material();
        if (material != nullptr && runtime::find_material_description(*material) == nullptr &&
            material_type(material->get_template().name()) == nullptr)
        {
            return "its " + material->get_template().name() + " material has no registered description";
        }
        return {};
    }

    std::string mesh_lossy_reason(const mesh_component& component)
    {
        const rendering_engine::material* material = component.material();
        if (material == nullptr || runtime::find_material_description(*material) != nullptr)
        {
            return {};
        }
        const std::string& template_name = material->get_template().name();
        if (template_name == "standard")
        {
            return "its material was not built from a description: only its params and vertex-layout flags are "
                   "saved, and its colours, scalars and maps load as the standard defaults";
        }
        if (material != builtin_material_for(template_name))
        {
            return "its " + template_name + " material is saved by template name and loads as the built-in one";
        }
        return {};
    }

    void register_material_types(runtime::type_registry& registry)
    {
        using description = standard_material_description;
        using rendering_engine::blend_mode;
        registry.register_object<description>("standard")
            .field(
                "transparent",
                [](const description& d) { return d.params.transparent; },
                [](description& d, bool value) { d.params.transparent = value; })
            .field(
                "opacity",
                [](const description& d) { return d.params.opacity; },
                [](description& d, float value) { d.params.opacity = value; })
            .field(
                "double_sided",
                [](const description& d) { return d.params.double_sided; },
                [](description& d, bool value) { d.params.double_sided = value; })
            .field(
                "blending",
                [](const description& d) { return d.params.blending; },
                [](description& d, blend_mode value)
                {
                    if (value > blend_mode::multiply)
                    {
                        return false;
                    }
                    d.params.blending = value;
                    return true;
                })
            .enumerators({{"none", static_cast<int64_t>(blend_mode::none)},
                          {"normal", static_cast<int64_t>(blend_mode::normal)},
                          {"additive", static_cast<int64_t>(blend_mode::additive)},
                          {"subtractive", static_cast<int64_t>(blend_mode::subtractive)},
                          {"multiply", static_cast<int64_t>(blend_mode::multiply)}})
            .field(
                "wireframe",
                [](const description& d) { return d.params.wireframe; },
                [](description& d, bool value) { d.params.wireframe = value; })
            .field(
                "depth_test",
                [](const description& d) { return d.params.depth_test; },
                [](description& d, bool value) { d.params.depth_test = value; })
            .field(
                "depth_write",
                [](const description& d) { return d.params.depth_write; },
                [](description& d, bool value) { d.params.depth_write = value; })
            .field(
                "fog",
                [](const description& d) { return d.params.fog; },
                [](description& d, bool value) { d.params.fog = value; })
            .field("base_color", &description::base_color)
            .color()
            .field("metalness", &description::metalness)
            .field("roughness", &description::roughness)
            .field("emissive", &description::emissive)
            .color()
            .field("emissive_intensity", &description::emissive_intensity)
            .field("occlusion_strength", &description::occlusion_strength)
            .field("ibl_intensity", &description::ibl_intensity)
            .field("tangents", &description::tangents)
            .field("skinned", &description::skinned)
            .field("albedo_map", &description::albedo_map)
            .asset("texture")
            .field("normal_map", &description::normal_map)
            .asset("texture")
            .field("metalness_map", &description::metalness_map)
            .asset("texture")
            .field("roughness_map", &description::roughness_map)
            .asset("texture")
            .field("occlusion_map", &description::occlusion_map)
            .asset("texture")
            .field("orm_map", &description::orm_map)
            .asset("texture")
            .field("emissive_map", &description::emissive_map)
            .asset("texture");

        registry.register_object<builtin_material>("phong");
        registry.register_object<builtin_material>("basic");
    }

    void register_camera(runtime::type_registry& registry)
    {
        registry
            .register_component<camera_component>("camera",
                                                  [] { return camera_component{make_camera(projection::perspective)}; })
            .field("projection", projection_of, set_projection)
            .enumerators({{"none", static_cast<int64_t>(projection::none)},
                          {"perspective", static_cast<int64_t>(projection::perspective)},
                          {"orthographic", static_cast<int64_t>(projection::orthographic)}})
            .construct_only()
            .field(
                "field_of_view",
                [](const camera_component& c) { return perspective_of(c)->get_field_of_view(); },
                [](camera_component& c, float value)
                { dynamic_cast<rendering_engine::perspective_camera&>(*c.get()).set_field_of_view(value); })
            .when(is_perspective)
            .field(
                "x_magnification",
                [](const camera_component& c) { return orthographic_of(c)->get_x_magnification(); },
                [](camera_component& c, float value)
                { dynamic_cast<rendering_engine::orthographic_camera&>(*c.get()).set_x_magnification(value); })
            .when(is_orthographic)
            .field(
                "y_magnification",
                [](const camera_component& c) { return orthographic_of(c)->get_y_magnification(); },
                [](camera_component& c, float value)
                { dynamic_cast<rendering_engine::orthographic_camera&>(*c.get()).set_y_magnification(value); })
            .when(is_orthographic)
            .field("near_clip", near_clip_of, set_near_clip)
            .when(has_camera)
            .field("far_clip", far_clip_of, set_far_clip)
            .when(has_camera)
            .field(
                "priority",
                [](const camera_component& c) { return c.get()->get_priority(); },
                [](camera_component& c, int value) { c.get()->set_priority(value); })
            .when(has_camera)
            .field(
                "main",
                [](const camera_component& c) { return c.get()->is_main(); },
                [](camera_component& c, bool value) { c.get()->set_main(value); })
            .when(has_camera)
            .field(
                "position",
                [](const camera_component& c) { return c.offset().get_position(); },
                [](camera_component& c, const math::vec3& value) { c.offset().set_position(value); })
            .when(has_camera)
            .field(
                "rotation",
                [](const camera_component& c) { return c.offset().get_quaternion(); },
                [](camera_component& c, const math::quat& value) { restore_rotation(c.offset(), value); })
            .when(has_camera)
            .field(
                "scale",
                [](const camera_component& c) { return c.offset().get_scale(); },
                [](camera_component& c, const math::vec3& value) { c.offset().set_scale(value); })
            .when(has_camera)
            .placeholder_reason(
                [](const camera_component& c) -> std::string
                {
                    return c.get() != nullptr && projection_of(c) == projection::none
                               ? "its camera is of a type other than perspective or orthographic"
                               : "";
                });
    }

    void register_light(runtime::type_registry& registry)
    {
        auto light = registry.register_component<light_component>("light");
        light.field("kind", kind_of, set_light_kind)
            .enumerators({{"none", static_cast<int64_t>(light_kind::none)},
                          {"ambient", static_cast<int64_t>(light_kind::ambient)},
                          {"directional", static_cast<int64_t>(light_kind::directional)},
                          {"point", static_cast<int64_t>(light_kind::point)},
                          {"spot", static_cast<int64_t>(light_kind::spot)}})
            .construct_only()
            .field(
                "color",
                [](const light_component& c) { return c.get()->color; },
                [](light_component& c, const math::vec3& value) { c.get()->color = value; })
            .color()
            .when(has_light)
            .field(
                "intensity",
                [](const light_component& c) { return c.get()->intensity; },
                [](light_component& c, float value) { c.get()->intensity = value; })
            .when(has_light)
            .field("cast_shadow", cast_shadow_of, set_cast_shadow)
            .when(casts_shadows);
        add_attenuation_field(
            light, "range", &rendering_engine::point_light::range, &rendering_engine::spot_light::range);
        add_attenuation_field(light,
                              "constant_attenuation",
                              &rendering_engine::point_light::constant_attenuation,
                              &rendering_engine::spot_light::constant_attenuation);
        add_attenuation_field(light,
                              "linear_attenuation",
                              &rendering_engine::point_light::linear_attenuation,
                              &rendering_engine::spot_light::linear_attenuation);
        add_attenuation_field(light,
                              "quadratic_attenuation",
                              &rendering_engine::point_light::quadratic_attenuation,
                              &rendering_engine::spot_light::quadratic_attenuation);
        light
            .field(
                "inner_angle",
                [](const light_component& c) { return light_as<rendering_engine::spot_light>(c)->inner_angle; },
                [](light_component& c, float value) { light_as<rendering_engine::spot_light>(c)->inner_angle = value; })
            .when(is_spot)
            .field(
                "outer_angle",
                [](const light_component& c) { return light_as<rendering_engine::spot_light>(c)->outer_angle; },
                [](light_component& c, float value) { light_as<rendering_engine::spot_light>(c)->outer_angle = value; })
            .when(is_spot);
    }

    void register_mesh(runtime::type_registry& registry)
    {
        registry.register_component<mesh_component>("mesh")
            .field(
                "mesh",
                [](const mesh_component& c)
                { return c.mesh() != nullptr ? runtime::mesh_reference(*c.mesh()) : std::string{}; },
                set_mesh_reference)
            .asset("mesh")
            .construct_only()
            .field(
                "material", [](const mesh_component& c) { return describe_material(c.material()); }, set_material)
            .construct_only()
            .placeholder_reason(mesh_placeholder_reason)
            .lossy_reason(mesh_lossy_reason);
    }

    using runtime::collider_component;
    using runtime::rigidbody_component;
    namespace physics = runtime::physics;

    bool is_dynamic(const rigidbody_component& body)
    {
        return body.type() == physics::body_type::dynamic_body;
    }

    bool set_body_type(rigidbody_component& body, physics::body_type type)
    {
        if (type != physics::body_type::static_body && type != physics::body_type::kinematic_body &&
            type != physics::body_type::dynamic_body)
        {
            return false;
        }
        body.set_type(type);
        return true;
    }

    void register_rigidbody(runtime::type_registry& registry)
    {
        registry.register_component<rigidbody_component>("rigidbody")
            .field("type", &rigidbody_component::type, set_body_type)
            .enumerators({{"static", static_cast<int64_t>(physics::body_type::static_body)},
                          {"kinematic", static_cast<int64_t>(physics::body_type::kinematic_body)},
                          {"dynamic", static_cast<int64_t>(physics::body_type::dynamic_body)}})
            .field("mass", &rigidbody_component::mass, &rigidbody_component::set_mass)
            .field("friction", &rigidbody_component::friction, &rigidbody_component::set_friction)
            .field("restitution", &rigidbody_component::restitution, &rigidbody_component::set_restitution)
            .field("linear_damping", &rigidbody_component::linear_damping, &rigidbody_component::set_linear_damping)
            .field("angular_damping", &rigidbody_component::angular_damping, &rigidbody_component::set_angular_damping)
            .field("gravity_scale", &rigidbody_component::gravity_scale, &rigidbody_component::set_gravity_scale)
            .field("linear_velocity", &rigidbody_component::linear_velocity, &rigidbody_component::set_linear_velocity)
            .when(is_dynamic)
            .field(
                "angular_velocity", &rigidbody_component::angular_velocity, &rigidbody_component::set_angular_velocity)
            .when(is_dynamic);
    }

    // A member of the collider's settings, read and written through them.
    template<typename V>
    runtime::type_builder<collider_component>& add_collider_field(runtime::type_builder<collider_component>& type,
                                                                  std::string name,
                                                                  V physics::collider_settings::*member)
    {
        return type.field(
            std::move(name),
            [member](const collider_component& collider) { return collider.settings().*member; },
            [member](collider_component& collider, const V& value)
            {
                physics::collider_settings settings = collider.settings();
                settings.*member = value;
                collider.set_settings(settings);
            });
    }

    bool set_collider_shape(collider_component& collider, physics::collider_shape shape)
    {
        if (shape != physics::collider_shape::box && shape != physics::collider_shape::sphere &&
            shape != physics::collider_shape::capsule && shape != physics::collider_shape::convex_hull)
        {
            return false;
        }
        physics::collider_settings settings = collider.settings();
        settings.shape = shape;
        collider.set_settings(settings);
        return true;
    }

    void register_collider(runtime::type_registry& registry)
    {
        using physics::collider_settings;
        using physics::collider_shape;
        auto collider = registry.register_component<collider_component>("collider");
        collider.field("shape", &collider_component::shape, set_collider_shape)
            .enumerators({{"box", static_cast<int64_t>(collider_shape::box)},
                          {"sphere", static_cast<int64_t>(collider_shape::sphere)},
                          {"capsule", static_cast<int64_t>(collider_shape::capsule)},
                          {"convex_hull", static_cast<int64_t>(collider_shape::convex_hull)}});
        add_collider_field(collider, "fit_to_mesh", &collider_settings::fit_to_mesh);
        add_collider_field(collider, "half_extents", &collider_settings::half_extents)
            .when([](const collider_component& c) { return c.shape() == collider_shape::box; });
        add_collider_field(collider, "radius", &collider_settings::radius)
            .when([](const collider_component& c)
                  { return c.shape() == collider_shape::sphere || c.shape() == collider_shape::capsule; });
        add_collider_field(collider, "half_height", &collider_settings::half_height)
            .when([](const collider_component& c) { return c.shape() == collider_shape::capsule; });
        add_collider_field(collider, "center", &collider_settings::center)
            .when([](const collider_component& c) { return c.shape() != collider_shape::convex_hull; });
        add_collider_field(collider, "trigger", &collider_settings::is_trigger);
        collider.placeholder_reason(
            [](const collider_component& c) -> std::string
            {
                return c.shape() == collider_shape::convex_hull && !c.settings().points.empty()
                           ? "a convex hull's point list has no field kind yet"
                           : "";
            });
    }

    using runtime::audio_source_component;
    using source_config = audio_source_component::config;

    // Rebuilds @p source with @p edit applied to its settings: for the
    // settings with no live setter, which only an unattached source takes.
    template<typename Edit>
    void rebuild_source(audio_source_component& source, Edit edit)
    {
        source_config settings = source.configuration();
        edit(settings);
        source = audio_source_component{std::move(settings)};
    }

    std::string clip_reference(const audio_source_component& source)
    {
        const std::shared_ptr<core::audio_clip>& clip = source.clip();
        return clip != nullptr && !clip->key.empty() ? runtime::file_reference(clip->key) : std::string{};
    }

    bool set_clip_reference(audio_source_component& source, const std::string& reference)
    {
        if (reference.empty())
        {
            source.set_clip(nullptr);
            return true;
        }
        std::shared_ptr<core::audio_clip> clip =
            runtime::current_engine().audio->load_clip(core::os::utf8_path(reference));
        if (clip == nullptr)
        {
            return false;
        }
        source.set_clip(std::move(clip));
        return true;
    }

    bool is_spatial(const audio_source_component& source)
    {
        return source.is_spatial();
    }

    void register_audio(runtime::type_registry& registry)
    {
        registry.register_component<audio_source_component>("audio_source")
            .field("clip", clip_reference, set_clip_reference)
            .asset("audio")
            .field("gain", &audio_source_component::gain, &audio_source_component::set_gain)
            .field("pitch", &audio_source_component::pitch, &audio_source_component::set_pitch)
            .field("loop", &audio_source_component::loop, &audio_source_component::set_loop)
            .field(
                "play_on_attach",
                [](const audio_source_component& source) { return source.configuration().play_on_attach; },
                [](audio_source_component& source, bool value)
                { rebuild_source(source, [value](source_config& settings) { settings.play_on_attach = value; }); })
            .construct_only()
            .field("spatial",
                   is_spatial,
                   [](audio_source_component& source, bool value)
                   { rebuild_source(source, [value](source_config& settings) { settings.spatial = value; }); })
            .construct_only()
            .field("pan", &audio_source_component::pan, &audio_source_component::set_pan)
            .when([](const audio_source_component& source) { return !source.is_spatial(); })
            .field(
                "min_distance",
                [](const audio_source_component& source) { return source.configuration().min_distance; },
                [](audio_source_component& source, float value)
                {
                    const source_config& settings = source.configuration();
                    source.set_attenuation(value, settings.max_distance, settings.rolloff);
                })
            .when(is_spatial)
            .field(
                "max_distance",
                [](const audio_source_component& source) { return source.configuration().max_distance; },
                [](audio_source_component& source, float value)
                {
                    const source_config& settings = source.configuration();
                    source.set_attenuation(settings.min_distance, value, settings.rolloff);
                })
            .when(is_spatial)
            .field(
                "rolloff",
                [](const audio_source_component& source) { return source.configuration().rolloff; },
                [](audio_source_component& source, float value)
                {
                    const source_config& settings = source.configuration();
                    source.set_attenuation(settings.min_distance, settings.max_distance, value);
                })
            .when(is_spatial)
            .placeholder_reason(
                [](const audio_source_component& source) -> std::string
                {
                    return source.clip() != nullptr && source.clip()->key.empty()
                               ? "its clip was not loaded from a file, so it has no path to reference"
                               : "";
                });

        registry.register_component<runtime::audio_listener_component>("audio_listener");
    }
} // namespace

void runtime::register_component_types(type_registry& registry)
{
    register_material_types(registry);
    register_camera(registry);
    register_light(registry);
    register_mesh(registry);
    register_rigidbody(registry);
    register_collider(registry);
    register_audio(registry);

    registry.register_component<runtime::renderable_component>("renderable")
        .placeholder_reason([](const runtime::renderable_component& c) -> std::string
                            { return c.get() != nullptr ? "a renderable built in code has no data form yet" : ""; });

    registry.register_component<runtime::ui_element_component>("ui_element")
        .placeholder_reason([](const runtime::ui_element_component& c) -> std::string
                            { return c.get() != nullptr ? "a UI element built in code has no data form yet" : ""; });

    registry.register_component<runtime::animator_component>("animator")
        .placeholder_reason(
            [](const runtime::animator_component& c) -> std::string
            {
                return c.skeleton() != nullptr || !c.clips().empty()
                           ? "its skeleton, clips and bindings come from an imported model and have no data form yet"
                           : "";
            });
}
