target_sources(AlphaEngine PRIVATE
    rendering_engine.cpp
    rendering_engine.hpp
    window.cpp
    window.hpp
)

add_subdirectory(camera)
add_subdirectory(gpu)
add_subdirectory(materials)
add_subdirectory(mesh)
add_subdirectory(passes)
add_subdirectory(renderables)
add_subdirectory(util)
