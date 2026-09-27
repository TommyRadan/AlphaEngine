// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/assets/gltf_importer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/tangent.hpp>
#include <rendering_engine/assets/vertex.hpp>

// cgltf is a single-header library; this is its one implementation unit
// (the same arrangement as stb_image in assets/image.cpp).
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace rendering_engine
{
    namespace
    {
        namespace math = core::math;

        using base_vertex = vertex_position_uv_normal;
        using tangent_vertex = vertex_position_uv_normal_tangent;
        using skin_vertex = vertex_position_uv_normal_tangent_skin;

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
            return "gltf:" + core::default_vfs().canonical_key(path);
        }

        // cgltf's file hooks, so the .gltf / .glb itself and every external
        // .bin it references are read through the virtual filesystem: a
        // mount-relative model path resolves its buffers under the same
        // mount. cgltf combines the glTF's directory with each (decoded)
        // buffer URI before calling read, so `path` arrives fully spelled.
        // The bytes come from the memory hooks' allocator (malloc by default)
        // because cgltf_free releases the glTF's own file data through it.
        cgltf_result vfs_file_read(const cgltf_memory_options* memory_options,
                                   const cgltf_file_options* /*file_options*/,
                                   const char* path,
                                   cgltf_size* size,
                                   void** data)
        {
            std::vector<std::byte> bytes;
            if (!core::default_vfs().read_file(core::platform::utf8_path(path), bytes))
            {
                return cgltf_result_file_not_found;
            }
            // A non-zero *size is the byte count the caller expects (a buffer's
            // declared length); a shorter file cannot satisfy it.
            cgltf_size wanted = size != nullptr ? *size : 0;
            if (wanted == 0)
            {
                wanted = bytes.size();
            }
            else if (bytes.size() < wanted)
            {
                return cgltf_result_io_error;
            }

            void* (*allocate)(void*, cgltf_size) = memory_options->alloc_func;
            void* block = allocate != nullptr ? allocate(memory_options->user_data, wanted) : std::malloc(wanted);
            if (block == nullptr)
            {
                return cgltf_result_out_of_memory;
            }
            if (wanted > 0)
            {
                std::memcpy(block, bytes.data(), wanted);
            }
            if (size != nullptr)
            {
                *size = wanted;
            }
            if (data != nullptr)
            {
                *data = block;
            }
            return cgltf_result_success;
        }

        void vfs_file_release(const cgltf_memory_options* memory_options,
                              const cgltf_file_options* /*file_options*/,
                              void* data,
                              cgltf_size /*size*/)
        {
            void (*release)(void*, void*) = memory_options->free_func;
            if (release != nullptr)
            {
                release(memory_options->user_data, data);
            }
            else
            {
                std::free(data);
            }
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

        // Unpacks a JOINTS_0 accessor (u8 or u16 VEC4) into four indices per
        // vertex.
        bool unpack_joints(const cgltf_accessor& accessor, std::vector<cgltf_uint>& out, const std::string& label)
        {
            if (cgltf_num_components(accessor.type) != 4)
            {
                LOG_WRN("gltf: JOINTS_0 attribute of %s has %zu components, expected 4",
                        label.c_str(),
                        cgltf_num_components(accessor.type));
                return false;
            }
            out.resize(accessor.count * 4);
            for (std::size_t v = 0; v < accessor.count; ++v)
            {
                if (!cgltf_accessor_read_uint(&accessor, v, &out[v * 4], 4))
                {
                    LOG_WRN("gltf: could not read the JOINTS_0 attribute of %s", label.c_str());
                    return false;
                }
            }
            return true;
        }

        // The skinned records: each tangent record with the joints and
        // weights of the source vertex it was built from. Weights are
        // renormalised to sum to 1 (quantised exports drift); a vertex with
        // no weight at all is bound wholly to its first joint.
        std::vector<skin_vertex> with_skin(const std::vector<tangent_vertex>& records,
                                           const std::vector<uint32_t>& sources,
                                           const std::vector<cgltf_uint>& joints,
                                           const std::vector<float>& weights)
        {
            std::vector<skin_vertex> out;
            out.reserve(records.size());
            for (std::size_t v = 0; v < records.size(); ++v)
            {
                const std::size_t source = sources[v];
                skin_vertex record{};
                record.pos = records[v].pos;
                record.uv = records[v].uv;
                record.normal = records[v].normal;
                record.tangent = records[v].tangent;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    record.joints[k] = static_cast<uint16_t>(std::min<cgltf_uint>(joints[source * 4 + k], UINT16_MAX));
                }
                const math::vec4 w = read_vec4(weights, source);
                const float sum = w.x + w.y + w.z + w.w;
                record.weights = sum > 0.0f ? w / sum : math::vec4{1.0f, 0.0f, 0.0f, 0.0f};
                out.push_back(record);
            }
            return out;
        }

        // Builds the vertex_position_uv_normal_tangent records (with the skin
        // channels, vertex_position_uv_normal_tangent_skin, when the
        // primitive has JOINTS_0 and WEIGHTS_0) and 32-bit indices of one
        // TRIANGLES primitive. Throws primitive_import_error (after warning)
        // when the attributes cannot be read.
        mesh_data
        build_geometry(const cgltf_primitive& primitive, const gltf_import_options& options, const std::string& label)
        {
            const cgltf_accessor* position = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
            const cgltf_accessor* normal = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* texcoord = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* tangent = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
            const cgltf_accessor* joint = cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0);
            const cgltf_accessor* weight = cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, 0);

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
            std::vector<cgltf_uint> joints;
            std::vector<float> weights;
            const bool has_skin = joint != nullptr && weight != nullptr && joint->count == vertex_count &&
                                  weight->count == vertex_count && unpack_joints(*joint, joints, label) &&
                                  unpack_floats(*weight, 4, weights, "WEIGHTS_0", label);
            if (has_skin && cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 1) != nullptr)
            {
                LOG_WRN("gltf: %s has more than four joint influences per vertex; only JOINTS_0 / WEIGHTS_0 "
                        "are imported",
                        label.c_str());
            }

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

            // sources[v] is the file vertex output vertex v was built from, so
            // the skin channels follow the flat-shading split below.
            std::vector<base_vertex> vertices;
            std::vector<uint32_t> sources;
            std::vector<tangent_vertex> records;
            if (has_normals)
            {
                vertices.resize(vertex_count);
                sources.resize(vertex_count);
                std::iota(sources.begin(), sources.end(), 0u);
                for (std::size_t v = 0; v < vertex_count; ++v)
                {
                    vertices[v].pos = read_vec3(positions, v);
                    vertices[v].uv = has_uvs ? read_vec2(uvs, v) : math::vec2{0.0f, 0.0f};
                    vertices[v].normal = read_vec3(normals, v);
                }

                if (has_tangents)
                {
                    records.reserve(vertex_count);
                    for (std::size_t v = 0; v < vertex_count; ++v)
                    {
                        records.push_back(tangent_vertex{
                            vertices[v].pos, vertices[v].uv, vertices[v].normal, read_vec4(tangents, v)});
                    }
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
                        sources.push_back(source);
                    }
                }
                indices.resize(vertices.size());
                std::iota(indices.begin(), indices.end(), 0u);
            }

            if (records.empty())
            {
                records = options.generate_missing_tangents ? generate_tangents(vertices, indices)
                                                            : with_placeholder_tangents(vertices);
            }
            if (has_skin)
            {
                return mesh_data::from_vertices(with_skin(records, sources, joints, weights), std::move(indices));
            }
            return mesh_data::from_vertices(records, std::move(indices));
        }

        std::string primitive_label(std::size_t mesh, std::size_t primitive, const std::filesystem::path& path)
        {
            return "mesh " + std::to_string(mesh) + " primitive " + std::to_string(primitive) + " of '" +
                   path.string() + "'";
        }

        // Whether @p primitive is one the importer turns into a mesh:
        // TRIANGLES, not Draco-compressed, with a POSITION attribute. With
        // @p warn the reason one is skipped is logged (by the finishing
        // stage, so each warning appears once).
        bool importable_primitive(const cgltf_primitive& primitive, const std::string& label, bool warn)
        {
            if (primitive.type != cgltf_primitive_type_triangles)
            {
                if (warn)
                {
                    LOG_WRN("gltf: %s uses %s topology; only TRIANGLES are imported, skipped",
                            label.c_str(),
                            topology_name(primitive.type));
                }
                return false;
            }
            if (primitive.has_draco_mesh_compression)
            {
                if (warn)
                {
                    LOG_WRN("gltf: %s is Draco-compressed, which is not supported; skipped", label.c_str());
                }
                return false;
            }
            if (cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0) == nullptr)
            {
                if (warn)
                {
                    LOG_WRN("gltf: %s has no POSITION attribute; skipped", label.c_str());
                }
                return false;
            }
            return true;
        }

        // One primitive's geometry, built ahead by a prebuilding
        // begin_gltf_import.
        struct prebuilt_geometry
        {
            std::optional<mesh_data> data;
            // build_geometry refused the primitive (and warned); it is
            // skipped without a second attempt.
            bool failed{false};
        };
    } // namespace

    // The state both halves of an import share: the parsed file (owned, so
    // it outlives the worker that parsed it), where its relative URIs
    // resolve, the decoded images (lazily, at most once each), the geometry
    // built ahead and the parts of the model the CPU stages produce.
    struct gltf_import
    {
        gltf_import(cgltf_data_ptr parsed, const std::filesystem::path& in_path, const gltf_import_options& in_options)
            : owner{std::move(parsed)}, data{*owner}, path{in_path}, path_text{in_path.string()},
              base_dir{in_path.parent_path()}, identity{file_identity(in_path)}, options{in_options},
              images(data.images_count), image_failed(data.images_count, false), geometry(data.meshes_count)
        {
        }

        gltf_import(const gltf_import&) = delete;
        gltf_import& operator=(const gltf_import&) = delete;

        cgltf_data_ptr owner;
        const cgltf_data& data;
        std::filesystem::path path;
        std::string path_text;
        std::filesystem::path base_dir;
        std::string identity;
        gltf_import_options options;

        // Index-aligned with data.images; an entry is decoded on first use
        // and left empty (with the failure remembered) when it cannot be.
        std::vector<std::optional<image>> images;
        std::vector<bool> image_failed;

        // geometry[i][j] is primitive j of mesh i when prebuilt; the inner
        // vectors stay empty otherwise.
        std::vector<std::vector<prebuilt_geometry>> geometry;

        // The nodes (their primitives linked by the finishing stage), the
        // roots, the skeleton and the clips; completed and handed out by
        // finish_gltf_import.
        gltf_model model;

        // The file name for log lines.
        const char* path_name() const
        {
            return path_text.c_str();
        }

        // The decoded pixels of image @p index, or nullptr when it cannot
        // be decoded (warned about once).
        const image* decoded_image(std::size_t index)
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

        std::optional<image> decode_image(const cgltf_image& image, std::size_t index)
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
                    return rendering_engine::image{bytes, image.buffer_view->size};
                }
                if (const char* payload = data_uri_payload(image.uri); payload != nullptr)
                {
                    const std::vector<uint8_t> bytes = decode_base64(payload);
                    if (bytes.empty())
                    {
                        LOG_WRN("gltf: image %zu of '%s' has a malformed data URI", index, path_name());
                        return std::nullopt;
                    }
                    return rendering_engine::image{bytes.data(), bytes.size()};
                }
                if (!is_file_uri(image.uri))
                {
                    LOG_WRN("gltf: image %zu of '%s' has an unsupported URI (%s)",
                            index,
                            path_name(),
                            image.uri != nullptr ? image.uri : "none");
                    return std::nullopt;
                }
                return rendering_engine::image{resolve_file_uri(base_dir, image.uri).string()};
            }
            catch (const std::runtime_error&)
            {
                // rendering_engine::image already logged the decoder's reason.
                LOG_WRN("gltf: image %zu of '%s' could not be decoded; maps using it are skipped", index, path_name());
                return std::nullopt;
            }
        }

        // The map a texture view samples, decoded, or nullptr when the
        // view names no texture or its image failed to decode.
        const image* map_image(const cgltf_texture_view& view, const char* slot)
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
    };

    void gltf_import_deleter::operator()(gltf_import* import) const noexcept
    {
        delete import;
    }

    namespace
    {
        // CPU stage (prebuild): every importable primitive's geometry, so
        // the main thread only uploads it.
        void prebuild_geometry(gltf_import& ctx)
        {
            for (std::size_t i = 0; i < ctx.data.meshes_count; ++i)
            {
                const cgltf_mesh& mesh = ctx.data.meshes[i];
                ctx.geometry[i].resize(mesh.primitives_count);
                for (std::size_t j = 0; j < mesh.primitives_count; ++j)
                {
                    const cgltf_primitive& primitive = mesh.primitives[j];
                    const std::string label = primitive_label(i, j, ctx.path);
                    if (!importable_primitive(primitive, label, false))
                    {
                        continue;
                    }
                    prebuilt_geometry& built = ctx.geometry[i][j];
                    try
                    {
                        built.data = build_geometry(primitive, ctx.options, label);
                        built.data->bounds =
                            accessor_bounds(*cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0));
                    }
                    catch (const primitive_import_error&)
                    {
                        built.failed = true; // already warned
                    }
                }
            }
        }

        // CPU stage (prebuild): every image a texture samples, decoded once
        // (external files included), so the main thread only uploads.
        void predecode_images(gltf_import& ctx)
        {
            for (std::size_t t = 0; t < ctx.data.textures_count; ++t)
            {
                const cgltf_texture& texture = ctx.data.textures[t];
                if (texture.image != nullptr)
                {
                    ctx.decoded_image(cgltf_image_index(&ctx.data, texture.image));
                }
            }
        }

        // The cache texture for image @p index in @p space, or null when it
        // cannot be decoded (the decoder warned). An image with a file of
        // its own is keyed on that file's path — shared with every other
        // loader of it, and followed by a debug hot reload — through the
        // decode already in hand when there is one; an embedded image (GLB
        // chunk or data URI) on the model's identity plus the index.
        std::shared_ptr<texture_asset>
        image_texture(gltf_import& ctx, asset_cache& cache, std::size_t index, gpu::color_space space)
        {
            const cgltf_image& image = ctx.data.images[index];
            if (image.buffer_view == nullptr && is_file_uri(image.uri))
            {
                const std::filesystem::path file = resolve_file_uri(ctx.base_dir, image.uri);
                if (ctx.images[index].has_value())
                {
                    return cache.adopt_texture_image(file, *ctx.images[index], space);
                }
                if (ctx.image_failed[index])
                {
                    return nullptr;
                }
                try
                {
                    return cache.load_texture(file, space);
                }
                catch (const std::runtime_error&)
                {
                    return nullptr; // the decoder logged why
                }
            }
            const rendering_engine::image* decoded = ctx.decoded_image(index);
            if (decoded == nullptr)
            {
                return nullptr;
            }
            return cache.load_texture_from_image(ctx.identity + "#image" + std::to_string(index), *decoded, space);
        }

        // Finishing stage: every TRIANGLES primitive through the cache.
        // Fills @p primitives_by_mesh (glTF mesh index -> model primitive
        // indices) for the node links.
        void import_primitives(gltf_import& ctx,
                               asset_cache& cache,
                               std::vector<std::vector<std::size_t>>& primitives_by_mesh)
        {
            gltf_model& model = ctx.model;
            primitives_by_mesh.assign(ctx.data.meshes_count, {});
            for (std::size_t i = 0; i < ctx.data.meshes_count; ++i)
            {
                const cgltf_mesh& mesh = ctx.data.meshes[i];
                for (std::size_t j = 0; j < mesh.primitives_count; ++j)
                {
                    const cgltf_primitive& primitive = mesh.primitives[j];
                    const std::string label = primitive_label(i, j, ctx.path);
                    if (!importable_primitive(primitive, label, true))
                    {
                        continue;
                    }
                    prebuilt_geometry* built = j < ctx.geometry[i].size() ? &ctx.geometry[i][j] : nullptr;
                    if (built != nullptr && built->failed)
                    {
                        continue; // already warned; nothing was cached
                    }
                    const cgltf_accessor* position = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);

                    // The builder only runs on a cache miss; a hit shares the
                    // upload of an earlier load of this file (and drops any
                    // geometry built ahead).
                    const std::string key = ctx.identity + "#mesh" + std::to_string(i) + "/prim" + std::to_string(j);
                    std::shared_ptr<mesh_asset> asset;
                    try
                    {
                        asset = cache.get_or_create_mesh(key,
                                                         [&]
                                                         {
                                                             if (built != nullptr && built->data.has_value())
                                                             {
                                                                 mesh_data data = std::move(*built->data);
                                                                 built->data.reset();
                                                                 return data;
                                                             }
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
                    if (asset == nullptr)
                    {
                        continue; // the cache logged why
                    }

                    gltf_mesh_primitive imported;
                    imported.mesh = std::move(asset);
                    imported.material_index =
                        primitive.material != nullptr ? cgltf_material_index(&ctx.data, primitive.material) : gltf_npos;
                    imported.skinned = imported.mesh->format == vertex_format::position_uv_normal_tangent_skin;
                    primitives_by_mesh[i].push_back(model.primitives.size());
                    model.primitives.push_back(std::move(imported));
                }
            }
        }

        // Finishing stage: one cache texture per glTF texture, in the colour
        // space the materials sampling it expect.
        void import_textures(gltf_import& ctx, asset_cache& cache)
        {
            gltf_model& model = ctx.model;
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

                model.textures[t] = image_texture(ctx, cache, cgltf_image_index(&ctx.data, texture.image), space);
                if (model.textures[t] == nullptr && texture.image->buffer_view == nullptr &&
                    is_file_uri(texture.image->uri))
                {
                    LOG_WRN("gltf: texture %zu of '%s' could not be loaded", t, ctx.path_name());
                }
            }
        }

        // The cache texture behind a decoded map, in the colour space its
        // slot samples it in; null exactly when @p image is.
        std::shared_ptr<texture_asset> slot_texture(gltf_import& ctx,
                                                    asset_cache& cache,
                                                    const cgltf_texture_view& view,
                                                    const image* image,
                                                    gpu::color_space space)
        {
            if (image == nullptr)
            {
                return nullptr;
            }
            return image_texture(ctx, cache, cgltf_image_index(&ctx.data, view.texture->image), space);
        }

        gltf_material_description
        describe_material(gltf_import& ctx, asset_cache& cache, const cgltf_material& material)
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
                description.base_color_texture = slot_texture(
                    ctx, cache, pbr.base_color_texture, description.base_color_map, ctx.options.base_color_space);
                // Passed through packed: G roughness, B metallic, the ORM
                // layout the material samples directly.
                description.metallic_roughness_map =
                    ctx.map_image(pbr.metallic_roughness_texture, "metallicRoughnessTexture");
                description.metallic_roughness_texture = slot_texture(ctx,
                                                                      cache,
                                                                      pbr.metallic_roughness_texture,
                                                                      description.metallic_roughness_map,
                                                                      gpu::color_space::linear);
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
            description.normal_texture =
                slot_texture(ctx, cache, material.normal_texture, description.normal_map, gpu::color_space::linear);
            description.emissive_map = ctx.map_image(material.emissive_texture, "emissiveTexture");
            description.emissive_texture =
                slot_texture(ctx, cache, material.emissive_texture, description.emissive_map, gpu::color_space::srgb);
            // The occlusion texture is usually the metallic-roughness image
            // itself (R channel); the factory tells the two cases apart by
            // comparing the pointers. cgltf stores strength in scale.
            description.occlusion_map = ctx.map_image(material.occlusion_texture, "occlusionTexture");
            description.occlusion_texture = slot_texture(
                ctx, cache, material.occlusion_texture, description.occlusion_map, gpu::color_space::linear);
            if (description.occlusion_map != nullptr)
            {
                description.occlusion_strength = material.occlusion_texture.scale;
            }

            // What standard_material does not take, named once per material
            // so a dull-looking import is not a mystery.
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

        // Finishing stage: one material per glTF material through the
        // factory, plus the shared default when a primitive names none, and
        // a skinned twin of each of those a skinned primitive draws with.
        void import_materials(gltf_import& ctx, asset_cache& cache, gltf_material_factory& factory)
        {
            gltf_model& model = ctx.model;
            std::vector<gltf_material_description> descriptions;
            descriptions.reserve(ctx.data.materials_count);
            model.materials.reserve(ctx.data.materials_count);
            for (std::size_t m = 0; m < ctx.data.materials_count; ++m)
            {
                descriptions.push_back(describe_material(ctx, cache, ctx.data.materials[m]));
                model.materials.push_back(factory.create(descriptions.back()));
            }

            const bool needs_default =
                std::any_of(model.primitives.begin(),
                            model.primitives.end(),
                            [](const gltf_mesh_primitive& primitive) { return primitive.material_index == gltf_npos; });
            // glTF's default material: white, fully metallic, fully rough.
            gltf_material_description default_description;
            default_description.name = "gltf default";
            default_description.base_color_space = ctx.options.base_color_space;
            if (needs_default)
            {
                model.default_material = factory.create(default_description);
            }

            // The skinning twins, only for the materials skinned primitives
            // name. The descriptions are reused, so every image is still
            // decoded once and every warning still logged once.
            model.skinned_materials.assign(ctx.data.materials_count, nullptr);
            std::vector<bool> skinned_use(ctx.data.materials_count, false);
            bool skinned_default = false;
            for (const gltf_mesh_primitive& primitive : model.primitives)
            {
                if (!primitive.skinned)
                {
                    continue;
                }
                if (primitive.material_index == gltf_npos)
                {
                    skinned_default = true;
                }
                else if (primitive.material_index < skinned_use.size())
                {
                    skinned_use[primitive.material_index] = true;
                }
            }
            for (std::size_t m = 0; m < descriptions.size(); ++m)
            {
                if (skinned_use[m])
                {
                    gltf_material_description description = descriptions[m];
                    description.skinned = true;
                    model.skinned_materials[m] = factory.create(description);
                }
            }
            if (skinned_default)
            {
                default_description.skinned = true;
                model.skinned_default_material = factory.create(default_description);
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

        // CPU stage: the node tree and which roots to instantiate (each
        // node's primitives are linked by the finishing stage).
        void import_nodes(gltf_import& ctx)
        {
            gltf_model& model = ctx.model;
            model.nodes.resize(ctx.data.nodes_count);
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

                node.skin = source.skin != nullptr ? cgltf_skin_index(&ctx.data, source.skin) : gltf_npos;
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

            const bool morphs = std::any_of(ctx.data.meshes,
                                            ctx.data.meshes + ctx.data.meshes_count,
                                            [](const cgltf_mesh& mesh)
                                            {
                                                return std::any_of(mesh.primitives,
                                                                   mesh.primitives + mesh.primitives_count,
                                                                   [](const cgltf_primitive& primitive)
                                                                   { return primitive.targets_count > 0; });
                                            });
            if (morphs)
            {
                LOG_WRN("gltf: '%s' carries morph targets, which are not imported; those meshes keep their base shape",
                        ctx.path_name());
            }
        }

        // Finishing stage: each node's primitives, once import_primitives has
        // numbered them.
        void link_node_primitives(gltf_import& ctx, const std::vector<std::vector<std::size_t>>& primitives_by_mesh)
        {
            for (std::size_t k = 0; k < ctx.data.nodes_count && k < ctx.model.nodes.size(); ++k)
            {
                const cgltf_node& source = ctx.data.nodes[k];
                if (source.mesh != nullptr)
                {
                    ctx.model.nodes[k].primitives = primitives_by_mesh[cgltf_mesh_index(&ctx.data, source.mesh)];
                }
            }
        }

        using runtime::animation::animation_clip;
        using runtime::animation::joint_track;
        using runtime::animation::no_joint;
        using runtime::animation::skeleton;
        using runtime::animation::skeleton_joint;
        using runtime::animation::skeleton_skin;

        // CPU stage: the skeleton over every node, with one palette per glTF
        // skin. Only built when something will pose it (a skin or an
        // animation).
        void import_skeleton(gltf_import& ctx)
        {
            gltf_model& model = ctx.model;
            if (ctx.data.skins_count == 0 && ctx.data.animations_count == 0)
            {
                return;
            }

            std::vector<skeleton_joint> joints(model.nodes.size());
            for (std::size_t k = 0; k < model.nodes.size(); ++k)
            {
                const gltf_node& node = model.nodes[k];
                joints[k].name = node.name;
                joints[k].parent = node.parent == gltf_npos ? no_joint : node.parent;
                joints[k].bind_pose = math::trs{node.translation, node.rotation, node.scale};
            }

            std::vector<skeleton_skin> skins(ctx.data.skins_count);
            for (std::size_t s = 0; s < ctx.data.skins_count; ++s)
            {
                const cgltf_skin& source = ctx.data.skins[s];
                skeleton_skin& skin = skins[s];
                skin.name = source.name != nullptr ? source.name : "skin " + std::to_string(s);
                skin.joints.reserve(source.joints_count);
                for (std::size_t j = 0; j < source.joints_count; ++j)
                {
                    skin.joints.push_back(source.joints[j] != nullptr ? cgltf_node_index(&ctx.data, source.joints[j])
                                                                      : no_joint);
                }

                // Absent inverse bind matrices are identity (the skeleton
                // pads them), which is what the spec defaults them to.
                std::vector<float> matrices;
                const std::string label = "skin '" + skin.name + "' of '" + ctx.path.string() + "'";
                if (source.inverse_bind_matrices != nullptr &&
                    unpack_floats(*source.inverse_bind_matrices, 16, matrices, "inverseBindMatrices", label))
                {
                    const std::size_t count = std::min(source.inverse_bind_matrices->count, source.joints_count);
                    skin.inverse_bind_matrices.resize(count);
                    for (std::size_t j = 0; j < count; ++j)
                    {
                        std::copy_n(&matrices[j * 16], 16, skin.inverse_bind_matrices[j].m);
                    }
                }
            }

            model.node_skeleton = std::make_shared<const skeleton>(std::move(joints), std::move(skins));
        }

        math::interpolation to_interpolation(cgltf_interpolation_type type)
        {
            switch (type)
            {
            case cgltf_interpolation_type_step:
                return math::interpolation::step;
            case cgltf_interpolation_type_cubic_spline:
                return math::interpolation::cubic_spline;
            default:
                return math::interpolation::linear;
            }
        }

        // glTF stores a rotation as (x, y, z, w); the engine's quat is (w, x, y, z).
        math::quat read_rotation(const std::vector<float>& values, std::size_t index)
        {
            const math::vec4 v = read_vec4(values, index);
            return math::quat{v.w, v.x, v.y, v.z};
        }

        // The curve of one sampler: @p values holds one element per key, or
        // three (in-tangent, value, out-tangent) for a cubic spline.
        template<typename T, typename Read>
        math::curve<T> make_curve(const std::vector<float>& times,
                                  const std::vector<float>& values,
                                  math::interpolation mode,
                                  Read read)
        {
            math::curve<T> result;
            result.mode = mode;
            result.keys.resize(times.size());
            for (std::size_t k = 0; k < times.size(); ++k)
            {
                math::keyframe<T>& key = result.keys[k];
                key.time = times[k];
                if (mode == math::interpolation::cubic_spline)
                {
                    key.in_tangent = read(values, k * 3);
                    key.value = read(values, k * 3 + 1);
                    key.out_tangent = read(values, k * 3 + 2);
                }
                else
                {
                    key.value = read(values, k);
                }
            }
            return result;
        }

        // CPU stage: one clip per glTF animation. Channels are grouped into one
        // track per node they drive; the node index is the skeleton joint.
        void import_animations(gltf_import& ctx)
        {
            gltf_model& model = ctx.model;
            model.animations.reserve(ctx.data.animations_count);
            for (std::size_t a = 0; a < ctx.data.animations_count; ++a)
            {
                const cgltf_animation& animation = ctx.data.animations[a];
                const std::string name = animation.name != nullptr ? animation.name : "animation " + std::to_string(a);
                const std::string label = "animation '" + name + "' of '" + ctx.path.string() + "'";

                std::vector<joint_track> tracks;
                std::vector<std::size_t> track_of(model.nodes.size(), gltf_npos);
                bool weights_reported = false;
                for (std::size_t c = 0; c < animation.channels_count; ++c)
                {
                    const cgltf_animation_channel& channel = animation.channels[c];
                    if (channel.target_node == nullptr || channel.sampler == nullptr ||
                        channel.sampler->input == nullptr || channel.sampler->output == nullptr)
                    {
                        continue;
                    }
                    if (channel.target_path == cgltf_animation_path_type_weights)
                    {
                        if (!weights_reported)
                        {
                            LOG_WRN("gltf: %s animates morph-target weights, which are not imported", label.c_str());
                            weights_reported = true;
                        }
                        continue;
                    }
                    const bool is_rotation = channel.target_path == cgltf_animation_path_type_rotation;
                    if (!is_rotation && channel.target_path != cgltf_animation_path_type_translation &&
                        channel.target_path != cgltf_animation_path_type_scale)
                    {
                        continue;
                    }

                    const cgltf_animation_sampler& sampler = *channel.sampler;
                    std::vector<float> times;
                    if (!unpack_floats(*sampler.input, 1, times, "animation input", label) || times.empty())
                    {
                        continue;
                    }
                    if (!std::is_sorted(times.begin(), times.end()))
                    {
                        LOG_WRN("gltf: %s has a channel whose key times are not ascending; skipped", label.c_str());
                        continue;
                    }
                    const math::interpolation mode = to_interpolation(sampler.interpolation);
                    const std::size_t per_key = mode == math::interpolation::cubic_spline ? 3 : 1;
                    std::vector<float> values;
                    if (sampler.output->count != times.size() * per_key ||
                        !unpack_floats(*sampler.output, is_rotation ? 4 : 3, values, "animation output", label))
                    {
                        LOG_WRN("gltf: %s has a channel whose output does not match its %zu keys; skipped",
                                label.c_str(),
                                times.size());
                        continue;
                    }

                    const std::size_t target = cgltf_node_index(&ctx.data, channel.target_node);
                    if (track_of[target] == gltf_npos)
                    {
                        track_of[target] = tracks.size();
                        tracks.push_back(joint_track{});
                        tracks.back().joint = target;
                    }
                    joint_track& track = tracks[track_of[target]];
                    if (channel.target_path == cgltf_animation_path_type_translation)
                    {
                        track.translation = make_curve<math::vec3>(times, values, mode, read_vec3);
                    }
                    else if (channel.target_path == cgltf_animation_path_type_scale)
                    {
                        track.scale = make_curve<math::vec3>(times, values, mode, read_vec3);
                    }
                    else
                    {
                        track.rotation = make_curve<math::quat>(times, values, mode, read_rotation);
                        if (mode != math::interpolation::cubic_spline)
                        {
                            // Quantised or hand-written keys drift off the
                            // unit sphere; slerp wants them on it.
                            for (math::keyframe<math::quat>& key : track.rotation.keys)
                            {
                                key.value = math::normalize(key.value);
                            }
                        }
                    }
                }
                model.animations.push_back(std::make_shared<const animation_clip>(name, std::move(tracks)));
            }
        }
    } // namespace

    gltf_import_ptr
    begin_gltf_import(const std::filesystem::path& path, const gltf_import_options& options, bool prebuild)
    {
        // Generic separators: cgltf derives the buffers' directory from this
        // string, and the VFS reads the result whether it is mount-relative
        // or native.
        const std::string path_string = path.generic_string();

        cgltf_options parse_options{};
        parse_options.file.read = &vfs_file_read;
        parse_options.file.release = &vfs_file_release;
        cgltf_data* raw = nullptr;
        cgltf_result result = cgltf_parse_file(&parse_options, path_string.c_str(), &raw);
        if (result != cgltf_result_success)
        {
            LOG_ERR("gltf: could not parse '%s': %s", path_string.c_str(), result_name(result));
            throw std::runtime_error{"Could not parse glTF file (" + path_string + ")"};
        }
        cgltf_data_ptr data{raw};

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

        gltf_import_ptr import{new gltf_import(std::move(data), path, options)};
        if (prebuild)
        {
            prebuild_geometry(*import);
            predecode_images(*import);
        }
        import_nodes(*import);
        import_skeleton(*import);
        import_animations(*import);
        return import;
    }

    gltf_model finish_gltf_import(gltf_import& import, asset_cache& cache, gltf_material_factory& materials)
    {
        std::vector<std::vector<std::size_t>> primitives_by_mesh;
        import_primitives(import, cache, primitives_by_mesh);
        import_textures(import, cache);
        import_materials(import, cache, materials);
        link_node_primitives(import, primitives_by_mesh);

        gltf_model model = std::move(import.model);
        import.model = gltf_model{};
        LOG_INF("gltf: loaded '%s' (%zu nodes, %zu primitives, %zu materials, %zu textures)",
                import.path.generic_string().c_str(),
                model.nodes.size(),
                model.primitives.size(),
                model.materials.size(),
                model.textures.size());
        return model;
    }

    gltf_model load_gltf(const std::filesystem::path& path,
                         asset_cache& cache,
                         gltf_material_factory& materials,
                         const gltf_import_options& options)
    {
        // Both halves on this thread, and without the prebuild: the
        // geometry is built only for the primitives the cache misses, and a
        // texture file is decoded by the cache only when it is not held.
        const gltf_import_ptr import = begin_gltf_import(path, options, false);
        return finish_gltf_import(*import, cache, materials);
    }
} // namespace rendering_engine
