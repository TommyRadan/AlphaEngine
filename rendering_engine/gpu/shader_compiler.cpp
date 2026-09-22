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

#include <rendering_engine/gpu/shader_compiler.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <SPIRV/GlslangToSpv.h>

#include <core/hash.hpp>
#include <core/log.hpp>
#include <rendering_engine/gpu/shader_library.hpp>

namespace rendering_engine::gpu
{
    namespace
    {
        // glslang requires a single process-wide init/finalize bracket.
        // The first compile triggers initialisation; the static dtor
        // tears it down at process exit. Cheaper than wiring this into
        // engine init/quit and avoids a module-ordering dependency.
        struct glslang_process_guard
        {
            glslang_process_guard()
            {
                glslang::InitializeProcess();
            }
            ~glslang_process_guard()
            {
                glslang::FinalizeProcess();
            }
        };

        void ensure_glslang_initialized()
        {
            static glslang_process_guard guard;
            (void)guard;
        }

        EShLanguage to_glslang_stage(shader_stage stage)
        {
            switch (stage)
            {
            case shader_stage::vertex:
                return EShLangVertex;
            case shader_stage::fragment:
                return EShLangFragment;
            case shader_stage::geometry:
                return EShLangGeometry;
            case shader_stage::tessellation_control:
                return EShLangTessControl;
            case shader_stage::tessellation_evaluation:
                return EShLangTessEvaluation;
            case shader_stage::compute:
                return EShLangCompute;
            }
            return EShLangVertex;
        }

        const char* stage_name(shader_stage stage)
        {
            switch (stage)
            {
            case shader_stage::vertex:
                return "vertex";
            case shader_stage::fragment:
                return "fragment";
            case shader_stage::geometry:
                return "geometry";
            case shader_stage::tessellation_control:
                return "tessellation control";
            case shader_stage::tessellation_evaluation:
                return "tessellation evaluation";
            case shader_stage::compute:
                return "compute";
            }
            return "unknown";
        }

        // The target environment every compile uses: Vulkan client + SPV
        // target produces SPIR-V usable both by the Vulkan backend
        // natively and by the OpenGL 4.6 backend through ARB_gl_spirv.
        // The input version (100) is the glslang convention for GLSL.
        constexpr int glsl_input_version = 100;
        constexpr glslang::EShTargetClientVersion client_version = glslang::EShTargetVulkan_1_0;
        constexpr glslang::EShTargetLanguageVersion target_version = glslang::EShTargetSpv_1_0;
        constexpr auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

        // Bumped whenever the cache blob format or the key inputs change
        // shape, so stale entries from an older engine are never read.
        constexpr std::string_view cache_format_tag = "alphaengine-spirv-cache-1";

        constexpr uint32_t spirv_magic = 0x07230203u;

        // -- #include resolution ---------------------------------------

        // Resolves #include "name" / <name> against the shader library,
        // so a shader includes "include/fog.glsl" by its library path
        // wherever it lives. Both quote forms end up here: glslang tries
        // includeLocal first for the quoted form, which is left at its
        // default (nothing), and then falls through to includeSystem.
        class library_includer final : public glslang::TShader::Includer
        {
        public:
            IncludeResult*
            includeSystem(const char* header_name, const char* /*includer_name*/, size_t /*depth*/) override
            {
                if (!shader_library::contains(header_name))
                {
                    return nullptr;
                }
                const std::string_view text = shader_library::source(header_name);
                return new IncludeResult{header_name, text.data(), text.size(), nullptr};
            }

            void releaseInclude(IncludeResult* result) override
            {
                delete result;
            }
        };

        // Text injected ahead of the source: the include extension the
        // includer needs, then one #define per option. glslang inserts
        // the preamble before the shader's own #version, which it allows
        // for exactly this purpose.
        std::string build_preamble(const shader_defines& defines)
        {
            std::string preamble = "#extension GL_GOOGLE_include_directive : enable\n";
            for (const auto& [name, value] : defines)
            {
                preamble += "#define " + name;
                if (!value.empty())
                {
                    preamble += " " + value;
                }
                preamble += "\n";
            }
            return preamble;
        }

        // -- cache key ---------------------------------------------------

        std::string_view trim_left(std::string_view text)
        {
            const std::size_t start = text.find_first_not_of(" \t");
            return start == std::string_view::npos ? std::string_view{} : text.substr(start);
        }

        // The include name on a "#include" line, or empty. A textual
        // scan rather than the preprocessor: it may over-approximate
        // (a commented-out or #if'd-out include still counts) but never
        // misses a file the real preprocessor would pull in, which is
        // what a conservative cache key needs.
        std::string_view include_name(std::string_view line)
        {
            line = trim_left(line);
            if (line.empty() || line.front() != '#')
            {
                return {};
            }
            line = trim_left(line.substr(1));
            constexpr std::string_view keyword = "include";
            if (line.substr(0, keyword.size()) != keyword)
            {
                return {};
            }
            line = trim_left(line.substr(keyword.size()));
            if (line.empty() || (line.front() != '"' && line.front() != '<'))
            {
                return {};
            }
            const char close = line.front() == '"' ? '"' : '>';
            const std::size_t end = line.find(close, 1);
            if (end == std::string_view::npos)
            {
                return {};
            }
            return line.substr(1, end - 1);
        }

