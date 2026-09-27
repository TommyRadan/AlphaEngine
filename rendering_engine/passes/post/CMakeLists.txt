# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(AlphaEngine PRIVATE
    auto_exposure_pass.cpp
    auto_exposure_pass.hpp
    bloom_pass.cpp
    bloom_pass.hpp
    fullscreen_triangle.hpp
    fxaa_pass.cpp
    fxaa_pass.hpp
    motion_blur_pass.cpp
    motion_blur_pass.hpp
    taa_pass.cpp
    taa_pass.hpp
    tonemap_pass.cpp
    tonemap_pass.hpp
    velocity_pass.cpp
    velocity_pass.hpp
    volumetric_fog_pass.cpp
    volumetric_fog_pass.hpp
)
