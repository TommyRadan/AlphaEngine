// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

// The one translation unit that includes Jolt. The root CMakeLists.txt gives
// this file (and only this file) Jolt's compile definitions, instruction-set
// flags and include path, so every JPH_* define here matches the library.

#define LOG_CATEGORY "physics"

#include <runtime/physics/physics_world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Jolt.h has to precede every other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/editor/box_edges.hpp>
#include <rendering_engine/renderables/model.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/physics/physics_debug_draw.hpp>

namespace runtime::physics
{
    namespace
    {
        namespace math = core::math;

        constexpr JPH::uint k_max_bodies = 16384;
        constexpr JPH::uint k_max_body_pairs = 16384;
        constexpr JPH::uint k_max_contact_constraints = 8192;
        constexpr std::size_t k_temp_allocator_bytes = std::size_t{8} * 1024 * 1024;
        constexpr int k_max_worker_threads = 4;

        // Smallest half extent / radius a shape is built with, so a flat mesh
        // (a plane's bounds have no thickness) still yields a valid box.
        constexpr float k_min_extent = 0.005f;

        // "Game code moved this dynamic body's node" thresholds. Writing a
        // pose through the node's transform and reading it back out of the
        // recomposed world matrix loses a few ulps, which must not count as a
        // teleport. The position tolerance is relative to the distance from
        // the origin (with a floor of one metre), like float precision.
        constexpr float k_position_tolerance = 1.0e-5f;
        constexpr float k_rotation_tolerance = 1.0e-6f;
        // Relative tolerance for "the resolved shape changed" (world scale is
        // re-derived from the node's matrix every step).
        constexpr float k_shape_tolerance = 1.0e-5f;

        constexpr int k_circle_segments = 24;
        constexpr float k_contact_marker_size = 0.05f;
        constexpr float k_pi = 3.14159265358979f;

        // Worlds sharing the process-wide Jolt state (allocator hooks, type
        // factory, collision dispatch tables).
        int g_jolt_users = 0;

        void jolt_trace(const char* format, ...)
        {
            char message[512]{};
            va_list args;
            va_start(args, format);
            std::vsnprintf(message, sizeof(message), format, args);
            va_end(args);
            LOG_WRN("Jolt: %s", message);
        }

#ifdef JPH_ENABLE_ASSERTS
        // Log and carry on rather than trapping into a debugger that is
        // usually not attached.
        bool jolt_assert_failed(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            LOG_ERR("Jolt assertion failed: %s (%s) at %s:%u",
                    expression,
                    message != nullptr ? message : "no message",
                    file,
                    line);
            return false;
        }
#endif

        void acquire_jolt()
        {
            if (g_jolt_users > 0)
            {
                ++g_jolt_users;
                return;
            }
            JPH::RegisterDefaultAllocator();
            JPH::Trace = &jolt_trace;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &jolt_assert_failed;)
            // The JPH_* configuration this unit was compiled with must match
            // the library's; RegisterTypes would abort on a mismatch, so check
            // first and fail the way the engine reports errors.
            if (!JPH::VerifyJoltVersionID())
            {
                LOG_FTL("Physics: Jolt was built with a different configuration than the engine expects");
                throw std::runtime_error{"physics: Jolt library / header configuration mismatch"};
            }
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
            g_jolt_users = 1;
        }

        void release_jolt()
        {
            if (g_jolt_users == 0 || --g_jolt_users > 0)
            {
                return;
            }
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        // Two object layers — static bodies and everything that moves — each
        // with its own broad-phase tree. Static bodies never test against
        // each other.

        constexpr JPH::ObjectLayer k_layer_static = 0;
        constexpr JPH::ObjectLayer k_layer_moving = 1;
        constexpr JPH::BroadPhaseLayer k_broad_phase_static{0};
        constexpr JPH::BroadPhaseLayer k_broad_phase_moving{1};
        constexpr JPH::uint k_broad_phase_count = 2;

        struct broad_phase_layers final : JPH::BroadPhaseLayerInterface
        {
            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return k_broad_phase_count;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return layer == k_layer_static ? k_broad_phase_static : k_broad_phase_moving;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == k_broad_phase_static ? "static" : "moving";
            }
#endif
        };

