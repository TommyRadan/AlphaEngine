// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gltf_importer.hpp
 * @brief glTF 2.0 importer: turns a .gltf / .glb file into a CPU
 *        description — geometry per primitive (static and skinned),
 *        material descriptions, texture and image references with their
 *        decoded pixels, the node hierarchy, skins and animations.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <assets/color.hpp>
#include <assets/image.hpp>
#include <assets/mesh_data.hpp>
#include <core/math/curve.hpp>
#include <core/math/math.hpp>

namespace assets
{
    /** @brief "No index": a primitive without a material, a node without a parent, a slot without a texture. */
    inline constexpr std::size_t gltf_npos = static_cast<std::size_t>(-1);

    /** @brief Knobs for @ref import_gltf. */
    struct gltf_import_options
    {
        // Colour space the base-colour (albedo) images were authored in.
        // glTF mandates sRGB; override only for an asset that breaks the
        // spec. Emissive maps are always sRGB and the normal / metallic-
        // roughness / occlusion data maps always linear.
        color_space base_color_space{color_space::srgb};

        // Derive tangents (via @ref generate_tangents) for primitives that
        // ship none. Off, such primitives get a constant placeholder
        // tangent, which is only acceptable when no normal map reads it.
        bool generate_missing_tangents{true};
    };

    /**
     * @brief One TRIANGLES primitive of a glTF mesh as CPU geometry: a
     *        @ref vertex_position_uv_normal_tangent record per vertex (or,
     *        for a primitive with JOINTS_0 / WEIGHTS_0, a
     *        @ref vertex_position_uv_normal_tangent_skin one) and 32-bit
     *        indices.
     */
    struct gltf_primitive
    {
        // "gltf:<canonical path>#mesh<i>/prim<j>": names the geometry by the
        // file's canonical identity and the primitive's mesh / primitive
        // indices, so the same file loaded twice (or through two spellings)
        // yields the same key and an upload keyed on it is shared.
        std::string key;

        // The records and indices. Its bounds are the POSITION accessor's
        // min / max when the file supplies them (the spec requires it);
        // otherwise they are left unset for @c mesh_data::compute_bounds.
        mesh_data geometry;

        // Whether @ref geometry carries joint indices and weights (the
        // skinned record). A node with a skin draws such a primitive
        // skinned; anywhere else it draws rigid, in its bind pose.
        bool skinned{false};

        // Index into @ref gltf_document::materials, or @ref gltf_npos when
        // the primitive names none (glTF's default material applies).
        std::size_t material{gltf_npos};
    };

    /** @brief One glTF node: a name, a local TRS pose and its links. */
    struct gltf_node
    {
        std::string name;

        // Index into @ref gltf_document::nodes, or @ref gltf_npos for a root.
        std::size_t parent{gltf_npos};

        // Indices into @ref gltf_document::nodes, in file order.
        std::vector<std::size_t> children;

        // Local pose in the file's own coordinate system (+Y up, right-
        // handed); a node authored as a matrix is decomposed into TRS.
        core::math::vec3 translation{0.0f, 0.0f, 0.0f};
        core::math::quat rotation{};
        core::math::vec3 scale{1.0f, 1.0f, 1.0f};

        // Indices into @ref gltf_document::primitives drawn at this node.
        std::vector<std::size_t> primitives;

        // Index into @ref gltf_document::skins deforming this node's skinned
        // primitives, or @ref gltf_npos.
        std::size_t skin{gltf_npos};
    };

    /**
     * @brief An image a texture samples: where it comes from and, once
     *        decoded, its RGBA8 pixels.
     *
     * An image with a file of its own names that file (resolved against the
     * glTF file's directory), so whoever uploads it can share the upload
     * with every other loader of the file. One embedded in a GLB chunk or a
     * data URI has no file; @ref key names it instead, by the model's
     * identity plus the image index (@c "gltf:<canonical path>#image<i>").
     */
    struct gltf_image
    {
        // The image's own file, or empty for an embedded image.
        std::filesystem::path file;

        // The identity of an embedded image; empty for a file.
        std::string key;

        // The decoded pixels: set for every image a texture references,
        // unless it could not be decoded (warned about).
        std::optional<image> pixels;
    };

    /** @brief One glTF texture: the image it samples and the colour space its materials sample it in. */
    struct gltf_texture
    {
        // Index into @ref gltf_document::images, or @ref gltf_npos when the
        // texture has no image source the importer reads (warned about).
        std::size_t image{gltf_npos};

        // Colour for a texture sampled as base colour (in
        // @ref gltf_import_options::base_color_space) or emissive (sRGB),
        // linear for a data map; colour wins when a texture is, unusually,
        // sampled both ways.
        color_space space{color_space::srgb};
    };

