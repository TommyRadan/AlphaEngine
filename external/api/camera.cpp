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

#include <external/api/camera.hpp>

#include <memory>
#include <utility>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <core/pool.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>

namespace
{
    struct camera_entry
    {
        camera_type type{camera_type::unknown};
        std::unique_ptr<rendering_engine::camera> handle;
    };

    // Tag type so the facade's pool handles are distinct from any other pool's.
    struct camera_id_tag
    {
    };

    using camera_pool = core::pool<camera_entry, camera_id_tag>;
    using camera_handle = camera_pool::handle;

    // The pool is what makes ids generational: erasing a slot bumps its
    // generation, so an id minted before the erase never resolves again even
    // once the slot has been handed out to a later camera.
    camera_pool& cameras()
    {
        static camera_pool storage;
        return storage;
    }

    camera_id to_camera_id(camera_handle handle)
    {
        return make_camera_id(handle.index, handle.generation);
    }

    camera_handle to_camera_handle(camera_id id)
    {
        return camera_handle{camera_id_index(id), camera_id_generation(id)};
    }

    // Resolves @p id, warning on behalf of @p caller when it names no live camera.
    camera_entry* find_camera(camera_id id, const char* caller)
    {
        camera_entry* entry = cameras().get(to_camera_handle(id));
        if (entry == nullptr)
        {
            LOG_WRN("%s: camera id %llu does not name a live camera", caller, static_cast<unsigned long long>(id));
        }
        return entry;
    }

    bool is_current(const camera_entry& entry)
    {
        return rendering_engine::camera::get_current_camera() == entry.handle.get();
    }

    // Detaches the camera in @p entry if it is the attached one, so a camera
    // about to be freed is never left behind as the renderer's current camera.
    void detach_if_current(camera_entry& entry)
    {
        if (is_current(entry))
        {
            entry.handle->detach();
        }
    }
} // namespace

camera_id create_camera(camera_type type)
{
    camera_entry entry;
    entry.type = type;

    switch (type)
    {
    case camera_type::orthographic:
        entry.handle = std::make_unique<rendering_engine::orthographic_camera>();
        break;
    case camera_type::perspective:
        entry.handle = std::make_unique<rendering_engine::perspective_camera>();
        break;
    case camera_type::unknown:
        break;
    }

    if (entry.handle == nullptr)
    {
        LOG_WRN("create_camera: camera_type %d is not a creatable camera type", static_cast<int>(type));
        return invalid_camera_id;
    }

    return to_camera_id(cameras().insert(std::move(entry)));
}

void destroy_camera(camera_id id)
{
    camera_entry* entry = find_camera(id, "destroy_camera");
    if (entry == nullptr)
    {
        return;
    }

    detach_if_current(*entry);
    cameras().erase(to_camera_handle(id));
}

camera_type get_camera_type(camera_id id)
{
    const camera_entry* entry = find_camera(id, "get_camera_type");
    return entry != nullptr ? entry->type : camera_type::unknown;
}

void set_camera_pos(camera_id id, float px, float py, float pz)
{
    camera_entry* entry = find_camera(id, "set_camera_pos");
    if (entry == nullptr)
    {
        return;
    }

    entry->handle->transform.set_position({px, py, pz});
    entry->handle->invalidate_view_matrix();
}

bool get_camera_pos(camera_id id, float* px, float* py, float* pz)
{
    const camera_entry* entry = find_camera(id, "get_camera_pos");
    if (entry == nullptr)
    {
        return false;
    }

    const core::math::vec3 pos = entry->handle->transform.get_position();
    *px = pos.x;
    *py = pos.y;
    *pz = pos.z;
    return true;
}

void set_camera_rot(camera_id id, float rx, float ry, float rz)
{
    camera_entry* entry = find_camera(id, "set_camera_rot");
    if (entry == nullptr)
    {
        return;
    }

    entry->handle->transform.set_rotation({rx, ry, rz});
    entry->handle->invalidate_view_matrix();
}

bool get_camera_rot(camera_id id, float* rx, float* ry, float* rz)
{
    const camera_entry* entry = find_camera(id, "get_camera_rot");
    if (entry == nullptr)
    {
        return false;
    }

    const core::math::vec3 rot = entry->handle->transform.get_rotation();
    *rx = rot.x;
    *ry = rot.y;
    *rz = rot.z;
    return true;
}

void destroy_all_cameras()
{
    camera_pool& pool = cameras();
    // Erasing the slot an iterator sits on is allowed as long as it is not
    // dereferenced again before advancing (see core::pool).
    for (camera_pool::iterator it = pool.begin(); it != pool.end(); ++it)
    {
        detach_if_current(*it);
        pool.erase(it.handle());
    }
}

std::size_t get_number_of_cameras()
{
    return cameras().size();
}

void attach_camera(camera_id id)
{
    camera_entry* entry = find_camera(id, "attach_camera");
    if (entry == nullptr)
    {
        return;
    }

    entry->handle->attach();
}

void detach_camera()
{
    rendering_engine::camera* current = rendering_engine::camera::get_current_camera();
    if (current == nullptr)
    {
        return;
    }

    current->detach();
}

bool is_camera_attached(camera_id id)
{
    const camera_entry* entry = find_camera(id, "is_camera_attached");
    return entry != nullptr && is_current(*entry);
}

bool is_any_camera_attached()
{
    return rendering_engine::camera::get_current_camera() != nullptr;
}
