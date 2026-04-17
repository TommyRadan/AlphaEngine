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
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/rendering_engine.hpp>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

struct camera_info
{
    camera_id id;
    camera_type type;
    std::unique_ptr<rendering_engine::camera> handle;
};

static std::vector<std::unique_ptr<camera_info>> camera_infos;

static camera_info* get_camera_info(camera_id id)
{
    for (auto& info : camera_infos)
    {
        if (info->id == id)
        {
            return info.get();
        }
    }

    return nullptr;
}

camera_id get_new_camera_id()
{
    camera_id id = 1;

    while (get_camera_info(id) != nullptr)
    {
        id++;
    }

    return id;
}

camera_id create_camera(camera_type type)
{
    auto info = std::make_unique<camera_info>();

    info->id = get_new_camera_id();
    info->type = type;

    switch (type)
    {
    case camera_type::unknown:
    case camera_type::orthographic:
        // TODO: Implement Orthographic camera.
    case camera_type::perspective:
        info->handle = std::make_unique<rendering_engine::perspective_camera>();
    }

    camera_id id = info->id;
    camera_infos.push_back(std::move(info));
    return id;
}

void destroy_camera(camera_id id)
{
    auto it = std::find_if(begin(camera_infos),
                           end(camera_infos),
                           [id](const std::unique_ptr<camera_info>& info) { return info->id == id; });

    if (it == end(camera_infos))
        return;

    camera_infos.erase(it);
}

camera_type get_camera_type(camera_id id)
{
    auto camera_info = get_camera_info(id);

    if (camera_info == nullptr)
        return camera_type::unknown;

    return camera_info->type;
}

void set_camera_pos(camera_id id, float px, float py, float pz)
{
    auto camera_info = get_camera_info(id);

    if (camera_info == nullptr)
        return;

    camera_info->handle->transform.set_position({px, py, pz});
    camera_info->handle->invalidate_view_matrix();
}

void get_camera_pos(camera_id id, float* px, float* py, float* pz)
{
    auto camera_info = get_camera_info(id);

    if (camera_info == nullptr)
        return;

    glm::vec3 pos{camera_info->handle->transform.get_position()};

    *px = pos.x;
    *py = pos.y;
    *pz = pos.z;
}

void set_camera_rot(camera_id id, float rx, float ry, float rz)
{
    auto camera_info = get_camera_info(id);

    if (camera_info == nullptr)
        return;

    camera_info->handle->transform.set_rotation({rx, ry, rz});
    camera_info->handle->invalidate_view_matrix();
}

void get_camera_rot(camera_id id, float* rx, float* ry, float* rz)
{
    auto camera_info = get_camera_info(id);

    if (camera_info == nullptr)
        return;

    glm::vec3 rot{camera_info->handle->transform.get_rotation()};

    *rx = rot.x;
    *ry = rot.y;
    *rz = rot.z;
}

void destroy_all_cameras()
{
    camera_infos.clear();
}

int get_number_of_cameras()
{
    return camera_infos.size();
}

void attach_camera(camera_id id)
{
    auto camera_info = get_camera_info(id);

    if (camera_info == nullptr)
        return;

    camera_info->handle->attach();
}

void detach_camera()
{
    auto current_camera = rendering_engine::camera::get_current_camera();

    if (current_camera == nullptr)
        return;

    current_camera->detach();
}

bool is_camera_attached(camera_id id)
{
    auto camera_info = get_camera_info(id);

    if (camera_info == nullptr)
        return false;

    return (rendering_engine::camera::get_current_camera() == camera_info->handle.get());
}

bool is_any_camera_attached()
{
    return (rendering_engine::camera::get_current_camera() != nullptr);
}