    /**
     * @brief Everything the importer knows about one glTF material: its
     *        factors and the textures of its slots.
     *
     * The factors are glTF's (linear, unquantised). The slots name entries
     * of @ref gltf_document::textures; each is sampled in the colour space
     * the slot implies (base colour in @ref base_color_space, emissive
     * sRGB, the rest linear), which may differ from the texture's own
     * @ref gltf_texture::space when a texture is sampled both ways. The
     * packed metallic-roughness texture is glTF's R occlusion / G
     * roughness / B metallic layout; its R channel is the occlusion only
     * when @ref occlusion_texture samples the same image.
     */
    struct gltf_material
    {
        std::string name;

        core::math::vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic_factor{1.0f};
        float roughness_factor{1.0f};
        core::math::vec3 emissive_factor{0.0f, 0.0f, 0.0f};

        // KHR_materials_emissive_strength multiplier; 1 when absent.
        float emissive_strength{1.0f};

        // Colour space @ref base_color_texture is sampled in (from the
        // import options); the other slots' spaces are fixed by the spec.
        color_space base_color_space{color_space::srgb};

        // Indices into @ref gltf_document::textures, or @ref gltf_npos for
        // an empty slot.
        std::size_t base_color_texture{gltf_npos};
        std::size_t metallic_roughness_texture{gltf_npos};
        std::size_t normal_texture{gltf_npos};
        std::size_t occlusion_texture{gltf_npos};
        std::size_t emissive_texture{gltf_npos};

        // occlusionTexture.strength; 1 without an occlusion texture.
        float occlusion_strength{1.0f};
    };

    /**
     * @brief One glTF skin: the joints that deform a mesh, in the order its
     *        vertices' joint indices count them, and their inverse bind
     *        matrices.
     */
    struct gltf_skin
    {
        std::string name;

        // Palette entry -> index into @ref gltf_document::nodes, or
        // @ref gltf_npos for an entry that names no node.
        std::vector<std::size_t> joints;

        // One per palette entry when the file supplies them (the spec
        // defaults them to identity), possibly fewer.
        std::vector<core::math::mat4> inverse_bind_matrices;
    };

    /**
     * @brief The keyframed channels one glTF animation drives on one node.
     *        A channel with no keys is not animated.
     */
    struct gltf_node_track
    {
        // Index into @ref gltf_document::nodes.
        std::size_t node{gltf_npos};

        core::math::curve<core::math::vec3> translation;
        core::math::curve<core::math::quat> rotation;
        core::math::curve<core::math::vec3> scale;
    };

    /**
     * @brief One glTF animation: translation, rotation and scale channels
     *        with their step / linear / cubic-spline sampling, grouped into
     *        one track per node. Morph-target weight channels are not
     *        imported. Times are in seconds.
     */
    struct gltf_animation
    {
        std::string name;
        std::vector<gltf_node_track> tracks;
    };

    /**
     * @brief A glTF file as CPU data, from @ref import_gltf.
     *
     * Every vector is index-aligned with the file's own array except
     * @ref primitives, which lists only the primitives that imported
     * (TRIANGLES ones whose attributes could be read; others are skipped
     * with a warning and never referenced from a node).
     */
    struct gltf_document
    {
        // The path the file was imported from, for log lines.
        std::filesystem::path path;

        // "gltf:<canonical path>": the stable identity every key of the
        // file hangs off.
        std::string identity;

        std::vector<gltf_node> nodes;

        // The nodes to instantiate: the default scene's roots (or the first
        // scene's, or every parentless node when the file declares no scene).
        std::vector<std::size_t> root_nodes;

        std::vector<gltf_primitive> primitives;
        std::vector<gltf_material> materials;
        std::vector<gltf_texture> textures;
        std::vector<gltf_image> images;
        std::vector<gltf_skin> skins;
        std::vector<gltf_animation> animations;
    };

    /**
     * @brief Imports the .gltf / .glb at @p path, read through the VFS.
     *
     * Handles external buffer and image files (resolved relative to the glTF
     * file), GLB binary chunks and base64 data URIs. Each TRIANGLES primitive
     * becomes @ref vertex_position_uv_normal_tangent geometry — flat normals
     * are generated when the file has none (its tangents are then ignored,
     * as the spec requires), tangents are taken from the file or derived; a
     * primitive with JOINTS_0 / WEIGHTS_0 becomes
     * @ref vertex_position_uv_normal_tangent_skin geometry, its weights
     * renormalised to sum to 1. Every image a texture references is decoded;
     * an undecodable one is warned about and left without pixels. Touches
     * nothing but the file system and the CPU, so it is safe on any thread:
     * the renderer's asynchronous loads run it on the worker pool. Throws
     * @c std::runtime_error (after logging) when the file cannot be parsed,
     * its buffers cannot be loaded, or it fails validation.
     */
    gltf_document import_gltf(const std::filesystem::path& path, const gltf_import_options& options = {});
} // namespace assets
