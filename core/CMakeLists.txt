target_sources(AlphaEngine PRIVATE
    event.hpp
    event_engine.cpp
    event_engine.hpp
    jobs.cpp
    jobs.hpp
    log.cpp
    log.hpp
    pool.hpp
    settings.cpp
    settings.hpp
    time.cpp
    time.hpp
    version.cpp
    version.hpp
)

add_subdirectory(math)
