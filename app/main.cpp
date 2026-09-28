// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file main.cpp
 * @brief The entry point of the stock executable: runs the project named on
 *        the command line, in the environment or beside the executable, with
 *        every game module the executable links (see app/application.hpp).
 */

#include <app/application.hpp>

int main(int argc, char* argv[])
{
    app::application application{argc, argv};
    return application.run();
}
