target_sources(AlphaEngine PRIVATE
    render_stats.hpp
    rendering_engine.cpp
    rendering_engine.hpp
    window.cpp
    window.hpp
)

add_subdirectory(assets)
add_subdirectory(camera)
add_subdirectory(debug)
add_subdirectory(debug_ui)
add_subdirectory(gpu)
add_subdirectory(ibl)
add_subdirectory(lighting)
add_subdirectory(materials)
add_subdirectory(mesh)
add_subdirectory(passes)
add_subdirectory(render_graph)
add_subdirectory(renderables)
add_subdirectory(util)
