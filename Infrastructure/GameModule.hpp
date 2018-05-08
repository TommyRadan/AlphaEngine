#pragma once

#include <EventEngine/EventEngine.hpp>
#include <EventEngine/Dispatch.hpp>

#define GAME_MODULE() bool ModuleInit()
#define REGISTER_CALLBACK(event, callback) EventEngine::Dispatch::GetInstance()->Register ## event ## Callback(callback);

static bool ModuleInit();

static bool initStatus = ModuleInit();
