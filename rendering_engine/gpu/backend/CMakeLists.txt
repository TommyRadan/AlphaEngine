target_sources(AlphaEngine PRIVATE
    handle_pool.hpp
)

add_subdirectory(opengl)

if(ALPHAENGINE_BACKEND_VULKAN)
    add_subdirectory(vulkan)
endif()