        struct object_vs_broad_phase final : JPH::ObjectVsBroadPhaseLayerFilter
        {
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad_phase) const override
            {
                return layer != k_layer_static || broad_phase == k_broad_phase_moving;
            }
        };

        struct object_layer_pairs final : JPH::ObjectLayerPairFilter
        {
            bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
            {
                return first != k_layer_static || second != k_layer_static;
            }
        };

        // Ray casts skip triggers unless asked for them.
        struct trigger_body_filter final : JPH::BodyFilter
        {
            explicit trigger_body_filter(bool include_triggers) : m_include_triggers{include_triggers} {}

            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                return m_include_triggers || !body.IsSensor();
            }

        private:
            bool m_include_triggers;
        };

        JPH::Vec3 to_jolt(const math::vec3& v)
        {
            return JPH::Vec3{v.x, v.y, v.z};
        }

        JPH::RVec3 to_jolt_position(const math::vec3& v)
        {
            return JPH::RVec3{JPH::Real(v.x), JPH::Real(v.y), JPH::Real(v.z)};
        }

        JPH::Quat to_jolt(const math::quat& q)
        {
            return JPH::Quat{q.x, q.y, q.z, q.w}.Normalized();
        }

        math::vec3 from_jolt(JPH::Vec3Arg v)
        {
            return math::vec3{v.GetX(), v.GetY(), v.GetZ()};
        }

        math::vec3 from_jolt_position(JPH::RVec3Arg v)
        {
            return math::vec3{static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
        }

        math::quat from_jolt(JPH::QuatArg q)
        {
            return math::quat{q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
        }

        bool is_zero(const math::vec3& v)
        {
            return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
        }

        float max_component(const math::vec3& v)
        {
            return std::max({v.x, v.y, v.z});
        }

        math::vec3 abs_components(const math::vec3& v)
        {
            return math::vec3{std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)};
        }

        // A node's world transform split into what a body can carry (position
        // and rotation) and what goes into its shape (scale).
        struct pose
        {
            math::vec3 position{};
            math::quat rotation{};
            math::vec3 scale{1.0f, 1.0f, 1.0f};
        };

        pose decompose(const math::mat4& m)
        {
            const math::vec3 x_axis{m.m[0], m.m[1], m.m[2]};
            const math::vec3 y_axis{m.m[4], m.m[5], m.m[6]};
            const math::vec3 z_axis{m.m[8], m.m[9], m.m[10]};
            pose out;
            out.position = math::vec3{m.m[12], m.m[13], m.m[14]};
            math::vec3 scale{math::length(x_axis), math::length(y_axis), math::length(z_axis)};
            constexpr float degenerate = 1.0e-8f;
            if (scale.x < degenerate || scale.y < degenerate || scale.z < degenerate)
            {
                // A collapsed axis has no orientation to recover.
                out.scale = scale;
                return out;
            }
            math::vec3 x_unit = x_axis / scale.x;
            const math::vec3 y_unit = y_axis / scale.y;
            const math::vec3 z_unit = z_axis / scale.z;
            // A mirroring transform: fold the reflection into the X scale so
            // the remaining basis is a proper rotation.
            if (math::dot(math::cross(x_unit, y_unit), z_unit) < 0.0f)
            {
                scale.x = -scale.x;
                x_unit = -x_unit;
            }
            out.scale = scale;
            out.rotation = math::normalize(math::quat_from_basis(x_unit, y_unit, z_unit));
            return out;
        }

        // Whether @p node_pose is not (within write-back noise) the pose last
        // written to the node.
        bool pose_moved(const pose& node_pose, const math::vec3& position, const math::quat& rotation)
        {
            const math::vec3 delta = node_pose.position - position;
            const float reach = std::max({1.0f, std::fabs(position.x), std::fabs(position.y), std::fabs(position.z)});
            const float tolerance = k_position_tolerance * reach;
            if (math::dot(delta, delta) > tolerance * tolerance)
            {
                return true;
            }
            return std::fabs(math::dot(node_pose.rotation, rotation)) < 1.0f - k_rotation_tolerance;
        }

        // Whether @p node_pose differs at all from the pose last pushed into a
        // body the node drives (static, kinematic): nothing writes to such a
        // node, so any change is the game's.
        bool pose_changed(const pose& node_pose, const math::vec3& position, const math::quat& rotation)
        {
            const math::quat& q = node_pose.rotation;
            return node_pose.position != position || q.w != rotation.w || q.x != rotation.x || q.y != rotation.y ||
                   q.z != rotation.z;
        }

        // Places @p target at world @p position / @p rotation. Exact for a
        // root, and under ancestors that are unscaled or uniformly scaled.
        void write_pose(runtime::node& target, const math::vec3& position, const math::quat& rotation)
        {
            runtime::node* parent = target.parent();
            if (parent == nullptr)
            {
                target.transform.set_position(position);
                target.transform.set_quaternion(rotation);
                return;
            }
            const pose parent_pose = decompose(parent->world_matrix());
            target.set_world_position(position);
            target.transform.set_quaternion(math::normalize(math::inverse(parent_pose.rotation) * rotation));
        }

        // A collider resolved against its node: mesh-fitted where asked for
        // and scaled into body space. Only the fields its kind uses matter.
        struct shape_desc
        {
            collider_shape kind{collider_shape::box};
            math::vec3 half_extents{0.5f, 0.5f, 0.5f};
            float radius{0.5f};
            float half_height{0.5f};
            math::vec3 center{};
            std::vector<math::vec3> points;
        };

        bool nearly_equal(float a, float b)
        {
            return std::fabs(a - b) <= k_shape_tolerance * std::max({1.0f, std::fabs(a), std::fabs(b)});
        }

        bool nearly_equal(const math::vec3& a, const math::vec3& b)
        {
            return nearly_equal(a.x, b.x) && nearly_equal(a.y, b.y) && nearly_equal(a.z, b.z);
        }

        bool same_shape(const shape_desc& a, const shape_desc& b)
        {
            if (a.kind != b.kind)
            {
                return false;
            }
            switch (a.kind)
            {
            case collider_shape::box:
                return nearly_equal(a.half_extents, b.half_extents) && nearly_equal(a.center, b.center);
            case collider_shape::sphere:
                return nearly_equal(a.radius, b.radius) && nearly_equal(a.center, b.center);
            case collider_shape::capsule:
                return nearly_equal(a.radius, b.radius) && nearly_equal(a.half_height, b.half_height) &&
                       nearly_equal(a.center, b.center);
            case collider_shape::convex_hull:
                return a.points.size() == b.points.size() &&
                       std::equal(a.points.begin(),
                                  a.points.end(),
                                  b.points.begin(),
                                  [](const math::vec3& p, const math::vec3& q) { return nearly_equal(p, q); });
            }
            return false;
        }

        std::vector<math::vec3> box_corners(const math::vec3& center, const math::vec3& half_extents)
        {
            std::vector<math::vec3> corners;
            corners.reserve(8);
            for (int i = 0; i < 8; ++i)
            {
                corners.push_back(math::vec3{center.x + ((i & 1) != 0 ? half_extents.x : -half_extents.x),
                                             center.y + ((i & 2) != 0 ? half_extents.y : -half_extents.y),
                                             center.z + ((i & 4) != 0 ? half_extents.z : -half_extents.z)});
            }
            return corners;
        }

        // The bounds, in @p owner's local space, of what it draws: its
        // mesh_component's model, else its renderable_component's renderable.
        bool mesh_bounds(runtime::node& owner, math::aabb& out)
        {
            const runtime::mesh_component* mesh = owner.get_component<runtime::mesh_component>();
            if (mesh != nullptr && mesh->model() != nullptr && mesh->model()->local_bounds(out))
            {
                return true;
            }
            const runtime::renderable_component* drawn = owner.get_component<runtime::renderable_component>();
            return drawn != nullptr && drawn->get() != nullptr && drawn->get()->local_bounds(out);
        }

        shape_desc resolve_shape(runtime::node& owner, const collider_state* collider, const math::vec3& scale)
        {
            // A rigidbody without a collider gets the default collider: a box
            // fitted to what the node draws (a unit box when it draws nothing).
            static const collider_settings defaults{};
            const collider_settings& settings = collider != nullptr ? collider->settings : defaults;

            shape_desc desc;
            desc.kind = settings.shape;
            desc.half_extents = settings.half_extents;
            desc.radius = settings.radius;
            desc.half_height = settings.half_height;
            desc.center = settings.center;

            math::aabb bounds;
            if (settings.fit_to_mesh && mesh_bounds(owner, bounds))
            {
                const math::vec3 extents = bounds.extents();
                desc.center = bounds.center();
                switch (settings.shape)
                {
                case collider_shape::box:
                    desc.half_extents = extents;
                    break;
                case collider_shape::sphere:
                    desc.radius = max_component(extents);
                    break;
                case collider_shape::capsule:
                    desc.radius = std::max(extents.x, extents.y);
                    desc.half_height = std::max(extents.z - desc.radius, 0.0f);
                    break;
                case collider_shape::convex_hull:
                    desc.points = box_corners(desc.center, extents);
                    desc.center = math::vec3{};
                    break;
                }
            }
            else if (settings.shape == collider_shape::convex_hull)
            {
                desc.points = settings.points;
                if (desc.points.empty())
                {
                    desc.points = box_corners(math::vec3{}, settings.half_extents);
                }
            }

            // Into body space: the body carries the node's world position and
            // rotation, the shape its world scale.
            const math::vec3 size = abs_components(scale);
            desc.center = desc.center * scale;
            switch (desc.kind)
            {
            case collider_shape::box:
            {
                const math::vec3 half = abs_components(desc.half_extents) * size;
                desc.half_extents = math::vec3{
                    std::max(half.x, k_min_extent), std::max(half.y, k_min_extent), std::max(half.z, k_min_extent)};
                break;
            }
            case collider_shape::sphere:
                desc.radius = std::max(std::fabs(desc.radius) * max_component(size), k_min_extent);
                break;
            case collider_shape::capsule:
                desc.radius = std::max(std::fabs(desc.radius) * std::max(size.x, size.y), k_min_extent);
                desc.half_height = std::fabs(desc.half_height) * size.z;
                break;
            case collider_shape::convex_hull:
                for (math::vec3& point : desc.points)
                {
                    point = point * scale;
                }
                break;
            }
            return desc;
        }

        JPH::ShapeRefC create_shape(JPH::ShapeSettings& settings, const char* what)
        {
            // Settings built on the stack are reference counted objects that
            // nothing else owns.
            settings.SetEmbedded();
            const JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                LOG_WRN("Physics: cannot build a %s shape: %s", what, result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }

        // Builds the Jolt shape for @p desc. @p drawn receives what the debug
        // draw should show — @p desc itself, unless it had to fall back (a
        // degenerate hull becomes its bounding box, a capsule without a
        // cylinder a sphere).
        JPH::ShapeRefC build_shape(const shape_desc& desc, shape_desc& drawn)
        {
            drawn = desc;
            JPH::ShapeRefC shape;
            JPH::Quat rotation = JPH::Quat::sIdentity();
            switch (desc.kind)
            {
            case collider_shape::box:
            {
                JPH::BoxShapeSettings settings{to_jolt(desc.half_extents)};
                shape = create_shape(settings, "box");
                break;
            }
            case collider_shape::sphere:
            {
                JPH::SphereShapeSettings settings{desc.radius};
                shape = create_shape(settings, "sphere");
                break;
            }
            case collider_shape::capsule:
            {
                if (desc.half_height <= k_min_extent)
                {
                    drawn.kind = collider_shape::sphere;
                    JPH::SphereShapeSettings settings{desc.radius};
                    shape = create_shape(settings, "sphere");
                    break;
                }
                // Jolt's capsule runs along its Y axis; the engine's along Z.
                rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * k_pi);
                JPH::CapsuleShapeSettings settings{desc.half_height, desc.radius};
                shape = create_shape(settings, "capsule");
                break;
            }
            case collider_shape::convex_hull:
            {
                if (desc.points.size() >= 4)
                {
                    JPH::Array<JPH::Vec3> points;
                    points.reserve(desc.points.size());
                    for (const math::vec3& point : desc.points)
                    {
                        points.push_back(to_jolt(point));
                    }
                    JPH::ConvexHullShapeSettings settings{points};
                    shape = create_shape(settings, "convex hull");
                }
                if (shape != nullptr)
                {
                    // Hull points are already in body space; nothing to offset.
                    return shape;
                }
                // Too few points, or all on a plane: use their bounding box.
                math::aabb bounds{desc.points.empty() ? math::vec3{} : desc.points.front(),
                                  desc.points.empty() ? math::vec3{} : desc.points.front()};
                for (const math::vec3& point : desc.points)
                {
                    bounds = math::merge(bounds, point);
                }
                const math::vec3 half = bounds.extents();
                drawn.kind = collider_shape::box;
                drawn.center = bounds.center();
                drawn.half_extents = math::vec3{
                    std::max(half.x, k_min_extent), std::max(half.y, k_min_extent), std::max(half.z, k_min_extent)};
                JPH::BoxShapeSettings settings{to_jolt(drawn.half_extents)};
                shape = create_shape(settings, "box");
                break;
            }
            }
            if (shape == nullptr)
            {
                return nullptr;
            }
            if (!is_zero(drawn.center) || rotation != JPH::Quat::sIdentity())
            {
                JPH::RotatedTranslatedShapeSettings settings{to_jolt(drawn.center), rotation, shape.GetPtr()};
                shape = create_shape(settings, "offset");
            }
            return shape;
        }

        JPH::EMotionType to_motion_type(body_type type)
        {
            switch (type)
            {
            case body_type::static_body:
                return JPH::EMotionType::Static;
            case body_type::kinematic_body:
                return JPH::EMotionType::Kinematic;
            case body_type::dynamic_body:
                return JPH::EMotionType::Dynamic;
            }
            return JPH::EMotionType::Static;
        }

        // The body for one node: what it was built from and what was last
        // pushed into it, so each step applies only what changed.
        struct body_record
        {
            runtime::node* owner{nullptr};
            rigidbody_state* rigidbody{nullptr};
            collider_state* collider{nullptr};

            JPH::BodyID body{};
            bool added{false};
            // A component came or went; build the body afresh.
            bool rebuild{false};

            // The configuration the current body was created with. A change
            // to any of these rebuilds it.
            bool built_with_rigidbody{false};
            body_type built_type{body_type::static_body};
            bool built_trigger{false};

            // The shape the body carries: as resolved (compared every step),
            // as drawn, and the library object.
            shape_desc shape;
            shape_desc drawn;
            JPH::ShapeRefC jolt_shape;

            // Material and mass as last applied.
            rigidbody_settings applied{};

            // The node's world pose as last pushed into the body or written
            // back to the node; a node found elsewhere was moved by game code.
            math::vec3 synced_position{};
            math::quat synced_rotation{};
            bool kinematic_moving{false};

            // A dynamic body's pose after the previous step and after the
            // latest one; the node is drawn blended between the two.
            math::vec3 previous_position{};
            math::quat previous_rotation{};
            math::vec3 current_position{};
            math::quat current_rotation{};
            // The node still has to be moved towards current_*.
            bool interpolating{false};
            bool was_active{false};
        };

        // Puts a body's pose bookkeeping at rest at @p position / @p rotation:
        // nothing to interpolate, and the node is taken to be there.
        void settle_at(body_record& record, const math::vec3& position, const math::quat& rotation)
        {
            record.synced_position = position;
            record.synced_rotation = rotation;
            record.previous_position = position;
            record.previous_rotation = rotation;
            record.current_position = position;
            record.current_rotation = rotation;
            record.interpolating = false;
        }

        bool record_active(const body_record& record)
        {
            return (record.rigidbody == nullptr || record.rigidbody->active) &&
                   (record.collider == nullptr || record.collider->active);
        }

        rigidbody_settings static_settings()
        {
            rigidbody_settings settings;
            settings.type = body_type::static_body;
            return settings;
        }

        std::uint32_t raw_id(const JPH::BodyID& id)
        {
            return id.GetIndexAndSequenceNumber();
        }

        std::uint64_t pair_key(std::uint32_t first, std::uint32_t second)
        {
            return (std::uint64_t{first} << 32) | std::uint64_t{second};
        }

        std::uint64_t sub_shape_key(const JPH::SubShapeID& first, const JPH::SubShapeID& second)
        {
            return (std::uint64_t{first.GetValue()} << 32) | std::uint64_t{second.GetValue()};
        }

        enum class raw_kind : std::uint8_t
        {
            added,
            persisted,
            removed,
        };

        // One contact callback, as recorded on whichever worker thread made
        // it. Jolt orders body 1 before body 2 by id in every callback.
        struct raw_contact
        {
            raw_kind kind{raw_kind::added};
            std::uint32_t body1{0};
            std::uint32_t body2{0};
            std::uint64_t sub_shapes{0};
            bool trigger{false};
            bool body1_is_trigger{false};
            math::vec3 point1{};
            math::vec3 point2{};
            math::vec3 normal{};
        };

        // Buffers the callbacks for the main thread. They arrive concurrently
        // from the physics workers while every body is locked, so nothing
        // here may touch the bodies beyond reading what the callback passes.
        struct contact_recorder final : JPH::ContactListener
        {
            // Triggers only detect solid bodies (two dynamic or kinematic
            // triggers would otherwise report each other).
            JPH::ValidateResult OnContactValidate(const JPH::Body& body1,
                                                  const JPH::Body& body2,
                                                  JPH::RVec3Arg /*base_offset*/,
                                                  const JPH::CollideShapeResult& /*result*/) override
            {
                return body1.IsSensor() && body2.IsSensor() ? JPH::ValidateResult::RejectAllContactsForThisBodyPair
                                                            : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            void OnContactAdded(const JPH::Body& body1,
                                const JPH::Body& body2,
                                const JPH::ContactManifold& manifold,
                                JPH::ContactSettings& /*settings*/) override
            {
                record(raw_kind::added, body1, body2, manifold);
            }

            void OnContactPersisted(const JPH::Body& body1,
                                    const JPH::Body& body2,
                                    const JPH::ContactManifold& manifold,
                                    JPH::ContactSettings& /*settings*/) override
            {
                record(raw_kind::persisted, body1, body2, manifold);
            }

            void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
            {
                raw_contact contact;
                contact.kind = raw_kind::removed;
                contact.body1 = raw_id(pair.GetBody1ID());
                contact.body2 = raw_id(pair.GetBody2ID());
                contact.sub_shapes = sub_shape_key(pair.GetSubShapeID1(), pair.GetSubShapeID2());
                const std::lock_guard<std::mutex> lock{m_mutex};
                m_buffer.push_back(contact);
            }

            // Hands the buffered callbacks to the caller and starts afresh.
            std::vector<raw_contact> take()
            {
                std::vector<raw_contact> out;
                const std::lock_guard<std::mutex> lock{m_mutex};
                out.swap(m_buffer);
                return out;
            }

        private:
            void
            record(raw_kind kind, const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
            {
                raw_contact contact;
                contact.kind = kind;
                contact.body1 = raw_id(body1.GetID());
                contact.body2 = raw_id(body2.GetID());
                contact.sub_shapes = sub_shape_key(manifold.mSubShapeID1, manifold.mSubShapeID2);
                contact.trigger = body1.IsSensor() || body2.IsSensor();
                contact.body1_is_trigger = body1.IsSensor();
                contact.normal = from_jolt(manifold.mWorldSpaceNormal);
                const JPH::uint count = static_cast<JPH::uint>(manifold.mRelativeContactPointsOn1.size());
                if (count > 0)
                {
                    // The centre of the contact patch.
                    JPH::Vec3 sum1 = JPH::Vec3::sZero();
                    JPH::Vec3 sum2 = JPH::Vec3::sZero();
                    for (JPH::uint i = 0; i < count; ++i)
                    {
                        sum1 += manifold.mRelativeContactPointsOn1[i];
                        sum2 += manifold.mRelativeContactPointsOn2[i];
                    }
                    const float scale = 1.0f / static_cast<float>(count);
                    contact.point1 = from_jolt_position(manifold.mBaseOffset + sum1 * scale);
                    contact.point2 = from_jolt_position(manifold.mBaseOffset + sum2 * scale);
                }
                const std::lock_guard<std::mutex> lock{m_mutex};
                m_buffer.push_back(contact);
            }

            std::mutex m_mutex;
            std::vector<raw_contact> m_buffer;
        };

        // A body pair in contact, tracked across steps to turn the library's
        // per-sub-shape callbacks into enter / stay / exit.
        struct contact_pair
        {
            std::uint32_t body1{0};
            std::uint32_t body2{0};
            // The sub-shape pairs currently touching.
            std::vector<std::uint64_t> sub_shapes;
            bool trigger{false};
            bool body1_is_trigger{false};
            // enter has been reported and exit has not.
            bool reported{false};
            // Both bodies fell asleep touching: the library drops the
            // contact, the pair stays in contact silently.
            bool dormant{false};
            // Added or persisted this step.
            bool touched{false};
            math::vec3 point1{};
            math::vec3 point2{};
            math::vec3 normal{};
        };

        struct pending_event
        {
            std::uint64_t key{0};
            contact_phase phase{contact_phase::enter};
            std::uint32_t body1{0};
            std::uint32_t body2{0};
            bool trigger{false};
            bool body1_is_trigger{false};
            math::vec3 point1{};
            math::vec3 point2{};
            math::vec3 normal{};
        };

        void add_segment(std::vector<math::vec3>& positions,
                         std::vector<math::vec3>& colors,
                         const math::vec3& from,
                         const math::vec3& to,
                         const math::vec3& color)
        {
            positions.push_back(from);
            positions.push_back(to);
            colors.push_back(color);
            colors.push_back(color);
        }

        // An arc of @p radius around @p center in the plane of the unit axes
        // @p u and @p v, from angle @p start to @p end (radians from @p u).
        void add_arc(std::vector<math::vec3>& positions,
                     std::vector<math::vec3>& colors,
                     const math::vec3& center,
                     const math::vec3& u,
                     const math::vec3& v,
                     float radius,
                     float start,
                     float end,
                     int segments,
                     const math::vec3& color)
        {
            math::vec3 previous = center + (u * std::cos(start) + v * std::sin(start)) * radius;
            for (int i = 1; i <= segments; ++i)
            {
                const float angle = start + (end - start) * static_cast<float>(i) / static_cast<float>(segments);
                const math::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
                add_segment(positions, colors, previous, next, color);
                previous = next;
            }
        }

        math::vec3 debug_color(const body_record& record, bool awake)
        {
            if (record.built_trigger)
            {
                return math::vec3{1.0f, 0.8f, 0.2f};
            }
            switch (record.built_type)
            {
            case body_type::static_body:
                return math::vec3{0.7f, 0.7f, 0.7f};
            case body_type::kinematic_body:
                return math::vec3{0.3f, 0.6f, 1.0f};
            case body_type::dynamic_body:
                return awake ? math::vec3{0.2f, 1.0f, 0.3f} : math::vec3{0.25f, 0.5f, 0.3f};
            }
            return math::vec3{1.0f, 1.0f, 1.0f};
        }
    } // namespace

    struct world::impl
    {
        bool initialized{false};
        bool jolt_acquired{false};
        math::vec3 gravity{0.0f, 0.0f, -9.81f};
        std::uint64_t revision{0};
        core::event_bus* events{nullptr};
        unsigned reported_update_errors{0};
        bool reported_body_limit{false};

        // The system keeps pointers to the layer tables and the contact
        // recorder, so they are declared (and outlive it) first.
        broad_phase_layers broad_phase;
        object_vs_broad_phase object_vs_broad_phase_filter;
        object_layer_pairs object_layer_pair_filter;
        contact_recorder contacts;
        std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
        std::unique_ptr<JPH::JobSystemThreadPool> job_system;
        std::unique_ptr<JPH::PhysicsSystem> system;

        std::unordered_map<const runtime::node*, std::unique_ptr<body_record>> records;
        // Live bodies by raw id, and the bodies rebuilt this step (their old
        // ids still name the record until the step's contacts are resolved).
        std::unordered_map<std::uint32_t, body_record*> by_body;
        std::unordered_map<std::uint32_t, body_record*> retired;
        std::unordered_map<std::uint64_t, contact_pair> pairs;
        std::vector<pending_event> pending;
        std::vector<math::vec3> contact_points;

        std::unique_ptr<debug_draw> debug;

        body_record* find(std::uint32_t id) const
        {
            const auto live = by_body.find(id);
            if (live != by_body.end())
            {
                return live->second;
            }
            const auto old = retired.find(id);
            return old != retired.end() ? old->second : nullptr;
        }

        body_record& record_for(runtime::node& owner)
        {
            std::unique_ptr<body_record>& slot = records[&owner];
            if (slot == nullptr)
            {
                slot = std::make_unique<body_record>();
                slot->owner = &owner;
            }
            return *slot;
        }

        // Removes and destroys @p record's body. A rebuild keeps the old id
        // resolvable for the rest of the step, so its contacts end with exit.
        void remove_body(body_record& record, bool rebuilding)
        {
            if (record.body.IsInvalid())
            {
                return;
            }
            JPH::BodyInterface& bodies = system->GetBodyInterface();
            const std::uint32_t id = raw_id(record.body);
            if (record.added)
            {
                bodies.RemoveBody(record.body);
            }
            bodies.DestroyBody(record.body);
            by_body.erase(id);
            if (rebuilding)
            {
                retired[id] = &record;
            }
            record.body = JPH::BodyID{};
            record.added = false;
            record.jolt_shape = nullptr;
            ++revision;
        }

        // Drops @p owner's record once neither component is left on it.
        void release_if_unused(const runtime::node* owner)
        {
            const auto it = records.find(owner);
            if (it == records.end())
            {
                return;
            }
            body_record& record = *it->second;
            if (record.rigidbody != nullptr || record.collider != nullptr)
            {
                record.rebuild = true;
                return;
            }
            remove_body(record, false);
            for (auto old = retired.begin(); old != retired.end();)
            {
                old = old->second == &record ? retired.erase(old) : std::next(old);
            }
            records.erase(it);
        }

        void set_mass(const JPH::BodyID& id, float mass)
        {
            JPH::BodyLockWrite lock{system->GetBodyLockInterface(), id};
            if (!lock.Succeeded())
            {
                return;
            }
            JPH::Body& body = lock.GetBody();
            JPH::MotionProperties* motion = body.GetMotionPropertiesUnchecked();
            if (body.IsStatic() || motion == nullptr)
            {
                return;
            }
            JPH::MassProperties properties = body.GetShape()->GetMassProperties();
            if (mass > 0.0f)
            {
                properties.ScaleToMass(mass);
            }
            motion->SetMassProperties(JPH::EAllowedDOFs::All, properties);
        }

        bool create_body(body_record& record, const pose& node_pose, body_type type, bool trigger)
        {
            shape_desc desc = resolve_shape(*record.owner, record.collider, node_pose.scale);
            shape_desc drawn;
            JPH::ShapeRefC shape = build_shape(desc, drawn);
            if (shape == nullptr)
            {
                return false;
            }
            const rigidbody_settings settings =
                record.rigidbody != nullptr ? record.rigidbody->settings : static_settings();
            JPH::BodyCreationSettings creation{shape.GetPtr(),
                                               to_jolt_position(node_pose.position),
                                               to_jolt(node_pose.rotation),
                                               to_motion_type(type),
                                               type == body_type::static_body ? k_layer_static : k_layer_moving};
            creation.mIsSensor = trigger;
            creation.mFriction = settings.friction;
            creation.mRestitution = settings.restitution;
            creation.mLinearDamping = std::max(settings.linear_damping, 0.0f);
            creation.mAngularDamping = std::max(settings.angular_damping, 0.0f);
            creation.mGravityFactor = settings.gravity_scale;
            if (type != body_type::static_body && settings.mass > 0.0f)
            {
                creation.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                creation.mMassPropertiesOverride.mMass = settings.mass;
            }
            if (type == body_type::dynamic_body && record.rigidbody != nullptr)
            {
                creation.mLinearVelocity = to_jolt(record.rigidbody->linear_velocity);
                creation.mAngularVelocity = to_jolt(record.rigidbody->angular_velocity);
            }

            JPH::BodyInterface& bodies = system->GetBodyInterface();
            JPH::Body* body = bodies.CreateBody(creation);
            if (body == nullptr)
            {
                if (!reported_body_limit)
                {
                    LOG_WRN("Physics: body limit (%u) reached; further bodies are not simulated", k_max_bodies);
                    reported_body_limit = true;
                }
                return false;
            }
            record.body = body->GetID();
            by_body[raw_id(record.body)] = &record;
            bodies.AddBody(record.body,
                           type == body_type::dynamic_body ? JPH::EActivation::Activate
                                                           : JPH::EActivation::DontActivate);
            record.added = true;
            record.rebuild = false;
            record.built_with_rigidbody = record.rigidbody != nullptr;
            record.built_type = type;
            record.built_trigger = trigger;
            record.shape = std::move(desc);
            record.drawn = std::move(drawn);
            record.jolt_shape = shape;
            record.applied = settings;
            settle_at(record, node_pose.position, node_pose.rotation);
            record.kinematic_moving = false;
            record.was_active = type == body_type::dynamic_body;
            ++revision;
            return true;
        }

        void set_added(body_record& record, bool add, const pose& node_pose)
        {
            JPH::BodyInterface& bodies = system->GetBodyInterface();
            if (add && !record.added)
            {
                // The node may have moved while it was disabled.
                bodies.SetPositionAndRotation(record.body,
                                              to_jolt_position(node_pose.position),
                                              to_jolt(node_pose.rotation),
                                              JPH::EActivation::DontActivate);
                settle_at(record, node_pose.position, node_pose.rotation);
                bodies.AddBody(record.body,
                               record.built_type == body_type::dynamic_body ? JPH::EActivation::Activate
                                                                            : JPH::EActivation::DontActivate);
                record.added = true;
                ++revision;
            }
            else if (!add && record.added)
            {
                bodies.RemoveBody(record.body);
                record.added = false;
                ++revision;
            }
        }

        void apply_shape(body_record& record, shape_desc desc)
        {
            shape_desc drawn;
            JPH::ShapeRefC shape = build_shape(desc, drawn);
            if (shape == nullptr)
            {
                // Keep the previous shape; do not retry every step.
                record.shape = std::move(desc);
                return;
            }
            const bool dynamic = record.built_type == body_type::dynamic_body;
            const float mass = record.rigidbody != nullptr ? record.rigidbody->settings.mass : 0.0f;
            system->GetBodyInterface().SetShape(record.body,
                                                shape.GetPtr(),
                                                false,
                                                dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            if (record.built_type != body_type::static_body)
            {
                set_mass(record.body, mass);
            }
            record.shape = std::move(desc);
            record.drawn = std::move(drawn);
            record.jolt_shape = shape;
            ++revision;
        }

        void apply_material(body_record& record, const rigidbody_settings& settings)
        {
            JPH::BodyInterface& bodies = system->GetBodyInterface();
            rigidbody_settings& applied = record.applied;
            if (settings.friction != applied.friction)
            {
                bodies.SetFriction(record.body, settings.friction);
            }
            if (settings.restitution != applied.restitution)
            {
                bodies.SetRestitution(record.body, settings.restitution);
            }
            if (record.built_type != body_type::static_body)
            {
                if (settings.gravity_scale != applied.gravity_scale)
                {
                    bodies.SetGravityFactor(record.body, settings.gravity_scale);
                }
                if (settings.linear_damping != applied.linear_damping ||
                    settings.angular_damping != applied.angular_damping)
                {
                    JPH::BodyLockWrite lock{system->GetBodyLockInterface(), record.body};
                    if (lock.Succeeded() && !lock.GetBody().IsStatic())
                    {
                        JPH::MotionProperties* motion = lock.GetBody().GetMotionProperties();
                        motion->SetLinearDamping(std::max(settings.linear_damping, 0.0f));
                        motion->SetAngularDamping(std::max(settings.angular_damping, 0.0f));
                    }
                }
                if (settings.mass != applied.mass)
                {
                    set_mass(record.body, settings.mass);
                }
            }
            applied = settings;
        }

        void sync_pose(body_record& record, const pose& node_pose, float delta_seconds)
        {
            JPH::BodyInterface& bodies = system->GetBodyInterface();
            const bool moved = record.built_type == body_type::dynamic_body
                                   ? pose_moved(node_pose, record.synced_position, record.synced_rotation)
                                   : pose_changed(node_pose, record.synced_position, record.synced_rotation);
            const JPH::RVec3 position = to_jolt_position(node_pose.position);
            const JPH::Quat rotation = to_jolt(node_pose.rotation);
            switch (record.built_type)
            {
            case body_type::static_body:
                if (moved)
                {
                    bodies.SetPositionAndRotation(record.body, position, rotation, JPH::EActivation::DontActivate);
                    ++revision;
                }
                break;
            case body_type::kinematic_body:
                // Driven towards the node, arriving at the end of the step. A
                // node that stopped gets one more move to its own pose, which
                // zeroes the velocity.
                if (moved || record.kinematic_moving)
                {
                    bodies.MoveKinematic(record.body, position, rotation, delta_seconds);
                    record.kinematic_moving = moved;
                }
                break;
            case body_type::dynamic_body:
                // Game code moved the node: teleport, keeping the velocity,
                // and do not blend the node back from where it was.
                if (moved)
                {
                    bodies.SetPositionAndRotation(record.body, position, rotation, JPH::EActivation::Activate);
                    settle_at(record, node_pose.position, node_pose.rotation);
                }
                break;
            }
            if (moved)
            {
                record.synced_position = node_pose.position;
                record.synced_rotation = node_pose.rotation;
            }
        }

        void apply_forces(body_record& record)
        {
            JPH::BodyInterface& bodies = system->GetBodyInterface();
            rigidbody_state& state = *record.rigidbody;
            if (state.velocity_dirty)
            {
                bodies.SetLinearAndAngularVelocity(
                    record.body, to_jolt(state.linear_velocity), to_jolt(state.angular_velocity));
            }
            if (!is_zero(state.force))
            {
                bodies.AddForce(record.body, to_jolt(state.force));
            }
            if (!is_zero(state.torque))
            {
                bodies.AddTorque(record.body, to_jolt(state.torque));
            }
            if (!is_zero(state.impulse))
            {
                bodies.AddImpulse(record.body, to_jolt(state.impulse));
            }
            if (!is_zero(state.angular_impulse))
            {
                bodies.AddAngularImpulse(record.body, to_jolt(state.angular_impulse));
            }
            for (const point_force& entry : state.forces_at)
            {
                bodies.AddForce(record.body, to_jolt(entry.force), to_jolt_position(entry.point));
            }
            for (const point_force& entry : state.impulses_at)
            {
                bodies.AddImpulse(record.body, to_jolt(entry.force), to_jolt_position(entry.point));
            }
            if (state.wake_requested)
            {
                bodies.ActivateBody(record.body);
            }
        }

        // Scene -> world, before the step.
        void sync_in(float delta_seconds)
        {
            for (auto& entry : records)
            {
                body_record& record = *entry.second;
                const pose node_pose = decompose(record.owner->world_matrix());
                const body_type type =
                    record.rigidbody != nullptr ? record.rigidbody->settings.type : body_type::static_body;
                const bool trigger = record.collider != nullptr && record.collider->settings.is_trigger;
                if (!record.body.IsInvalid() &&
                    (record.rebuild || record.built_with_rigidbody != (record.rigidbody != nullptr) ||
                     record.built_type != type || record.built_trigger != trigger))
                {
                    remove_body(record, true);
                }

                const bool active = record_active(record);
                bool fresh = false;
                if (record.body.IsInvalid())
                {
                    // Created lazily, and only while the node is enabled.
                    fresh = active && create_body(record, node_pose, type, trigger);
                    if (!fresh)
                    {
                        if (record.rigidbody != nullptr)
                        {
                            record.rigidbody->clear_pending();
                        }
                        continue;
                    }
                }

                if (!fresh)
                {
                    set_added(record, active, node_pose);
                    if (!record.added)
                    {
                        if (record.rigidbody != nullptr)
                        {
                            record.rigidbody->clear_pending();
                        }
                        continue;
                    }
                    shape_desc desc = resolve_shape(*record.owner, record.collider, node_pose.scale);
                    if (!same_shape(desc, record.shape))
                    {
                        apply_shape(record, std::move(desc));
                    }
                    if (record.rigidbody != nullptr)
                    {
                        apply_material(record, record.rigidbody->settings);
                    }
                    sync_pose(record, node_pose, delta_seconds);
                }

                if (record.rigidbody != nullptr)
                {
                    // Velocities, forces and impulses move dynamic bodies
                    // only; a kinematic body follows its node.
                    if (type == body_type::dynamic_body)
                    {
                        apply_forces(record);
                    }
                    record.rigidbody->velocity_dirty = false;
                    record.rigidbody->clear_pending();
                }
            }
        }

        // World -> scene, after the step.
        void sync_out()
        {
            JPH::BodyInterface& bodies = system->GetBodyInterface();
            for (auto& entry : records)
            {
                body_record& record = *entry.second;
                if (record.body.IsInvalid() || !record.added)
                {
                    continue;
                }
                const bool awake = bodies.IsActive(record.body);
                // A body that fell asleep this step still moved during it.
                if (record.built_type == body_type::dynamic_body && (awake || record.was_active))
                {
                    JPH::RVec3 position;
                    JPH::Quat rotation;
                    bodies.GetPositionAndRotation(record.body, position, rotation);
                    record.previous_position = record.current_position;
                    record.previous_rotation = record.current_rotation;
                    record.current_position = from_jolt_position(position);
                    record.current_rotation = from_jolt(rotation);
                    if (!awake)
                    {
                        // At rest: let the next interpolation land on it exactly.
                        record.previous_position = record.current_position;
                        record.previous_rotation = record.current_rotation;
                    }
                    record.interpolating = true;
                }
                if (record.rigidbody != nullptr && record.built_with_rigidbody &&
                    record.built_type != body_type::static_body)
                {
                    JPH::Vec3 linear;
                    JPH::Vec3 angular;
                    bodies.GetLinearAndAngularVelocity(record.body, linear, angular);
                    record.rigidbody->linear_velocity = from_jolt(linear);
                    record.rigidbody->angular_velocity = from_jolt(angular);
                    record.rigidbody->sleeping = !awake;
                }
                record.was_active = awake;
            }
        }

        // Writes every moving dynamic body's pose to its node, @p alpha of
        // the way from the previous step's pose to the latest one.
        void interpolate(float alpha)
        {
            const float t = std::clamp(alpha, 0.0f, 1.0f);
            for (auto& entry : records)
            {
                body_record& record = *entry.second;
                if (!record.interpolating || !record.added || record.built_type != body_type::dynamic_body)
                {
                    continue;
                }
                if (pose_moved(decompose(record.owner->world_matrix()), record.synced_position, record.synced_rotation))
                {
                    // Game code placed the node since the last write: leave it
                    // there for the next step to teleport the body to.
                    record.interpolating = false;
                    continue;
                }
                record.synced_position = math::lerp(record.previous_position, record.current_position, t);
                record.synced_rotation = math::slerp(record.previous_rotation, record.current_rotation, t);
                write_pose(*record.owner, record.synced_position, record.synced_rotation);
                // Once the two poses agree the node is where the body rests.
                const math::quat& from = record.previous_rotation;
                const math::quat& to = record.current_rotation;
                record.interpolating = record.previous_position != record.current_position || from.w != to.w ||
                                       from.x != to.x || from.y != to.y || from.z != to.z;
            }
        }

        // Asleep (or static) and still in the simulation.
        bool resting(std::uint32_t id)
        {
            const JPH::BodyID body{id};
            const JPH::BodyInterface& bodies = system->GetBodyInterface();
            return bodies.IsAdded(body) && !bodies.IsActive(body);
        }

        void queue(std::uint64_t key, const contact_pair& pair, contact_phase phase)
        {
            pending_event event;
            event.key = key;
            event.phase = phase;
            event.body1 = pair.body1;
            event.body2 = pair.body2;
            event.trigger = pair.trigger;
            event.body1_is_trigger = pair.body1_is_trigger;
            event.point1 = pair.point1;
            event.point2 = pair.point2;
            event.normal = pair.normal;
            pending.push_back(event);
        }

        // Folds this step's callbacks into the tracked pairs and queues the
        // resulting enter / stay / exit events.
        void process_contacts()
        {
            const std::vector<raw_contact> raw = contacts.take();
            contact_points.clear();
            for (const raw_contact& contact : raw)
            {
                const std::uint64_t key = pair_key(contact.body1, contact.body2);
                if (contact.kind == raw_kind::removed)
                {
                    const auto it = pairs.find(key);
                    if (it != pairs.end())
                    {
                        std::vector<std::uint64_t>& live = it->second.sub_shapes;
                        live.erase(std::remove(live.begin(), live.end(), contact.sub_shapes), live.end());
                    }
                    continue;
                }
                contact_pair& pair = pairs[key];
                pair.body1 = contact.body1;
                pair.body2 = contact.body2;
                pair.trigger = contact.trigger;
                pair.body1_is_trigger = contact.body1_is_trigger;
                if (std::find(pair.sub_shapes.begin(), pair.sub_shapes.end(), contact.sub_shapes) ==
                    pair.sub_shapes.end())
                {
                    pair.sub_shapes.push_back(contact.sub_shapes);
                }
                pair.touched = true;
                pair.point1 = contact.point1;
                pair.point2 = contact.point2;
                pair.normal = contact.normal;
                if (!contact.trigger)
                {
                    contact_points.push_back(contact.point1);
                }
            }

            for (auto it = pairs.begin(); it != pairs.end();)
            {
                contact_pair& pair = it->second;
                bool keep = true;
                if (!pair.sub_shapes.empty())
                {
                    if (!pair.reported)
                    {
                        queue(it->first, pair, contact_phase::enter);
                        pair.reported = true;
                    }
                    else if (!pair.dormant && pair.touched)
                    {
                        queue(it->first, pair, contact_phase::stay);
                    }
                    // A dormant pair touching again woke up still in contact.
                    pair.dormant = false;
                }
                else if (pair.reported && find(pair.body1) != nullptr && find(pair.body2) != nullptr)
                {
                    if (resting(pair.body1) && resting(pair.body2))
                    {
                        pair.dormant = true;
                    }
                    else
                    {
                        queue(it->first, pair, contact_phase::exit);
                        keep = false;
                    }
                }
                else
                {
                    // Never reported, or a body was destroyed: nothing to say.
                    keep = false;
                }
                pair.touched = false;
                it = keep ? std::next(it) : pairs.erase(it);
            }

            // The callbacks arrive in thread order; dispatch in a stable one.
            std::sort(pending.begin(),
                      pending.end(),
                      [](const pending_event& a, const pending_event& b) { return a.key < b.key; });
        }

        template<typename Listener>
        static void copy_listeners(const std::vector<std::pair<listener_id, Listener>>& registered,
                                   std::vector<Listener>& out)
        {
            for (const auto& entry : registered)
            {
                out.push_back(entry.second);
            }
        }

        // Hands @p event to the listeners on the node behind body @p id. The
        // listeners are copied first: one may remove itself, or the
        // component, while it runs.
        void notify(std::uint32_t id, const collision_event& event)
        {
            const body_record* record = find(id);
            if (record == nullptr)
            {
                return;
            }
            std::vector<collision_listener> listeners;
            if (record->rigidbody != nullptr)
            {
                copy_listeners(record->rigidbody->listeners.collision, listeners);
            }
            if (record->collider != nullptr)
            {
                copy_listeners(record->collider->listeners.collision, listeners);
            }
            for (const collision_listener& listener : listeners)
            {
                listener(event);
            }
        }

        void notify(std::uint32_t id, const trigger_event& event)
        {
            const body_record* record = find(id);
            if (record == nullptr)
            {
                return;
            }
            std::vector<trigger_listener> listeners;
            if (record->rigidbody != nullptr)
            {
                copy_listeners(record->rigidbody->listeners.trigger, listeners);
            }
            if (record->collider != nullptr)
            {
                copy_listeners(record->collider->listeners.trigger, listeners);
            }
            for (const trigger_listener& listener : listeners)
            {
                listener(event);
            }
        }

        // Emits the queued events. Every lookup is redone per event and per
        // party: a listener may remove components or disable nodes.
        void dispatch()
        {
            std::vector<pending_event> ready;
            ready.swap(pending);
            for (const pending_event& entry : ready)
            {
                const body_record* first = find(entry.body1);
                const body_record* second = find(entry.body2);
                if (first == nullptr || second == nullptr)
                {
                    continue;
                }
                runtime::node* first_node = first->owner;
                runtime::node* second_node = second->owner;
                if (entry.trigger)
                {
                    trigger_event event;
                    event.phase = entry.phase;
                    event.trigger = entry.body1_is_trigger ? first_node : second_node;
                    event.other = entry.body1_is_trigger ? second_node : first_node;
                    if (events != nullptr)
                    {
                        events->emit<trigger_event>(event);
                    }
                    notify(entry.body1, event);
                    notify(entry.body2, event);
                }
                else
                {
                    collision_event event;
                    event.phase = entry.phase;
                    event.self = first_node;
                    event.other = second_node;
                    event.point = entry.point1;
                    event.normal = entry.normal;
                    if (events != nullptr)
                    {
                        events->emit<collision_event>(event);
                    }
                    notify(entry.body1, event);
                    collision_event mirrored = event;
                    mirrored.self = second_node;
                    mirrored.other = first_node;
                    mirrored.point = entry.point2;
                    mirrored.normal = -entry.normal;
                    notify(entry.body2, mirrored);
                }
            }
        }

        void report_update_errors(JPH::EPhysicsUpdateError errors)
        {
            const auto bits = static_cast<unsigned>(errors);
            const unsigned fresh = bits & ~reported_update_errors;
            if (fresh == 0)
            {
                return;
            }
            reported_update_errors |= fresh;
            if ((fresh & static_cast<unsigned>(JPH::EPhysicsUpdateError::ManifoldCacheFull)) != 0)
            {
                LOG_WRN("Physics: contact manifold cache full; some contacts were dropped");
            }
            if ((fresh & static_cast<unsigned>(JPH::EPhysicsUpdateError::BodyPairCacheFull)) != 0)
            {
                LOG_WRN("Physics: body pair cache full (%u pairs); some contacts were dropped", k_max_body_pairs);
            }
            if ((fresh & static_cast<unsigned>(JPH::EPhysicsUpdateError::ContactConstraintsFull)) != 0)
            {
                LOG_WRN("Physics: contact constraint buffer full (%u); some contacts were dropped",
                        k_max_contact_constraints);
            }
        }

        void shut_down()
        {
            debug.reset();
            for (auto& entry : records)
            {
                body_record& record = *entry.second;
                if (record.rigidbody != nullptr)
                {
                    record.rigidbody->attached = nullptr;
                }
                if (record.collider != nullptr)
                {
                    record.collider->attached = nullptr;
                }
                if (system != nullptr)
                {
                    remove_body(record, false);
                }
                record.jolt_shape = nullptr;
            }
            records.clear();
            by_body.clear();
            retired.clear();
            pairs.clear();
            pending.clear();
            contact_points.clear();
            (void)contacts.take();
            system.reset();
            job_system.reset();
            temp_allocator.reset();
            if (jolt_acquired)
            {
                release_jolt();
                jolt_acquired = false;
            }
            events = nullptr;
            initialized = false;
        }
    };

    world::world() : m_impl{std::make_unique<impl>()} {}

    world::~world()
    {
        quit();
    }

    void world::init()
    {
        impl& self = *m_impl;
        if (self.initialized)
        {
            return;
        }
        acquire_jolt();
        self.jolt_acquired = true;

        self.temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(k_temp_allocator_bytes);
        const int workers =
            std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, k_max_worker_threads);
        self.job_system =
            std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);
        self.system = std::make_unique<JPH::PhysicsSystem>();
        self.system->Init(k_max_bodies,
                          0,
                          k_max_body_pairs,
                          k_max_contact_constraints,
                          self.broad_phase,
                          self.object_vs_broad_phase_filter,
                          self.object_layer_pair_filter);
        self.system->SetGravity(to_jolt(self.gravity));
        self.system->SetContactListener(&self.contacts);
        self.events = runtime::current_engine().events.get();
        self.initialized = true;

#if _DEBUG
        // The collider wireframe, drawn by the debug pass; toggled from the
        // overlay's Helpers panel.
        self.debug = std::make_unique<debug_draw>(*this);
#endif

        LOG_INF("Physics: Jolt %d.%d.%d up, %d worker thread(s)",
                JPH_VERSION_MAJOR,
                JPH_VERSION_MINOR,
                JPH_VERSION_PATCH,
                workers);
    }

    void world::quit()
    {
        const bool was_up = m_impl->initialized;
        m_impl->shut_down();
        if (was_up)
        {
            LOG_INF("Physics: shut down");
        }
    }

    bool world::is_initialized() const noexcept
    {
        return m_impl->initialized;
    }

    void world::step(float delta_seconds)
    {
        impl& self = *m_impl;
        if (!self.initialized || !(delta_seconds > 0.0f))
        {
            return;
        }
        self.sync_in(delta_seconds);
        if (self.system->GetNumBodies() > 0)
        {
            self.report_update_errors(
                self.system->Update(delta_seconds, 1, self.temp_allocator.get(), self.job_system.get()));
        }
        self.sync_out();
        self.process_contacts();
        ++self.revision;
        self.dispatch();
        // Rebuilt bodies' old ids have had their contacts resolved.
        self.retired.clear();
    }

    void world::interpolate(float alpha)
    {
        if (m_impl->initialized)
        {
            m_impl->interpolate(alpha);
        }
    }

    std::optional<raycast_hit>
    world::raycast(const core::math::ray& ray, float max_distance, bool include_triggers) const
    {
        const impl& self = *m_impl;
        const float length = math::length(ray.direction);
        if (!self.initialized || !(max_distance > 0.0f) || !std::isfinite(max_distance) || !(length > 0.0f) ||
            !std::isfinite(length))
        {
            return std::nullopt;
        }
        const math::vec3 direction = ray.direction / length;
        const JPH::RRayCast cast{to_jolt_position(ray.origin), to_jolt(direction * max_distance)};
        JPH::RayCastResult result;
        const trigger_body_filter filter{include_triggers};
        if (!self.system->GetNarrowPhaseQuery().CastRay(
                cast, result, JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{}, filter))
        {
            return std::nullopt;
        }

        raycast_hit hit;
        hit.distance = result.mFraction * max_distance;
        hit.point = ray.origin + direction * hit.distance;
        {
            JPH::BodyLockRead lock{self.system->GetBodyLockInterface(), result.mBodyID};
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();
                hit.normal =
                    from_jolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, to_jolt_position(hit.point)));
                hit.trigger = body.IsSensor();
            }
        }
        const body_record* record = self.find(raw_id(result.mBodyID));
        if (record == nullptr)
        {
            return std::nullopt;
        }
        hit.node = record->owner;
        return hit;
    }

    void world::set_gravity(const core::math::vec3& gravity)
    {
        m_impl->gravity = gravity;
        if (m_impl->system != nullptr)
        {
            m_impl->system->SetGravity(to_jolt(gravity));
        }
    }

    core::math::vec3 world::gravity() const noexcept
    {
        return m_impl->gravity;
    }

    std::size_t world::body_count() const noexcept
    {
        std::size_t count = 0;
        for (const auto& entry : m_impl->records)
        {
            count += entry.second->added ? 1 : 0;
        }
        return count;
    }

    void world::set_debug_draw(bool enabled)
    {
        if (m_impl->debug != nullptr)
        {
            m_impl->debug->visible = enabled;
        }
    }

    bool world::debug_draw_enabled() const noexcept
    {
        return m_impl->debug != nullptr && m_impl->debug->visible;
    }

    std::uint64_t world::revision() const noexcept
    {
        return m_impl->revision;
    }

    void world::debug_lines(std::vector<core::math::vec3>& positions, std::vector<core::math::vec3>& colors) const
    {
        const impl& self = *m_impl;
        if (!self.initialized)
        {
            return;
        }
        const JPH::BodyInterface& bodies = self.system->GetBodyInterface();
        for (const auto& entry : self.records)
        {
            const body_record& record = *entry.second;
            if (record.body.IsInvalid() || !record.added)
            {
                continue;
            }
            JPH::RVec3 body_position;
            JPH::Quat body_rotation;
            bodies.GetPositionAndRotation(record.body, body_position, body_rotation);
            const math::vec3 origin = from_jolt_position(body_position);
            const math::quat rotation = from_jolt(body_rotation);
            const math::vec3 color = debug_color(record, bodies.IsActive(record.body));
            const shape_desc& shape = record.drawn;
            const math::vec3 center = origin + rotation * shape.center;
            const math::vec3 x_axis = rotation * math::vec3{1.0f, 0.0f, 0.0f};
            const math::vec3 y_axis = rotation * math::vec3{0.0f, 1.0f, 0.0f};
            const math::vec3 z_axis = rotation * math::vec3{0.0f, 0.0f, 1.0f};
            switch (shape.kind)
            {
            case collider_shape::box:
            {
                std::array<math::vec3, 8> corners{};
                const std::vector<math::vec3> local = box_corners(shape.center, shape.half_extents);
                for (std::size_t i = 0; i < corners.size(); ++i)
                {
                    corners[i] = origin + rotation * local[i];
                }
                rendering_engine::editor::build_box_edges(corners, color, positions, colors);
                break;
            }
            case collider_shape::sphere:
            {
                const float r = shape.radius;
                add_arc(positions, colors, center, x_axis, y_axis, r, 0.0f, 2.0f * k_pi, k_circle_segments, color);
                add_arc(positions, colors, center, x_axis, z_axis, r, 0.0f, 2.0f * k_pi, k_circle_segments, color);
                add_arc(positions, colors, center, y_axis, z_axis, r, 0.0f, 2.0f * k_pi, k_circle_segments, color);
                break;
            }
            case collider_shape::capsule:
            {
                const float r = shape.radius;
                const math::vec3 top = center + z_axis * shape.half_height;
                const math::vec3 bottom = center - z_axis * shape.half_height;
                const int half = k_circle_segments / 2;
                add_arc(positions, colors, top, x_axis, y_axis, r, 0.0f, 2.0f * k_pi, k_circle_segments, color);
                add_arc(positions, colors, bottom, x_axis, y_axis, r, 0.0f, 2.0f * k_pi, k_circle_segments, color);
                add_arc(positions, colors, top, x_axis, z_axis, r, 0.0f, k_pi, half, color);
                add_arc(positions, colors, top, y_axis, z_axis, r, 0.0f, k_pi, half, color);
                add_arc(positions, colors, bottom, x_axis, z_axis, r, k_pi, 2.0f * k_pi, half, color);
                add_arc(positions, colors, bottom, y_axis, z_axis, r, k_pi, 2.0f * k_pi, half, color);
                for (const math::vec3& side : {x_axis, -x_axis, y_axis, -y_axis})
                {
                    add_segment(positions, colors, top + side * r, bottom + side * r, color);
                }
                break;
            }
            case collider_shape::convex_hull:
            {
                const auto* hull = static_cast<const JPH::ConvexHullShape*>(record.jolt_shape.GetPtr());
                if (hull == nullptr || hull->GetSubType() != JPH::EShapeSubType::ConvexHull)
                {
                    break;
                }
                // Hull vertices are stored relative to the centre of mass.
                const JPH::RMat44 transform = bodies.GetCenterOfMassTransform(record.body);
                std::vector<JPH::uint> face;
                for (JPH::uint f = 0; f < hull->GetNumFaces(); ++f)
                {
                    face.resize(hull->GetNumVerticesInFace(f));
                    const JPH::uint count = hull->GetFaceVertices(f, static_cast<JPH::uint>(face.size()), face.data());
                    for (JPH::uint i = 0; i < count; ++i)
                    {
                        const math::vec3 from = from_jolt_position(transform * hull->GetPoint(face[i]));
                        const math::vec3 to = from_jolt_position(transform * hull->GetPoint(face[(i + 1) % count]));
                        add_segment(positions, colors, from, to, color);
                    }
                }
                break;
            }
            }
        }

        const math::vec3 contact_color{1.0f, 0.2f, 0.2f};
        for (const math::vec3& point : self.contact_points)
        {
            for (const math::vec3& axis :
                 {math::vec3{1.0f, 0.0f, 0.0f}, math::vec3{0.0f, 1.0f, 0.0f}, math::vec3{0.0f, 0.0f, 1.0f}})
            {
                add_segment(positions,
                            colors,
                            point - axis * k_contact_marker_size,
                            point + axis * k_contact_marker_size,
                            contact_color);
            }
        }
    }

    void world::attach(runtime::node& owner, rigidbody_state& state)
    {
        if (state.attached != nullptr)
        {
            state.attached->detach(state);
        }
        state.owner = &owner;
        impl& self = *m_impl;
        if (!self.initialized)
        {
            LOG_WRN("Physics: rigidbody attached before the physics world is up; it is not simulated");
            return;
        }
        body_record& record = self.record_for(owner);
        if (record.rigidbody != nullptr && record.rigidbody != &state)
        {
            record.rigidbody->attached = nullptr;
        }
        record.rigidbody = &state;
        record.rebuild = true;
        state.attached = this;
    }

    void world::attach(runtime::node& owner, collider_state& state)
    {
        if (state.attached != nullptr)
        {
            state.attached->detach(state);
        }
        state.owner = &owner;
        impl& self = *m_impl;
        if (!self.initialized)
        {
            LOG_WRN("Physics: collider attached before the physics world is up; it is not simulated");
            return;
        }
        body_record& record = self.record_for(owner);
        if (record.collider != nullptr && record.collider != &state)
        {
            record.collider->attached = nullptr;
        }
        record.collider = &state;
        record.rebuild = true;
        state.attached = this;
    }

    void world::detach(rigidbody_state& state)
    {
        if (state.attached != this)
        {
            return;
        }
        state.attached = nullptr;
        const auto it = m_impl->records.find(state.owner);
        if (it != m_impl->records.end() && it->second->rigidbody == &state)
        {
            it->second->rigidbody = nullptr;
            m_impl->release_if_unused(state.owner);
        }
    }

    void world::detach(collider_state& state)
    {
        if (state.attached != this)
        {
            return;
        }
        state.attached = nullptr;
        const auto it = m_impl->records.find(state.owner);
        if (it != m_impl->records.end() && it->second->collider == &state)
        {
            it->second->collider = nullptr;
            m_impl->release_if_unused(state.owner);
        }
    }

    void world::refresh_active(runtime::node& owner)
    {
        impl& self = *m_impl;
        const auto it = self.records.find(&owner);
        if (!self.initialized || it == self.records.end() || it->second->body.IsInvalid())
        {
            return;
        }
        body_record& record = *it->second;
        self.set_added(record, record_active(record), decompose(owner.world_matrix()));
    }
} // namespace runtime::physics
