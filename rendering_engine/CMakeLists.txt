target_sources(AlphaEngine PRIVATE
    gpu_profiler.cpp
    gpu_profiler.hpp
    post_settings.hpp
    render_stats.hpp
    renderer.cpp
    renderer.hpp
    sdl_input.cpp
    sdl_input.hpp
    window.cpp
    window.hpp
)

add_subdirectory(animation)
add_subdirectory(assets)
add_subdirectory(camera)
add_subdirectory(editor)
add_subdirectory(gpu)
add_subdirectory(lighting)
add_subdirectory(materials)
add_subdirectory(passes)
add_subdirectory(renderables)
