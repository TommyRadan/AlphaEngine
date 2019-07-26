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

#include <API/Camera.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Cameras/PerspectiveCamera.hpp>

#include <vector>
#include <utility>
#include <algorithm>

struct CameraInfo
{
    CameraId id;
    CameraType type;
    RenderingEngine::Camera *handle;
};

static std::vector<struct CameraInfo *> cameraInfos;

static struct CameraInfo *getCameraInfo(CameraId id)
{
    struct CameraInfo *result { nullptr };

    for (auto cameraInfo : cameraInfos)
    {
        if (cameraInfo->id == id)
        {
            result = cameraInfo;
        }
    }

    return result;
}

CameraId getNewCameraId()
{
    CameraId id = 1;

    while (getCameraInfo(id) == nullptr)
    {
        id++;
    }

    return id;
}

CameraId CreateCamera(CameraType type)
{
    struct CameraInfo *cameraInfo { new struct CameraInfo };

    cameraInfo->id = getNewCameraId();
    cameraInfo->type = type;

    switch(type)
    {
        case CameraType::Unknown:
        case CameraType::Orthographic:
            // TODO: Implement Orthographic camera.
        case CameraType::Perspective:
            cameraInfo->handle = new RenderingEngine::PerspectiveCamera;
    }

    cameraInfos.push_back(cameraInfo);
    return cameraInfo->id;
}

void DestroyCamera(CameraId id)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return;

    auto it = std::find(begin(cameraInfos), end(cameraInfos), cameraInfo);

    delete cameraInfo->handle;

    cameraInfos.erase(std::find(begin(cameraInfos), end(cameraInfos), cameraInfo));
}

CameraType GetCameraType(CameraId id)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return CameraType::Unknown;

    return cameraInfo->type;
}

void SetCameraPos(CameraId id, float px, float py, float pz)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return;

    cameraInfo->handle->transform.SetPosition({px, py, pz});
}

void GetCameraPos(CameraId id, float *px, float *py, float *pz)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return;

    glm::vec3 pos { cameraInfo->handle->transform.GetPosition() };

    *px = pos.x;
    *py = pos.y;
    *pz = pos.z;
}

void SetCameraRot(CameraId id, float rx, float ry, float rz)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return;

    cameraInfo->handle->transform.SetRotation({rx, ry, rz});
}

void GetCameraRot(CameraId id, float *rx, float *ry, float *rz)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return;

    glm::vec3 rot { cameraInfo->handle->transform.GetRotation() };

    *rx = rot.x;
    *ry = rot.y;
    *rz = rot.z;
}

void DestroyAllCameras()
{
    for (auto& cameraInfo : cameraInfos)
    {
        delete cameraInfo->handle;
    }

    cameraInfos.clear();
}

int GetNumberOfCameras()
{
    return cameraInfos.size();
}

void AttachCamera(CameraId id)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return;

    cameraInfo->handle->Attach();
}

void DetachCamera()
{
    auto currentCamera = RenderingEngine::Context::GetInstance()->GetCurrentCamera();

    if (currentCamera == nullptr) return;

    currentCamera->Detach();
}

bool IsCameraAttached(CameraId id)
{
    auto cameraInfo = getCameraInfo(id);

    if (cameraInfo == nullptr) return false;

    return (RenderingEngine::Context::GetInstance()->GetCurrentCamera()== cameraInfo->handle);
}

bool IsAnyCameraAttached()
{
    return (RenderingEngine::Context::GetInstance()->GetCurrentCamera() != nullptr);
}
