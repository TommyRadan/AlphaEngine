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

using CameraId = int;

enum class CameraType
{
    Unknown,
    Orthographic,
    Perspective
};

CameraId CreateCamera(CameraType type);
void DestroyCamera(CameraId id);

CameraType GetCameraType(CameraId id);

void SetCameraPos(CameraId id, float px, float py, float pz);
void GetCameraPos(CameraId id, float *px, float *py, float *pz);

void SetCameraRot(CameraId id, float rx, float ry, float rz);
void GetCameraRot(CameraId id, float *rx, float *ry, float *rz);

void DestroyAllCameras();
int GetNumberOfCameras();

void AttachCamera(CameraId id);
void DetachCamera();
bool IsCameraAttached(CameraId id);
bool IsAnyCameraAttached();
