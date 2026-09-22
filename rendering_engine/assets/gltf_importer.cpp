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

#include <rendering_engine/assets/gltf_importer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/mesh/tangent.hpp>
#include <rendering_engine/mesh/vertex.hpp>

// cgltf is a single-header library; this is its one implementation unit
// (the same arrangement as stb_image in util/image.cpp).
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace rendering_engine
{
    namespace
    {
        namespace math = core::math;

        using base_vertex = vertex_position_uv_normal;
        using tangent_vertex = vertex_position_uv_normal_tangent;

        // Thrown by the geometry builder, from inside the cache's builder
        // callback, so a primitive that cannot be imported is skipped without
        // the cache ever inserting an entry for it.
        struct primitive_import_error : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        struct cgltf_data_deleter
        {
            void operator()(cgltf_data* data) const noexcept
            {
                cgltf_free(data);
            }
        };
        using cgltf_data_ptr = std::unique_ptr<cgltf_data, cgltf_data_deleter>;

        const char* result_name(cgltf_result result)
        {
            switch (result)
            {
            case cgltf_result_success:
                return "success";
            case cgltf_result_data_too_short:
                return "data too short";
            case cgltf_result_unknown_format:
                return "unknown format";
            case cgltf_result_invalid_json:
                return "invalid JSON";
            case cgltf_result_invalid_gltf:
                return "invalid glTF";
            case cgltf_result_invalid_options:
                return "invalid options";
            case cgltf_result_file_not_found:
                return "file not found";
            case cgltf_result_io_error:
                return "I/O error";
            case cgltf_result_out_of_memory:
                return "out of memory";
            case cgltf_result_legacy_gltf:
                return "legacy (1.0) glTF";
            default:
                return "unknown error";
            }
        }

        const char* topology_name(cgltf_primitive_type type)
        {
            switch (type)
            {
            case cgltf_primitive_type_points:
                return "POINTS";
            case cgltf_primitive_type_lines:
                return "LINES";
            case cgltf_primitive_type_line_loop:
                return "LINE_LOOP";
            case cgltf_primitive_type_line_strip:
                return "LINE_STRIP";
            case cgltf_primitive_type_triangles:
                return "TRIANGLES";
            case cgltf_primitive_type_triangle_strip:
                return "TRIANGLE_STRIP";
            case cgltf_primitive_type_triangle_fan:
                return "TRIANGLE_FAN";
            default:
                return "unknown";
            }
        }

        // The stable identity every cache key for this file hangs off:
        // "gltf:<canonical path>", so the same file reached through two
        // spellings shares one set of uploads.
        std::string file_identity(const std::filesystem::path& path)
        {
            std::error_code error;
            std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
            if (error)
            {
                canonical = std::filesystem::absolute(path, error);
            }
            if (error)
            {
                canonical = path;
            }
            return "gltf:" + canonical.lexically_normal().generic_string();
        }

        // Decodes a base64 payload (as found after the comma of a data URI).
        // Returns an empty vector on any character outside the alphabet.
        std::vector<uint8_t> decode_base64(const char* text)
        {
            std::vector<uint8_t> out;
            uint32_t buffer = 0;
            int bits = 0;
            for (const char* p = text; *p != '\0' && *p != '='; ++p)
            {
                const char c = *p;
                int value = -1;
                if (c >= 'A' && c <= 'Z')
                {
                    value = c - 'A';
                }
                else if (c >= 'a' && c <= 'z')
                {
                    value = c - 'a' + 26;
                }
                else if (c >= '0' && c <= '9')
                {
                    value = c - '0' + 52;
                }
                else if (c == '+')
                {
                    value = 62;
                }
                else if (c == '/')
                {
                    value = 63;
                }
                else
                {
                    return {};
                }
                buffer = (buffer << 6) | static_cast<uint32_t>(value);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xffu));
                }
            }
            return out;
        }

        // A data URI's base64 payload, or nullptr when @p uri is not one.
        const char* data_uri_payload(const char* uri)
        {
            if (uri == nullptr || std::strncmp(uri, "data:", 5) != 0)
            {
                return nullptr;
            }
            const char* comma = std::strchr(uri, ',');
            if (comma == nullptr || comma - uri < 7 || std::strncmp(comma - 7, ";base64", 7) != 0)
            {
                return nullptr;
            }
            return comma + 1;
        }

        bool is_file_uri(const char* uri)
        {
            return uri != nullptr && std::strncmp(uri, "data:", 5) != 0 && std::strstr(uri, "://") == nullptr;
        }

        // Resolves a relative, percent-encoded glTF URI against the file's
        // directory.
        std::filesystem::path resolve_file_uri(const std::filesystem::path& base_dir, const char* uri)
        {
            std::string decoded = uri;
            decoded.resize(cgltf_decode_uri(decoded.data()));
            return base_dir / std::filesystem::path{decoded};
        }

        // Unpacks @p accessor into floats, converting normalized integer
        // components, and checks it carries @p components values per element.
        bool unpack_floats(const cgltf_accessor& accessor,
                           std::size_t components,
                           std::vector<float>& out,
                           const char* attribute,
                           const std::string& label)
        {
            if (cgltf_num_components(accessor.type) != components)
            {
                LOG_WRN("gltf: %s attribute of %s has %zu components, expected %zu",
                        attribute,
                        label.c_str(),
                        cgltf_num_components(accessor.type),
                        components);
                return false;
            }
            out.resize(accessor.count * components);
            const cgltf_size unpacked = cgltf_accessor_unpack_floats(&accessor, out.data(), out.size());
            if (unpacked != out.size())
            {
                LOG_WRN("gltf: could not read the %s attribute of %s", attribute, label.c_str());
                return false;
            }
            return true;
        }

        math::vec3 read_vec3(const std::vector<float>& values, std::size_t index)
        {
            return math::vec3{values[index * 3], values[index * 3 + 1], values[index * 3 + 2]};
        }

        math::vec2 read_vec2(const std::vector<float>& values, std::size_t index)
        {
            return math::vec2{values[index * 2], values[index * 2 + 1]};
        }

        math::vec4 read_vec4(const std::vector<float>& values, std::size_t index)
        {
            return math::vec4{values[index * 4], values[index * 4 + 1], values[index * 4 + 2], values[index * 4 + 3]};
        }

        // The file's object-space box for a POSITION accessor, when it
        // supplies min / max (the spec requires them). Forwarded as
        // mesh_data::bounds so the cached asset carries the authoritative box;
        // a file that omits them leaves it unset and the cache scans the
        // positions at upload instead.
        std::optional<math::aabb> accessor_bounds(const cgltf_accessor& position)
        {
            if (!position.has_min || !position.has_max)
            {
                return std::nullopt;
            }
            return math::aabb{math::vec3{position.min[0], position.min[1], position.min[2]},
                              math::vec3{position.max[0], position.max[1], position.max[2]}};
        }

        // A tangent for records that carry none and cannot derive one. Only
        // acceptable when nothing samples a normal map through it.
        std::vector<tangent_vertex> with_placeholder_tangents(const std::vector<base_vertex>& vertices)
        {
            std::vector<tangent_vertex> out;
            out.reserve(vertices.size());
            for (const base_vertex& v : vertices)
            {
                out.push_back(tangent_vertex{v.pos, v.uv, v.normal, math::vec4{1.0f, 0.0f, 0.0f, 1.0f}});
            }
            return out;
        }

        // Builds the vertex_position_uv_normal_tangent records and 32-bit
        // indices of one TRIANGLES primitive. Throws primitive_import_error
        // (after warning) when the attributes cannot be read.
        mesh_data
        build_geometry(const cgltf_primitive& primitive, const gltf_import_options& options, const std::string& label)
        {
            const cgltf_accessor* position = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
            const cgltf_accessor* normal = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* texcoord = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* tangent = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);

            std::vector<float> positions;
            if (position == nullptr || position->count == 0 ||
                !unpack_floats(*position, 3, positions, "POSITION", label))
            {
                throw primitive_import_error{"missing POSITION"};
            }
            const std::size_t vertex_count = position->count;

            // The optional channels: a mismatched or unreadable one is
            // dropped (warned about) rather than failing the primitive.
            std::vector<float> normals;
            const bool has_normals = normal != nullptr && normal->count == vertex_count &&
                                     unpack_floats(*normal, 3, normals, "NORMAL", label);
            std::vector<float> uvs;
            const bool has_uvs = texcoord != nullptr && texcoord->count == vertex_count &&
                                 unpack_floats(*texcoord, 2, uvs, "TEXCOORD_0", label);
            std::vector<float> tangents;
            const bool has_tangents = tangent != nullptr && tangent->count == vertex_count &&
                                      unpack_floats(*tangent, 4, tangents, "TANGENT", label);

            // Indices: widen u8 / u16 / u32 to u32, or number the vertices for
            // a non-indexed primitive.
            std::vector<uint32_t> indices;
            if (primitive.indices != nullptr)
            {
                indices.resize(primitive.indices->count);
                if (cgltf_accessor_unpack_indices(
                        primitive.indices, indices.data(), sizeof(uint32_t), indices.size()) != indices.size())
                {
                    LOG_WRN("gltf: could not read the indices of %s", label.c_str());
                    throw primitive_import_error{"unreadable indices"};
                }
            }
            else
            {
                indices.resize(vertex_count);
                std::iota(indices.begin(), indices.end(), 0u);
            }
            if (indices.size() % 3 != 0)
            {
                LOG_WRN("gltf: %s has %zu indices, not a multiple of 3; trailing vertices dropped",
                        label.c_str(),
                        indices.size());
                indices.resize(indices.size() - indices.size() % 3);
            }
            for (const uint32_t index : indices)
            {
                if (index >= vertex_count)
                {
                    LOG_WRN("gltf: %s indexes vertex %u of %zu", label.c_str(), index, vertex_count);
                    throw primitive_import_error{"index out of range"};
                }
            }
            if (indices.empty())
            {
                LOG_WRN("gltf: %s has no triangles", label.c_str());
                throw primitive_import_error{"no triangles"};
            }

            std::vector<base_vertex> vertices;
            if (has_normals)
            {
                vertices.resize(vertex_count);
                for (std::size_t v = 0; v < vertex_count; ++v)
                {
                    vertices[v].pos = read_vec3(positions, v);
                    vertices[v].uv = has_uvs ? read_vec2(uvs, v) : math::vec2{0.0f, 0.0f};
                    vertices[v].normal = read_vec3(normals, v);
                }

                if (has_tangents)
                {
                    std::vector<tangent_vertex> records;
                    records.reserve(vertex_count);
                    for (std::size_t v = 0; v < vertex_count; ++v)
                    {
                        records.push_back(tangent_vertex{
                            vertices[v].pos, vertices[v].uv, vertices[v].normal, read_vec4(tangents, v)});
                    }
                    return mesh_data::from_vertices(records, std::move(indices));
                }
            }
            else
            {
                // No normals: the spec says to shade flat, ignoring any
                // supplied tangents. Every triangle gets its own three
                // vertices carrying the face normal.
                vertices.reserve(indices.size());
                for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
                {
                    const math::vec3 a = read_vec3(positions, indices[t]);
                    const math::vec3 b = read_vec3(positions, indices[t + 1]);
                    const math::vec3 c = read_vec3(positions, indices[t + 2]);
                    math::vec3 face_normal = math::cross(b - a, c - a);
                    if (math::length(face_normal) > 0.0f)
                    {
                        face_normal = math::normalize(face_normal);
                    }
                    else
                    {
                        face_normal = math::vec3{0.0f, 0.0f, 1.0f};
                    }
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const uint32_t source = indices[t + corner];
                        vertices.push_back(base_vertex{read_vec3(positions, source),
                                                       has_uvs ? read_vec2(uvs, source) : math::vec2{0.0f, 0.0f},
                                                       face_normal});
                    }
                }
                indices.resize(vertices.size());
                std::iota(indices.begin(), indices.end(), 0u);
            }

            if (options.generate_missing_tangents)
            {
                std::vector<tangent_vertex> records = generate_tangents(vertices, indices);
                return mesh_data::from_vertices(records, std::move(indices));
            }
            return mesh_data::from_vertices(with_placeholder_tangents(vertices), std::move(indices));
        }

        // Per-load state shared by the import stages: the parsed file, where
        // its relative URIs resolve, the decoded images (lazily, at most once
        // each) and the metallic / roughness maps split off them.
        struct import_context
        {
            const cgltf_data& data;
            const std::filesystem::path& path;
            std::string path_text;
            std::filesystem::path base_dir;
            std::string identity;
            asset_cache& cache;
            const gltf_import_options& options;

            // Index-aligned with data.images; an entry is decoded on first
            // use and left empty (with the failure remembered) when it
            // cannot be.
            std::vector<std::optional<util::image>> images;
            std::vector<bool> image_failed;

            struct split_maps
            {
                util::image metallic;
                util::image roughness;
            };
            std::unordered_map<std::size_t, split_maps> orm_splits;

            import_context(const cgltf_data& in_data,
                           const std::filesystem::path& in_path,
                           asset_cache& in_cache,
                           const gltf_import_options& in_options)
                : data{in_data}, path{in_path}, path_text{in_path.string()}, base_dir{in_path.parent_path()},
                  identity{file_identity(in_path)}, cache{in_cache}, options{in_options}, images(in_data.images_count),
                  image_failed(in_data.images_count, false)
            {
            }

            // The file name for log lines.
            const char* path_name() const
            {
                return path_text.c_str();
            }

            // The decoded pixels of image @p index, or nullptr when it cannot
            // be decoded (warned about once).
            const util::image* decoded_image(std::size_t index)
            {
                if (index >= images.size() || image_failed[index])
                {
                    return nullptr;
                }
                if (images[index].has_value())
                {
                    return &*images[index];
                }
                images[index] = decode_image(data.images[index], index);
                if (!images[index].has_value())
                {
                    image_failed[index] = true;
                    return nullptr;
                }
                return &*images[index];
            }

            std::optional<util::image> decode_image(const cgltf_image& image, std::size_t index)
            {
                try
                {
                    if (image.buffer_view != nullptr)
                    {
                        const uint8_t* bytes = cgltf_buffer_view_data(image.buffer_view);
                        if (bytes == nullptr)
                        {
                            LOG_WRN("gltf: image %zu of '%s' has no buffer data", index, path_name());
                            return std::nullopt;
                        }
                        return util::image{bytes, image.buffer_view->size};
                    }
                    if (const char* payload = data_uri_payload(image.uri); payload != nullptr)
                    {
                        const std::vector<uint8_t> bytes = decode_base64(payload);
                        if (bytes.empty())
                        {
                            LOG_WRN("gltf: image %zu of '%s' has a malformed data URI", index, path_name());
                            return std::nullopt;
                        }
                        return util::image{bytes.data(), bytes.size()};
                    }
                    if (!is_file_uri(image.uri))
                    {
                        LOG_WRN("gltf: image %zu of '%s' has an unsupported URI (%s)",
                                index,
                                path_name(),
                                image.uri != nullptr ? image.uri : "none");
                        return std::nullopt;
                    }
                    return util::image{resolve_file_uri(base_dir, image.uri).string()};
                }
                catch (const std::runtime_error&)
                {
                    // util::image already logged the decoder's reason.
                    LOG_WRN(
                        "gltf: image %zu of '%s' could not be decoded; maps using it are skipped", index, path_name());
                    return std::nullopt;
                }
            }

            // The map a texture view samples, decoded, or nullptr when the
            // view names no texture or its image failed to decode.
            const util::image* map_image(const cgltf_texture_view& view, const char* slot)
            {
                if (view.texture == nullptr || view.texture->image == nullptr)
                {
                    return nullptr;
                }
                if (view.texcoord != 0)
                {
                    LOG_WRN("gltf: %s of '%s' samples TEXCOORD_%d; only TEXCOORD_0 is imported",
                            slot,
                            path_name(),
                            view.texcoord);
                }
                if (view.has_transform)
                {
                    LOG_WRN("gltf: %s of '%s' uses KHR_texture_transform, which is ignored", slot, path_name());
                }
                return decoded_image(cgltf_image_index(&data, view.texture->image));
            }

            // glTF packs roughness in G and metalness in B of one texture;
            // standard_material samples .r of two separate maps, so split the
            // packed image into two single-channel-in-R images (each channel
            // replicated across RGB so the maps read sensibly on their own).
            // Cached per image so materials sharing one ORM texture split it
            // once. Interim until the material takes a packed ORM map.
            const split_maps* orm_maps(const cgltf_texture_view& view)
            {
                const util::image* packed = map_image(view, "metallicRoughnessTexture");
                if (packed == nullptr)
                {
                    return nullptr;
                }
                const std::size_t index = cgltf_image_index(&data, view.texture->image);
                if (auto it = orm_splits.find(index); it != orm_splits.end())
                {
                    return &it->second;
                }

                const uint32_t width = packed->get_width();
                const uint32_t height = packed->get_height();
                split_maps split{util::image{width, height, util::color{0, 0, 0, 255}},
                                 util::image{width, height, util::color{0, 0, 0, 255}}};
                for (uint32_t y = 0; y < height; ++y)
                {
                    for (uint32_t x = 0; x < width; ++x)
                    {
                        const util::color texel = packed->get_pixel(x, y);
                        split.metallic.set_pixel(x, y, util::color{texel.b, texel.b, texel.b, 255});
                        split.roughness.set_pixel(x, y, util::color{texel.g, texel.g, texel.g, 255});
                    }
                }
                return &orm_splits.emplace(index, std::move(split)).first->second;
            }
        };

        // Stage 1: every TRIANGLES primitive through the cache. Fills
        // @p primitives_by_mesh (glTF mesh index -> model primitive indices)
        // for the node stage.
        void import_primitives(import_context& ctx,
                               gltf_model& model,
                               std::vector<std::vector<std::size_t>>& primitives_by_mesh)
        {
            primitives_by_mesh.assign(ctx.data.meshes_count, {});
            for (std::size_t i = 0; i < ctx.data.meshes_count; ++i)
            {
                const cgltf_mesh& mesh = ctx.data.meshes[i];
                for (std::size_t j = 0; j < mesh.primitives_count; ++j)
                {
                    const cgltf_primitive& primitive = mesh.primitives[j];
                    const std::string label = "mesh " + std::to_string(i) + " primitive " + std::to_string(j) +
                                              " of '" + ctx.path.string() + "'";

                    if (primitive.type != cgltf_primitive_type_triangles)
                    {
                        LOG_WRN("gltf: %s uses %s topology; only TRIANGLES are imported, skipped",
                                label.c_str(),
                                topology_name(primitive.type));
                        continue;
                    }
                    if (primitive.has_draco_mesh_compression)
                    {
                        LOG_WRN("gltf: %s is Draco-compressed, which is not supported; skipped", label.c_str());
                        continue;
                    }
                    const cgltf_accessor* position = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
                    if (position == nullptr)
                    {
                        LOG_WRN("gltf: %s has no POSITION attribute; skipped", label.c_str());
                        continue;
                    }

                    // The builder only runs on a cache miss; a hit shares the
                    // upload of an earlier load of this file.
                    const std::string key = ctx.identity + "#mesh" + std::to_string(i) + "/prim" + std::to_string(j);
                    std::shared_ptr<mesh_asset> asset;
                    try
                    {
                        asset = ctx.cache.get_or_create_mesh(key,
                                                             [&]
                                                             {
                                                                 mesh_data data =
                                                                     build_geometry(primitive, ctx.options, label);
                                                                 data.bounds = accessor_bounds(*position);
                                                                 return data;
                                                             });
                    }
                    catch (const primitive_import_error&)
                    {
                        continue; // already warned; nothing was cached
                    }

                    gltf_mesh_primitive imported;
                    imported.mesh = std::move(asset);
                    imported.material_index =
                        primitive.material != nullptr ? cgltf_material_index(&ctx.data, primitive.material) : gltf_npos;
                    primitives_by_mesh[i].push_back(model.primitives.size());
                    model.primitives.push_back(std::move(imported));
                }
            }
        }

        // Stage 2: one cache texture per glTF texture, in the colour space
        // the materials sampling it expect.
        void import_textures(import_context& ctx, gltf_model& model)
        {
            // Usage bits per texture, gathered from the materials: colour
            // (base colour / emissive) wins over data when a texture is,
            // unusually, used both ways.
            constexpr uint8_t used_as_base_color = 1;
            constexpr uint8_t used_as_emissive = 2;
            constexpr uint8_t used_as_data = 4;
            std::vector<uint8_t> usage(ctx.data.textures_count, 0);
            const auto mark = [&](const cgltf_texture_view& view, uint8_t bit)
            {
                if (view.texture != nullptr)
                {
                    usage[cgltf_texture_index(&ctx.data, view.texture)] |= bit;
                }
            };
            for (std::size_t m = 0; m < ctx.data.materials_count; ++m)
            {
                const cgltf_material& material = ctx.data.materials[m];
                if (material.has_pbr_metallic_roughness)
                {
                    mark(material.pbr_metallic_roughness.base_color_texture, used_as_base_color);
                    mark(material.pbr_metallic_roughness.metallic_roughness_texture, used_as_data);
                }
                mark(material.emissive_texture, used_as_emissive);
                mark(material.normal_texture, used_as_data);
                mark(material.occlusion_texture, used_as_data);
            }

            model.textures.assign(ctx.data.textures_count, nullptr);
            for (std::size_t t = 0; t < ctx.data.textures_count; ++t)
            {
                const cgltf_texture& texture = ctx.data.textures[t];
                if (texture.image == nullptr)
                {
                    LOG_WRN(
                        "gltf: texture %zu of '%s' has no image source (only plain PNG / JPEG images are supported)",
                        t,
                        ctx.path_name());
                    continue;
                }

                gpu::color_space space = gpu::color_space::srgb;
                if ((usage[t] & used_as_base_color) != 0)
                {
                    space = ctx.options.base_color_space;
                }
                else if ((usage[t] & used_as_emissive) != 0)
                {
                    space = gpu::color_space::srgb;
                }
                else if ((usage[t] & used_as_data) != 0)
                {
                    space = gpu::color_space::linear;
                }
                if ((usage[t] & (used_as_base_color | used_as_emissive)) != 0 && (usage[t] & used_as_data) != 0)
                {
                    LOG_WRN("gltf: texture %zu of '%s' is sampled both as colour and as data; loaded as colour",
                            t,
                            ctx.path_name());
                }

                const std::size_t image_index = cgltf_image_index(&ctx.data, texture.image);
                if (texture.image->buffer_view == nullptr && is_file_uri(texture.image->uri))
                {
                    // A file of its own: the path is the cache key, shared
                    // with every other loader of that file.
                    try
                    {
                        model.textures[t] =
                            ctx.cache.load_texture(resolve_file_uri(ctx.base_dir, texture.image->uri), space);
                    }
                    catch (const std::runtime_error&)
                    {
                        LOG_WRN("gltf: texture %zu of '%s' could not be loaded", t, ctx.path_name());
                    }
                    continue;
                }

                // Embedded (GLB chunk or data URI): keyed on the file's
                // identity plus the image index.
                if (const util::image* decoded = ctx.decoded_image(image_index); decoded != nullptr)
                {
                    model.textures[t] = ctx.cache.load_texture_from_image(
                        ctx.identity + "#image" + std::to_string(image_index), *decoded, space);
                }
            }
        }

        gltf_material_description describe_material(import_context& ctx, const cgltf_material& material)
        {
            gltf_material_description description;
            description.name = material.name != nullptr ? material.name : "";
            description.base_color_space = ctx.options.base_color_space;

            if (material.has_pbr_metallic_roughness)
            {
                const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
                description.base_color_factor = math::vec4{pbr.base_color_factor[0],
                                                           pbr.base_color_factor[1],
                                                           pbr.base_color_factor[2],
                                                           pbr.base_color_factor[3]};
                description.metallic_factor = pbr.metallic_factor;
                description.roughness_factor = pbr.roughness_factor;
                description.base_color_map = ctx.map_image(pbr.base_color_texture, "baseColorTexture");
                if (const import_context::split_maps* split = ctx.orm_maps(pbr.metallic_roughness_texture);
                    split != nullptr)
                {
                    description.metallic_map = &split->metallic;
                    description.roughness_map = &split->roughness;
                }
            }
            else if (material.has_pbr_specular_glossiness)
            {
                LOG_WRN("gltf: material '%s' of '%s' is specular-glossiness only, which is not supported; "
                        "using default factors",
                        description.name.c_str(),
                        ctx.path_name());
            }

            description.emissive_factor =
                math::vec3{material.emissive_factor[0], material.emissive_factor[1], material.emissive_factor[2]};
            if (material.has_emissive_strength)
            {
                description.emissive_strength = material.emissive_strength.emissive_strength;
            }
            description.normal_map = ctx.map_image(material.normal_texture, "normalTexture");
            description.emissive_map = ctx.map_image(material.emissive_texture, "emissiveTexture");

            // What standard_material cannot take yet, named once per material
            // so a dull-looking import is not a mystery.
            if (material.occlusion_texture.texture != nullptr)
            {
                LOG_WRN(
                    "gltf: material '%s' of '%s' has an occlusion map; standard_material has no occlusion input yet",
                    description.name.c_str(),
                    ctx.path_name());
            }
            if (material.alpha_mode != cgltf_alpha_mode_opaque)
            {
                LOG_WRN("gltf: material '%s' of '%s' is not opaque; alpha blending / masking is not imported",
                        description.name.c_str(),
                        ctx.path_name());
            }
            if (material.double_sided)
            {
                LOG_WRN("gltf: material '%s' of '%s' is double-sided; the imported material culls back faces",
                        description.name.c_str(),
                        ctx.path_name());
            }
            return description;
        }

        // Stage 3: one material per glTF material through the factory, plus
        // the shared default when a primitive names none.
        void import_materials(import_context& ctx, gltf_model& model, gltf_material_factory& factory)
        {
            model.materials.reserve(ctx.data.materials_count);
            for (std::size_t m = 0; m < ctx.data.materials_count; ++m)
            {
                model.materials.push_back(factory.create(describe_material(ctx, ctx.data.materials[m])));
            }

            const bool needs_default =
                std::any_of(model.primitives.begin(),
                            model.primitives.end(),
                            [](const gltf_mesh_primitive& primitive) { return primitive.material_index == gltf_npos; });
            if (needs_default)
            {
                // glTF's default material: white, fully metallic, fully rough.
                gltf_material_description description;
                description.name = "gltf default";
                description.base_color_space = ctx.options.base_color_space;
                model.default_material = factory.create(description);
            }
        }

        // TRS of a node authored as a matrix (column-major, as glTF stores
        // it). The scale is the length of each basis column (negated on X
        // for a mirroring matrix), the rotation the normalized basis.
        void decompose_matrix(const cgltf_float* m, gltf_node& node)
        {
            node.translation = math::vec3{m[12], m[13], m[14]};

            const math::vec3 c0{m[0], m[1], m[2]};
            const math::vec3 c1{m[4], m[5], m[6]};
            const math::vec3 c2{m[8], m[9], m[10]};
            math::vec3 scale{math::length(c0), math::length(c1), math::length(c2)};
            if (math::dot(math::cross(c0, c1), c2) < 0.0f)
            {
                scale.x = -scale.x;
            }
            node.scale = scale;

            if (scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f)
            {
                node.rotation = math::quat{};
                return;
            }
            const math::vec3 x = c0 / scale.x;
            const math::vec3 y = c1 / scale.y;
            const math::vec3 z = c2 / scale.z;
            // r[row][column] of the rotation matrix whose columns are x, y, z.
            const float r00 = x.x, r01 = y.x, r02 = z.x;
            const float r10 = x.y, r11 = y.y, r12 = z.y;
            const float r20 = x.z, r21 = y.z, r22 = z.z;

            math::quat q;
            const float trace = r00 + r11 + r22;
            if (trace > 0.0f)
            {
                const float s = std::sqrt(trace + 1.0f) * 2.0f;
                q = math::quat{0.25f * s, (r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s};
            }
            else if (r00 > r11 && r00 > r22)
            {
                const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
                q = math::quat{(r21 - r12) / s, 0.25f * s, (r01 + r10) / s, (r02 + r20) / s};
            }
            else if (r11 > r22)
            {
                const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
                q = math::quat{(r02 - r20) / s, (r01 + r10) / s, 0.25f * s, (r12 + r21) / s};
            }
            else
            {
                const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
                q = math::quat{(r10 - r01) / s, (r02 + r20) / s, (r12 + r21) / s, 0.25f * s};
            }
            node.rotation = math::normalize(q);
        }

        // Stage 4: the node tree and which roots to instantiate.
        void import_nodes(import_context& ctx,
                          gltf_model& model,
                          const std::vector<std::vector<std::size_t>>& primitives_by_mesh)
        {
            model.nodes.resize(ctx.data.nodes_count);
            bool skinned = false;
            for (std::size_t k = 0; k < ctx.data.nodes_count; ++k)
            {
                const cgltf_node& source = ctx.data.nodes[k];
                gltf_node& node = model.nodes[k];
                node.name = source.name != nullptr ? source.name : "";
                node.parent = source.parent != nullptr ? cgltf_node_index(&ctx.data, source.parent) : gltf_npos;
                node.children.reserve(source.children_count);
                for (std::size_t c = 0; c < source.children_count; ++c)
                {
                    node.children.push_back(cgltf_node_index(&ctx.data, source.children[c]));
                }

                if (source.has_matrix)
                {
                    decompose_matrix(source.matrix, node);
                }
                else
                {
                    if (source.has_translation)
                    {
                        node.translation =
                            math::vec3{source.translation[0], source.translation[1], source.translation[2]};
                    }
                    if (source.has_rotation)
                    {
                        // glTF stores (x, y, z, w); the engine's quat is (w, x, y, z).
                        node.rotation =
                            math::quat{source.rotation[3], source.rotation[0], source.rotation[1], source.rotation[2]};
                    }
                    if (source.has_scale)
                    {
                        node.scale = math::vec3{source.scale[0], source.scale[1], source.scale[2]};
                    }
                }

                if (source.mesh != nullptr)
                {
                    node.primitives = primitives_by_mesh[cgltf_mesh_index(&ctx.data, source.mesh)];
                }
                skinned = skinned || source.skin != nullptr;
            }

            // The default scene names the roots; without one fall back to the
            // first scene, then to every parentless node.
            const cgltf_scene* scene = ctx.data.scene;
            if (scene == nullptr && ctx.data.scenes_count > 0)
            {
                scene = &ctx.data.scenes[0];
            }
            if (scene != nullptr)
            {
                model.root_nodes.reserve(scene->nodes_count);
                for (std::size_t n = 0; n < scene->nodes_count; ++n)
                {
                    model.root_nodes.push_back(cgltf_node_index(&ctx.data, scene->nodes[n]));
                }
            }
            else
            {
                for (std::size_t k = 0; k < model.nodes.size(); ++k)
                {
                    if (model.nodes[k].parent == gltf_npos)
                    {
                        model.root_nodes.push_back(k);
                    }
                }
            }

            if (skinned || ctx.data.animations_count > 0)
            {
                LOG_WRN("gltf: '%s' carries skins or animations, which are not imported; meshes are static",
                        ctx.path_name());
            }
        }
    } // namespace

    gltf_model load_gltf(const std::filesystem::path& path,
                         asset_cache& cache,
                         gltf_material_factory& materials,
                         const gltf_import_options& options)
    {
        const std::string path_string = path.string();

        cgltf_options parse_options{};
        cgltf_data* raw = nullptr;
        cgltf_result result = cgltf_parse_file(&parse_options, path_string.c_str(), &raw);
        if (result != cgltf_result_success)
        {
            LOG_ERR("gltf: could not parse '%s': %s", path_string.c_str(), result_name(result));
            throw std::runtime_error{"Could not parse glTF file (" + path_string + ")"};
        }
        const cgltf_data_ptr data{raw};

        // Pulls external .bin files (relative to the glTF), base64 buffer
        // URIs and the GLB binary chunk into memory.
        result = cgltf_load_buffers(&parse_options, data.get(), path_string.c_str());
        if (result != cgltf_result_success)
        {
            LOG_ERR("gltf: could not load the buffers of '%s': %s", path_string.c_str(), result_name(result));
            throw std::runtime_error{"Could not load glTF buffers (" + path_string + ")"};
        }

        // Accessor ranges, index bounds and the like: nothing below reads
        // buffer memory a validated file cannot vouch for.
        result = cgltf_validate(data.get());
        if (result != cgltf_result_success)
        {
            LOG_ERR("gltf: '%s' failed validation: %s", path_string.c_str(), result_name(result));
            throw std::runtime_error{"Invalid glTF file (" + path_string + ")"};
        }

        import_context ctx{*data, path, cache, options};
        gltf_model model;
        std::vector<std::vector<std::size_t>> primitives_by_mesh;
        import_primitives(ctx, model, primitives_by_mesh);
        import_textures(ctx, model);
        import_materials(ctx, model, materials);
        import_nodes(ctx, model, primitives_by_mesh);

        LOG_INF("gltf: loaded '%s' (%zu nodes, %zu primitives, %zu materials, %zu textures)",
                path_string.c_str(),
                model.nodes.size(),
                model.primitives.size(),
                model.materials.size(),
                model.textures.size());
        return model;
    }
} // namespace rendering_engine
