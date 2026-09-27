// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/backend/opengl/gl_program_cache.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <core/hash.hpp>
#include <core/log.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
        // The header ahead of each stored binary. Naturally aligned
        // fields, so the struct has no padding and is written and read
        // as bytes (the engine only targets little-endian hosts).
        struct blob_header
        {
            uint32_t magic{0};
            uint32_t version{0};
            uint64_t key{0};
            uint32_t format{0};
            uint32_t length{0};
            uint64_t digest{0};
        };
        static_assert(sizeof(blob_header) == 32, "the header is written as raw bytes");

        // "AEGP" read as a little-endian word.
        constexpr uint32_t blob_magic = 0x50474541u;
        // Bumped whenever the header or the key inputs change shape.
        constexpr uint32_t blob_version = 1;
        constexpr std::string_view key_tag = "alphaengine-gl-program-1";

        // Every program is specialised the same way today: entry point
        // "main" and no specialisation constants (gl_device_shader.cpp).
        // Both are folded into the key so a future change to either
        // cannot serve a stale binary.
        constexpr std::string_view entry_point = "main";
        constexpr uint32_t specialization_constant_count = 0;

        std::string gl_string(GLenum name)
        {
            const auto* text = reinterpret_cast<const char*>(glGetString(name));
            return text != nullptr ? std::string{text} : std::string{};
        }

        uint64_t digest_of(const std::vector<char>& bytes)
        {
            return core::fnv1a_64(std::string_view{bytes.data(), bytes.size()});
        }
    } // namespace

    void gl_program_cache::init()
    {
        m_enabled = false;
        m_formats.clear();
        m_hits = 0;
        m_misses = 0;
        m_refused = 0;
        m_stored = 0;

        GLint format_count = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &format_count);
        if (format_count <= 0)
        {
            LOG_INF("OpenGL program binary cache: off (the driver offers no program binary format)");
            return;
        }
        const std::filesystem::path& directory = gpu::shader_cache_directory();
        if (directory.empty())
        {
            LOG_INF("OpenGL program binary cache: off (the shader cache is disabled)");
            return;
        }

        m_formats.resize(static_cast<size_t>(format_count));
        glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, m_formats.data());

        // A binary is only valid for the driver build that produced it,
        // so the driver's identity leads every key.
        const std::string identity = gl_string(GL_VENDOR) + "|" + gl_string(GL_RENDERER) + "|" + gl_string(GL_VERSION);
        m_driver_key = core::fnv1a_64(identity);
        m_directory = directory;
        m_enabled = true;
        LOG_INF("OpenGL program binary cache: %s (%i binary format(s))", m_directory.string().c_str(), format_count);
    }

    void gl_program_cache::quit()
    {
        if (m_enabled)
        {
            LOG_INF("OpenGL program binary cache: %u loaded, %u linked (%u stored), %u refused by the driver",
                    m_hits,
                    m_misses,
                    m_stored,
                    m_refused);
        }
        m_enabled = false;
        m_formats.clear();
        m_directory.clear();
    }

    bool gl_program_cache::enabled() const noexcept
    {
        return m_enabled;
    }

    uint64_t gl_program_cache::program_key(const std::vector<gl_program_stage>& stages) const
    {
        core::fnv1a_64_hasher hasher;
        hasher.mix(key_tag);
        hasher.mix_value(m_driver_key);
        for (const gl_program_stage& stage : stages)
        {
            hasher.mix_value(static_cast<int>(stage.stage));
            hasher.mix_value(stage.spirv_digest);
            hasher.mix(entry_point);
            hasher.mix_value(specialization_constant_count);
        }
        return hasher.value();
    }

    std::filesystem::path gl_program_cache::file_for(uint64_t key) const
    {
        char name[48];
        std::snprintf(name, sizeof(name), "gl_program_%016llx.bin", static_cast<unsigned long long>(key));
        return m_directory / name;
    }

    bool gl_program_cache::load(GLuint program, uint64_t key)
    {
        if (!m_enabled)
        {
            return false;
        }

        // Anything short of a well-formed blob for exactly this key, in
        // a format the driver still lists, is a miss: nothing reaches
        // glProgramBinary unless it is what store() wrote for this key.
        std::ifstream in{file_for(key), std::ios::binary | std::ios::ate};
        if (!in)
        {
            ++m_misses;
            return false;
        }
        const std::streamoff file_size = in.tellg();
        blob_header header{};
        in.seekg(0);
        const bool header_ok =
            file_size >= static_cast<std::streamoff>(sizeof(blob_header)) &&
            in.read(reinterpret_cast<char*>(&header), sizeof(header)) && header.magic == blob_magic &&
            header.version == blob_version && header.key == key && header.length > 0 &&
            static_cast<uint64_t>(file_size) == sizeof(blob_header) + uint64_t{header.length} &&
            std::find(m_formats.begin(), m_formats.end(), static_cast<GLint>(header.format)) != m_formats.end();
        if (!header_ok)
        {
            ++m_misses;
            return false;
        }
        std::vector<char> binary(header.length);
        if (!in.read(binary.data(), static_cast<std::streamsize>(binary.size())) || digest_of(binary) != header.digest)
        {
            ++m_misses;
            return false;
        }

        glProgramBinary(
            program, static_cast<GLenum>(header.format), binary.data(), static_cast<GLsizei>(binary.size()));
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            // Drivers refuse binaries from another build of themselves;
            // the caller links from source and store() replaces the file.
            ++m_refused;
            ++m_misses;
            LOG_DBG("OpenGL program binary cache: the driver refused %s; linking instead",
                    file_for(key).string().c_str());
            return false;
        }
        ++m_hits;
        return true;
    }

    void gl_program_cache::store(GLuint program, uint64_t key)
    {
        if (!m_enabled)
        {
            return;
        }
        GLint length = 0;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
        if (length <= 0)
        {
            return;
        }
        std::vector<char> binary(static_cast<size_t>(length));
        GLsizei written = 0;
        GLenum format = GL_NONE;
        glGetProgramBinary(program, length, &written, &format, binary.data());
        if (written <= 0)
        {
            return;
        }
        binary.resize(static_cast<size_t>(written));

        blob_header header{};
        header.magic = blob_magic;
        header.version = blob_version;
        header.key = key;
        header.format = static_cast<uint32_t>(format);
        header.length = static_cast<uint32_t>(binary.size());
        header.digest = digest_of(binary);

        const std::filesystem::path file = file_for(key);
        std::filesystem::path temp = file;
        temp += ".tmp";
        {
            std::ofstream out{temp, std::ios::binary | std::ios::trunc};
            if (!out.write(reinterpret_cast<const char*>(&header), sizeof(header)) ||
                !out.write(binary.data(), static_cast<std::streamsize>(binary.size())))
            {
                LOG_WRN("OpenGL program binary cache: cannot write %s", temp.string().c_str());
                out.close();
                std::error_code ignored;
                std::filesystem::remove(temp, ignored);
                return;
            }
        }
        std::error_code error;
        std::filesystem::rename(temp, file, error);
        if (error)
        {
            LOG_WRN("OpenGL program binary cache: cannot move %s into place (%s)",
                    temp.string().c_str(),
                    error.message().c_str());
            std::filesystem::remove(temp, error);
            return;
        }
        ++m_stored;
    }
} // namespace rendering_engine::gpu::backend::opengl
