/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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

/**
 * @file singleton.hpp
 * @brief CRTP helper for process-wide singletons.
 */

#pragma once

/**
 * @brief Meyers-style singleton base, used via CRTP (e.g. @c struct foo : singleton<foo>).
 *
 * The instance is a function-local @c static, so construction is thread-safe
 * under C++11 rules but other member access is not — subclasses must document
 * their own thread-safety. Copy and assignment are deleted to prevent
 * accidental duplication of the singleton state.
 *
 * @tparam T The derived class being made into a singleton.
 */
template<typename T>
class singleton
{
public:
    /**
     * @brief Returns the singleton instance, constructing it on first call.
     * @return Reference to the single instance of @p T.
     */
    static T& get_instance()
    {
        static T instance;
        return instance;
    }

    singleton(const singleton&) = delete;
    singleton& operator=(const singleton&) = delete;

protected:
    singleton() {}
    virtual ~singleton() {}
};
