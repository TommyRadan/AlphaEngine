# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

# The CPU asset module: decoders and importers that turn files into plain
# data (images, font atlases, KTX2 levels, glTF documents, vertex records),
# with no dependency on the renderer, the GPU device or the runtime.
target_sources(alpha_assets PRIVATE
    color.hpp
    font.cpp
    font.hpp
    gltf_importer.cpp
    gltf_importer.hpp
    image.cpp
    image.hpp
    mesh_data.cpp
    mesh_data.hpp
    tangent.cpp
    tangent.hpp
    texture_decode.cpp
    texture_decode.hpp
    vertex.hpp
)
