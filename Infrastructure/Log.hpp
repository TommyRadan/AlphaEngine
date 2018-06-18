#pragma once

#include <Infrastructure/Loguru.hpp>

#define LOG_INIT(argc, argv) loguru::init(argc, argv)

#define LOG_INFO(...) LOG_F(INFO, __VA_ARGS__)
#define LOG_WARN(...) LOG_F(WARNING, __VA_ARGS__)
#define LOG_ERROR(...) LOG_F(ERROR, __VA_ARGS__)
#define LOG_FATAL(...) LOG_F(FATAL, __VA_ARGS__)

#define LOG_FUNCTION() LOG_SCOPE_FUNCTION(INFO)

namespace Infrastructure
{
    class Log
    {
        Log() = default;

    public:
        static Log* const GetInstance();

        void Init();
    };
}