        // Folds every library file text (transitively) includes into the
        // hasher, each once, in first-seen order.
        void mix_includes(std::string_view text, std::vector<std::string>& seen, core::fnv1a_64_hasher& hasher)
        {
            std::size_t pos = 0;
            while (pos < text.size())
            {
                std::size_t end = text.find('\n', pos);
                if (end == std::string_view::npos)
                {
                    end = text.size();
                }
                const std::string_view name = include_name(text.substr(pos, end - pos));
                pos = end + 1;
                if (name.empty() || std::find(seen.begin(), seen.end(), name) != seen.end() ||
                    !shader_library::contains(name))
                {
                    continue;
                }
                seen.emplace_back(name);
                const std::string_view included = shader_library::source(name);
                hasher.mix(name);
                hasher.mix("\0", 1);
                hasher.mix(included);
                hasher.mix("\0", 1);
                mix_includes(included, seen, hasher);
            }
        }

        uint64_t cache_key(std::string_view source, shader_stage stage, const std::string& preamble)
        {
            core::fnv1a_64_hasher hasher;
            hasher.mix(cache_format_tag);
            hasher.mix(glslang::GetGlslVersionString());
            hasher.mix_value(static_cast<int>(stage));
            hasher.mix_value(static_cast<int>(client_version));
            hasher.mix_value(static_cast<int>(target_version));
            hasher.mix_value(static_cast<int>(messages));
            hasher.mix(preamble);
            hasher.mix("\0", 1);
            hasher.mix(source);
            hasher.mix("\0", 1);
            std::vector<std::string> seen;
            mix_includes(source, seen, hasher);
            return hasher.value();
        }

        // -- disk cache --------------------------------------------------

        struct cache_state
        {
            bool resolved{false};
            std::filesystem::path directory;
            shader_cache_stats stats;
        };

        cache_state& cache()
        {
            static cache_state state;
            return state;
        }

        bool disables_cache(const char* value)
        {
            return value[0] == '\0' || std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0 ||
                   std::strcmp(value, "false") == 0;
        }

        // Makes sure the directory exists; on failure logs once and
        // disables the cache for the process.
        void prepare_cache_directory()
        {
            cache_state& state = cache();
            if (state.directory.empty())
            {
                return;
            }
            std::error_code error;
            std::filesystem::create_directories(state.directory, error);
            if (error || !std::filesystem::is_directory(state.directory, error))
            {
                LOG_WRN("Shader cache: cannot create %s (%s); caching disabled",
                        state.directory.string().c_str(),
                        error.message().c_str());
                state.directory.clear();
            }
        }

        void resolve_cache_directory()
        {
            cache_state& state = cache();
            if (state.resolved)
            {
                return;
            }
            state.resolved = true;

            if (const char* value = std::getenv("ALPHAENGINE_SHADER_CACHE"); value != nullptr)
            {
                if (disables_cache(value))
                {
                    LOG_INF("Shader cache: disabled by ALPHAENGINE_SHADER_CACHE");
                    return;
                }
                state.directory = std::filesystem::path{value};
            }
            else if (char* pref_path = SDL_GetPrefPath("AlphaEngine", "AlphaEngine"); pref_path != nullptr)
            {
                state.directory = std::filesystem::path{pref_path} / "shader_cache";
                SDL_free(pref_path);
            }
            else
            {
                LOG_WRN("Shader cache: SDL_GetPrefPath failed (%s); caching disabled", SDL_GetError());
                return;
            }

            prepare_cache_directory();
            if (!state.directory.empty())
            {
                LOG_INF("Shader cache: %s", state.directory.string().c_str());
            }
        }

