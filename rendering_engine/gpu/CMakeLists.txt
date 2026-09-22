target_sources(AlphaEngine PRIVATE
    bind_group.hpp
    buffer.hpp
    command_encoder.cpp
    command_encoder.hpp
    device.cpp
    device.hpp
    factory.cpp
    handle.hpp
    pipeline.hpp
    render_target.hpp
    shader.hpp
    shader_bindings.hpp
    shader_compiler.cpp
    shader_compiler.hpp
    shader_library.cpp
    shader_library.hpp
    texture.hpp
    types.hpp
)

add_subdirectory(backend)
