/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Opaque handle to a camera owned by this facade.
 *
 * Ids are generational: the low 32 bits name the slot the camera occupies and
 * the high 32 bits that slot's generation, which is bumped every time the slot
 * is freed. A destroyed camera's id therefore never resolves again, even once
 * a later @ref create_camera has reused its slot, so a stale id is rejected
 * (with a warning) rather than silently addressing another camera.
 * @ref invalid_camera_id never names a camera.
 */
using camera_id = std::uint64_t;

constexpr camera_id invalid_camera_id = 0;

/** @brief Packs a slot @p index and @p generation into a @ref camera_id. */
constexpr camera_id make_camera_id(std::uint32_t index, std::uint32_t generation) noexcept
{
    return (static_cast<camera_id>(generation) << 32u) | static_cast<camera_id>(index);
}

/** @brief The slot index carried by @p id (its low 32 bits). */
constexpr std::uint32_t camera_id_index(camera_id id) noexcept
{
    return static_cast<std::uint32_t>(id & 0xffffffffu);
}

/** @brief The slot generation carried by @p id (its high 32 bits). */
constexpr std::uint32_t camera_id_generation(camera_id id) noexcept
{
    return static_cast<std::uint32_t>(id >> 32u);
}

enum class camera_type
{
    unknown,
    orthographic,
    perspective
};

/**
 * @brief Creates a camera of @p type and returns its id.
 *
 * @p type must be @c orthographic or @c perspective; @c unknown is not a
 * camera type, so it is rejected with a warning and @ref invalid_camera_id
 * is returned.
 */
camera_id create_camera(camera_type type);

/**
 * @brief Destroys the camera named by @p id.
 *
 * If it is the attached camera it is detached first, so the renderer never
 * observes a dangling current camera.
 */
void destroy_camera(camera_id id);

/** @brief Type of the camera named by @p id, or @c unknown for a bad id. */
camera_type get_camera_type(camera_id id);

// Every call that takes a camera_id logs a warning and does nothing when the
// id does not name a live camera. The getters report that through their
// return value: true with the out-params filled on success, false with the
// out-params left untouched otherwise. Out-params must not be null.
//
// The rotation is the camera's forward direction vector (it looks at
// position + rotation); it need not be unit length.

void set_camera_pos(camera_id id, float px, float py, float pz);
bool get_camera_pos(camera_id id, float* px, float* py, float* pz);

void set_camera_rot(camera_id id, float rx, float ry, float rz);
bool get_camera_rot(camera_id id, float* rx, float* ry, float* rz);

/** @brief Destroys every camera this facade owns, detaching the attached one. */
void destroy_all_cameras();
std::size_t get_number_of_cameras();

void attach_camera(camera_id id);
void detach_camera();
bool is_camera_attached(camera_id id);
bool is_any_camera_attached();
