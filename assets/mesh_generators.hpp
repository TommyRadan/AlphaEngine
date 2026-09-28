// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file mesh_generators.hpp
 * @brief Procedural meshes: a description of each primitive shape and the
 *        generators that build its geometry as @ref assets::mesh_data.
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <vector>

#include <assets/mesh_data.hpp>
#include <core/math/constants.hpp>
#include <core/math/vec3.hpp>

/**
 * @brief The procedural mesh generators.
 *
 * Each primitive shape is a plain description — a struct of its
 * parameters, with defaults — that two overloads turn into what an asset
 * cache needs: @c generate builds the geometry, @c cache_key names it. The
 * key spells the generator, every parameter that shapes the geometry and
 * the vertex record, so two descriptions share a key exactly when they
 * generate the same mesh, and a cache builds and uploads each distinct
 * shape once (see @c rendering_engine::asset_cache::get_or_create_mesh):
 *
 * @code
 * auto ball = cache.get_or_create_mesh(assets::mesh_generators::sphere{});
 * auto slab = cache.get_or_create_mesh(assets::mesh_generators::box{.width = 4.0f, .depth = 0.2f});
 * @endcode
 *
 * Every generator emits indexed @ref vertex_position_uv_normal_tangent
 * records, the tangents derived by @ref generate_tangents, with each
 * triangle wound counter-clockwise seen from its front (outer) side. The
 * geometry is built around the origin; a node's transform places, turns
 * and scales it. Generating is plain CPU work with no device or cache
 * behind it, so it runs on any thread.
 */
namespace assets::mesh_generators
{
    /**
     * @brief A box centred on the origin, @ref width along X, @ref height
     *        along Y and @ref depth along Z.
     *
     * Each of the six faces is a grid of segments with its own [0, 1] UVs
     * and a constant outward normal, so the box can be textured and
     * normal-mapped. A segment count below 1 counts as 1.
     */
    struct box
    {
        float width{1.0f};
        float height{1.0f};
        float depth{1.0f};
        unsigned int width_segments{1};
        unsigned int height_segments{1};
        unsigned int depth_segments{1};
    };

    /**
     * @brief A capsule along Y, centred on the origin: a cylindrical body of
     *        @ref length (spanning [-length/2, +length/2]) capped by two
     *        hemispheres of @ref radius.
     *
     * One seamless latitude/longitude grid covers both caps and the body,
     * which share their boundary rings, so the surface has no cracks.
     * @ref cap_segments rings sweep each hemisphere and @ref radial_segments
     * columns go around the axis; the body is cut along the axis into
     * @ref radial_segments segments as well. UVs wrap around the axis and run
     * along it in proportion to the surface distance.
     */
    struct capsule
    {
        float radius{0.5f};
        float length{1.0f};
        unsigned int cap_segments{8};
        unsigned int radial_segments{16};
    };

    /**
     * @brief A flat disc in the XY plane facing +Z: a fan of @ref segments
     *        triangles around a centre vertex, sweeping @ref theta_length
     *        radians from @ref theta_start (a partial sweep is a sector).
     *        Fewer than 3 segments count as 3.
     */
    struct circle
    {
        float radius{1.0f};
        unsigned int segments{32};
        float theta_start{0.0f};
        float theta_length{core::math::two_pi};
    };

    /**
     * @brief A cube's six faces, each a grid of @ref subdivisions x
     *        @ref subdivisions quads, pushed out onto the unit sphere.
     *
     * Unlike a latitude/longitude @ref sphere it has no poles to pinch: six
     * face patches meet at the cube's corners with bounded distortion (about
     * 1.4x there). Each face takes the full [0, 1] UV square in the cube-map
     * convention (u to the right seen from outside, v down), so it pairs
     * naturally with cube-map textures, which it samples by its normal.
     */
    struct cubed_sphere
    {
        unsigned int subdivisions{32};
    };

    /**
     * @brief A cylinder, or a truncated cone, along Y and centred on the
     *        origin, spanning [-height/2, +height/2].
     *
     * The side wall is a grid of @ref radial_segments columns by
     * @ref height_segments rows with smooth normals following the slope
     * between the two radii. Unless @ref open_ended, each end is closed by a
     * flat fan; an end whose radius is 0 gets none, so a zero
     * @ref radius_top makes a cone (see @ref cone).
     */
    struct cylinder
    {
        float radius_top{1.0f};
        float radius_bottom{1.0f};
        float height{1.0f};
        unsigned int radial_segments{32};
        unsigned int height_segments{1};
        bool open_ended{false};
    };

    /** @brief A cone along Y: a @ref cylinder whose top radius is 0 and bottom radius @p radius. */
    cylinder cone(float radius = 1.0f,
                  float height = 1.0f,
                  unsigned int radial_segments = 32,
                  unsigned int height_segments = 1,
                  bool open_ended = false);

