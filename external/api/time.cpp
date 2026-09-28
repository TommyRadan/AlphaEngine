// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "time.hpp"

#include <core/time.hpp>
#include <runtime/engine.hpp>

float get_current_fps()
{
    return runtime::current_engine().time->current_fps();
}

int get_frame_count()
{
    return runtime::current_engine().time->frame_count();
}

double get_delta_time()
{
    return runtime::current_engine().time->delta_time();
}

double get_total_time()
{
    return runtime::current_engine().time->total_time();
}
