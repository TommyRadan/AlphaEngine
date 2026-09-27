target_sources(AlphaEngine PRIVATE
    event.hpp
    event_engine.cpp
    event_engine.hpp
    hash.hpp
    jobs.cpp
    jobs.hpp
    log.cpp
    log.hpp
    pool.hpp
    settings.cpp
    settings.hpp
    settings_parse.cpp
    settings_parse.hpp
    string_id.cpp
    string_id.hpp
    subscription.cpp
    subscription.hpp
    time.cpp
    time.hpp
    version.cpp
    version.hpp
)

add_subdirectory(math)
add_subdirectory(platform)
add_subdirectory(vfs)
