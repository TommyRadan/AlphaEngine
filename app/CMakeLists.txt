# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(alpha_app PRIVATE
    application.cpp
    application.hpp
    project.cpp
    project.hpp
)

# The stock entry point, built with the game modules (see external/).
target_sources(alpha_game PRIVATE
    main.cpp
)
