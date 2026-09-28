# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(alpha_render PRIVATE
    ambient_light.cpp
    ambient_light.hpp
    directional_light.cpp
    directional_light.hpp
    environment_probe.cpp
    environment_probe.hpp
    light.cpp
    light.hpp
    lights_ubo.cpp
    lights_ubo.hpp
    point_light.cpp
    point_light.hpp
    spot_light.cpp
    spot_light.hpp
)
