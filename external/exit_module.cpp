// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file exit_module.cpp
 * @brief Quits the engine when Escape is pressed.
 *
 * Quitting is a game-wide concern that belongs to no object in the world, so
 * this module spawns nothing: its bootstrap subscribes a listener to the event
 * bus for the bus's lifetime.
 */

#include "api/game_module.hpp"

#include <core/event_engine.hpp>
#include <runtime/engine.hpp>

GAME_MODULE()
{
    core::event_bus& events = *runtime::current_engine().events;
    events
        .subscribe<core::key_down>(
            [](const core::key_down& event)
            {
                if (event.m_key_code == core::key_code::escape)
                {
                    runtime::current_engine().events->emit<core::quit_requested>();
                }
            })
        .release();
}