        std::filesystem::path cache_file(uint64_t key)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "%016llx.spv", static_cast<unsigned long long>(key));
            return cache().directory / name;
        }

        // A cached blob for key, or empty. Anything that is not a
        // well-formed SPIR-V module is treated as a miss and overwritten.
        std::vector<uint32_t> load_cached(uint64_t key)
        {
            const std::filesystem::path file = cache_file(key);
            std::ifstream in{file, std::ios::binary | std::ios::ate};
            if (!in)
            {
                return {};
            }
            const std::streamoff size = in.tellg();
            if (size < static_cast<std::streamoff>(5 * sizeof(uint32_t)) || size % sizeof(uint32_t) != 0)
            {
                return {};
            }
            std::vector<uint32_t> words(static_cast<std::size_t>(size) / sizeof(uint32_t));
            in.seekg(0);
            if (!in.read(reinterpret_cast<char*>(words.data()), size) || words.front() != spirv_magic)
            {
                return {};
            }
            return words;
        }

        // Writes the blob next to its siblings through a temporary so a
        // crash mid-write never leaves a truncated .spv behind. Failure
        // only costs a recompile next launch, so it is logged, not thrown.
        void store_cached(uint64_t key, const std::vector<uint32_t>& words)
        {
            const std::filesystem::path file = cache_file(key);
            std::filesystem::path temp = file;
            temp += ".tmp";
            {
                std::ofstream out{temp, std::ios::binary | std::ios::trunc};
                if (!out.write(reinterpret_cast<const char*>(words.data()),
                               static_cast<std::streamsize>(words.size() * sizeof(uint32_t))))
                {
                    LOG_WRN("Shader cache: cannot write %s", temp.string().c_str());
                    return;
                }
            }
            std::error_code error;
            std::filesystem::rename(temp, file, error);
            if (error)
            {
                LOG_WRN("Shader cache: cannot move %s into place (%s)", temp.string().c_str(), error.message().c_str());
                std::filesystem::remove(temp, error);
            }
        }

        // -- compile -----------------------------------------------------

        [[noreturn]] void
        fail(const std::string& name, shader_stage stage, const char* what, const char* log, const char* debug_log)
        {
            std::string message = "shader '" + name + "' (" + stage_name(stage) + ") " + what;
            if (log != nullptr && log[0] != '\0')
            {
                message += ":\n";
                message += log;
            }
            if (debug_log != nullptr && debug_log[0] != '\0')
            {
                message += "\n";
                message += debug_log;
            }
            LOG_ERR("%s", message.c_str());
            throw std::runtime_error{message};
        }

        std::vector<uint32_t>
        run_glslang(std::string_view source, shader_stage stage, const std::string& preamble, const std::string& name)
        {
            const EShLanguage glslang_stage = to_glslang_stage(stage);

            glslang::TShader shader{glslang_stage};
            const char* source_data = source.data();
            const int source_length = static_cast<int>(source.size());
            const char* source_name = name.c_str();
            shader.setStringsWithLengthsAndNames(&source_data, &source_length, &source_name, 1);
            shader.setPreamble(preamble.c_str());

            shader.setEnvInput(glslang::EShSourceGlsl, glslang_stage, glslang::EShClientVulkan, glsl_input_version);
            shader.setEnvClient(glslang::EShClientVulkan, client_version);
            shader.setEnvTarget(glslang::EShTargetSpv, target_version);

            library_includer includer;
            if (!shader.parse(GetDefaultResources(), glsl_input_version, false, messages, includer))
            {
                fail(name, stage, "failed to parse", shader.getInfoLog(), shader.getInfoDebugLog());
            }

            glslang::TProgram program;
            program.addShader(&shader);
            if (!program.link(messages))
            {
                fail(name, stage, "failed to link", program.getInfoLog(), program.getInfoDebugLog());
            }

            std::vector<uint32_t> spirv;
            glslang::SpvOptions options{};
            options.disableOptimizer = true;
            glslang::GlslangToSpv(*program.getIntermediate(glslang_stage), spirv, &options);

            if (spirv.empty())
            {
                fail(name, stage, "produced no SPIR-V", nullptr, nullptr);
            }
            return spirv;
        }
    } // namespace

    std::vector<uint32_t>
    compile_glsl_to_spirv(std::string_view source, shader_stage stage, const shader_compile_options& options)
    {
        ensure_glslang_initialized();
        resolve_cache_directory();

        const std::string name = options.name.empty() ? std::string{"<inline>"} : std::string{options.name};
        const std::string preamble = build_preamble(options.defines);

        cache_state& state = cache();
        const bool cache_enabled = !state.directory.empty();
        const uint64_t key = cache_enabled ? cache_key(source, stage, preamble) : 0;

        if (cache_enabled)
        {
            std::vector<uint32_t> cached = load_cached(key);
            if (!cached.empty())
            {
                ++state.stats.hits;
                return cached;
            }
        }
        ++state.stats.misses;

        std::vector<uint32_t> spirv = run_glslang(source, stage, preamble, name);
        if (cache_enabled)
        {
            store_cached(key, spirv);
        }
        return spirv;
    }

    std::vector<uint32_t> compile_glsl_to_spirv(std::string_view source, shader_stage stage)
    {
        return compile_glsl_to_spirv(source, stage, shader_compile_options{});
    }

    std::vector<uint32_t>
    compile_library_shader(std::string_view path, shader_stage stage, const shader_defines& defines)
    {
        shader_compile_options options{};
        options.defines = defines;
        options.name = path;
        return compile_glsl_to_spirv(shader_library::source(path), stage, options);
    }

    std::vector<uint32_t> compile_library_shader(const shader_variant& variant, shader_stage stage)
    {
        return compile_library_shader(variant.path, stage, variant.defines);
    }

    void set_shader_cache_directory(const std::filesystem::path& directory)
    {
        cache_state& state = cache();
        state.resolved = true;
        state.directory = directory;
        prepare_cache_directory();
    }

    const std::filesystem::path& shader_cache_directory()
    {
        resolve_cache_directory();
        return cache().directory;
    }

    shader_cache_stats shader_cache_statistics()
    {
        return cache().stats;
    }
} // namespace rendering_engine::gpu