    /**
     * @brief A flat rectangle in the XY plane facing +Z, centred on the
     *        origin: @ref width along X, @ref height along Y, a grid of
     *        @ref width_segments x @ref height_segments quads with UVs
     *        spanning [0, 1] across it.
     */
    struct plane
    {
        float width{1.0f};
        float height{1.0f};
        unsigned int width_segments{1};
        unsigned int height_segments{1};
    };

    /**
     * @brief A polyhedron inscribed in the sphere of @ref radius, from a base
     *        mesh.
     *
     * The base is a list of vertex positions and a flat list of triangle
     * indices into it, each triangle wound counter-clockwise seen from
     * outside. Every base triangle is cut into @c 2^detail segments per
     * edge, each resulting vertex is projected onto the sphere, and it takes
     * the sphere's normal and a spherical UV. Every triangle has vertices of
     * its own, so a UV fix-up across the texture seam or at a pole stays
     * within that triangle. The platonic solids below build on it.
     */
    struct polyhedron
    {
        std::vector<core::math::vec3> base_vertices;
        std::vector<uint32_t> base_indices;
        float radius{1.0f};
        unsigned int detail{0};
    };

    /** @brief A regular tetrahedron (4 vertices, 4 triangles) as a @ref polyhedron. */
    polyhedron tetrahedron(float radius = 1.0f, unsigned int detail = 0);

    /** @brief A regular octahedron (6 vertices, 8 triangles) as a @ref polyhedron. */
    polyhedron octahedron(float radius = 1.0f, unsigned int detail = 0);

    /**
     * @brief A regular dodecahedron (20 vertices, 12 pentagons of three
     *        triangles each) as a @ref polyhedron.
     */
    polyhedron dodecahedron(float radius = 1.0f, unsigned int detail = 0);

    /** @brief A regular icosahedron (12 vertices, 20 triangles) as a @ref polyhedron. */
    polyhedron icosahedron(float radius = 1.0f, unsigned int detail = 0);

    /**
     * @brief A flat annulus in the XY plane facing +Z, between
     *        @ref inner_radius and @ref outer_radius: @ref phi_segments rings
     *        of @ref theta_segments quads, sweeping @ref theta_length radians
     *        from @ref theta_start. Fewer than 3 theta segments count as 3,
     *        fewer than 1 phi segment as 1.
     */
    struct ring
    {
        float inner_radius{0.5f};
        float outer_radius{1.0f};
        unsigned int theta_segments{32};
        unsigned int phi_segments{1};
        float theta_start{0.0f};
        float theta_length{core::math::two_pi};
    };

    /**
     * @brief The unit sphere as a latitude/longitude grid: @ref stacks rows
     *        from the +Z pole to the -Z pole by @ref slices columns around Z,
     *        two triangles per cell.
     *
     * The normal is the position. Texturing is well-behaved away from the
     * poles; near them the UVs pinch while the normals stay smooth (a
     * @ref cubed_sphere has no poles).
     */
    struct sphere
    {
        unsigned int stacks{64};
        unsigned int slices{128};
    };

    /**
     * @brief A torus around Z in the XY plane: a tube of radius @ref tube
     *        whose centre line is the circle of @ref radius.
     *
     * @ref radial_segments cut the tube's cross-section and
     * @ref tubular_segments the main ring, which sweeps @ref arc radians (a
     * full ring at @c two_pi).
     */
    struct torus
    {
        float radius{1.0f};
        float tube{0.4f};
        unsigned int radial_segments{12};
        unsigned int tubular_segments{48};
        float arc{core::math::two_pi};
    };

    /** @brief Builds the geometry @p shape describes. */
    mesh_data generate(const box& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const capsule& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const circle& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const cubed_sphere& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const cylinder& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const plane& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const polyhedron& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const ring& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const sphere& shape);
    /** @copydoc generate(const box&) */
    mesh_data generate(const torus& shape);

    /**
     * @brief The structural key a cache holds @p shape's geometry under: the
     *        generator's name, the parameters and the vertex record, e.g.
     *        @c "sphere:64x128:position_uv_normal_tangent".
     *
     * Numbers are spelled by @ref cache_key_number, except a
     * @ref polyhedron's radius (@c std::to_string, six decimals); a
     * @ref polyhedron keys its base tables by an FNV-1a digest, so two
     * solids of one radius and detail stay apart. A @ref box keys its
     * segment counts after raising them to 1.
     */
    std::string cache_key(const box& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const capsule& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const circle& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const cubed_sphere& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const cylinder& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const plane& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const polyhedron& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const ring& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const sphere& shape);
    /** @copydoc cache_key(const box&) */
    std::string cache_key(const torus& shape);
} // namespace assets::mesh_generators

namespace assets
{
    /**
     * @brief A description one of the @ref mesh_generators builds: one that
     *        @c mesh_generators::generate turns into geometry and
     *        @c mesh_generators::cache_key names.
     */
    template<typename Shape>
    concept mesh_shape = requires(const Shape& shape) {
        { mesh_generators::generate(shape) } -> std::same_as<mesh_data>;
        { mesh_generators::cache_key(shape) } -> std::same_as<std::string>;
    };
} // namespace assets
