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

/**
 * @file placeholder_component.hpp
 * @brief Component that carries the scene-file entries a load could not
 *        turn back into components.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace runtime
{
    /**
     * @brief Keeps, verbatim, the component entries of a scene file that a
     *        load could not restore, so the next save writes them back.
     *
     * A scene save writes a placeholder where a component has no data form
     * (a renderable built in code, a mesh from a private upload, a type with
     * no registered name); a load meets entries it cannot rebuild (an
     * unknown type, a mesh that no longer resolves). Either way the entry is
     * parked here, in its JSON text together with its position among the
     * node's component entries, rather than dropped — so saving the loaded
     * scene reproduces the file, and nothing a later build could restore is
     * lost. It does nothing at runtime; the serializer (scene_serializer.hpp)
     * is the only reader and writer. Plain data: copied by @c scene::clone.
     */
    struct placeholder_component
    {
        /** @brief One parked entry. */
        struct entry
        {
            /** @brief Index of the entry in the node's component list in the file. */
            std::size_t position{0};
            /** @brief The entry as compact JSON text. */
            std::string text;
        };

        /** @brief The parked entries, in file order. */
        std::vector<entry> entries;
    };
} // namespace runtime
