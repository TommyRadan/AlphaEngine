#define LOGURU_IMPLEMENTATION 1
#include <Infrastructure/Loguru.hpp>
#include <Infrastructure/Log.hpp>
#include <Infrastructure/Version.hpp>

Infrastructure::Log* const Infrastructure::Log::GetInstance()
{
    static Log* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Log();
    }

    return instance;
}

void Infrastructure::Log::Init()
{
#ifdef _DEBUG
    loguru::add_file("everything.log", loguru::Append, loguru::Verbosity_MAX);
    loguru::add_file("latest_readable.log", loguru::Truncate, loguru::Verbosity_INFO);
#else
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::g_colorlogtostderr = false;
#endif

    LOG_INFO("AlphaEngine v%s starting ...", Infrastructure::Version::GetVersion().c_str());
}
