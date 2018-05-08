#pragma once

#include <RenderingEngine/Mesh/Vertex.hpp>

#include <vector>
#include <string>

namespace RenderingEngine
{
    std::vector<Vertex> LoadOBJ(const std::string& filename);
}