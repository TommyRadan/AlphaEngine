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

#include <core/platform/dynamic_library.hpp>

#include <utility>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_loadso.h>

#include <core/platform/platform.hpp>

namespace core::platform
{
    namespace
    {
        SDL_SharedObject* as_sdl(void* handle)
        {
            return static_cast<SDL_SharedObject*>(handle);
        }
    } // namespace

    dynamic_library::dynamic_library(const std::filesystem::path& path)
    {
        open(path);
    }

    dynamic_library::~dynamic_library()
    {
        close();
    }

    dynamic_library::dynamic_library(dynamic_library&& other) noexcept
        : m_handle{std::exchange(other.m_handle, nullptr)}, m_last_error{std::move(other.m_last_error)}
    {
    }

    dynamic_library& dynamic_library::operator=(dynamic_library&& other) noexcept
    {
        if (this != &other)
        {
            close();
            m_handle = std::exchange(other.m_handle, nullptr);
            m_last_error = std::move(other.m_last_error);
        }
        return *this;
    }

    bool dynamic_library::open(const std::filesystem::path& path)
    {
        close();
        m_last_error.clear();
        m_handle = SDL_LoadObject(path_to_utf8(path).c_str());
        if (m_handle == nullptr)
        {
            const char* reason = SDL_GetError();
            m_last_error = reason != nullptr && *reason != '\0' ? reason : "could not load the library";
            return false;
        }
        return true;
    }

    void dynamic_library::close() noexcept
    {
        if (m_handle != nullptr)
        {
            SDL_UnloadObject(as_sdl(m_handle));
            m_handle = nullptr;
        }
    }

    bool dynamic_library::is_open() const noexcept
    {
        return m_handle != nullptr;
    }

    void* dynamic_library::symbol(const char* name)
    {
        if (m_handle == nullptr || name == nullptr)
        {
            m_last_error = "library is not open";
            return nullptr;
        }
        // SDL_FunctionPointer is a function pointer type; the symbol may be a
        // data object too, so it is handed back as a plain address.
        const SDL_FunctionPointer function = SDL_LoadFunction(as_sdl(m_handle), name);
        if (function == nullptr)
        {
            const char* reason = SDL_GetError();
            m_last_error = reason != nullptr && *reason != '\0' ? reason : "no such symbol";
            return nullptr;
        }
        m_last_error.clear();
        return reinterpret_cast<void*>(function);
    }

    const std::string& dynamic_library::last_error() const noexcept
    {
        return m_last_error;
    }
} // namespace core::platform
