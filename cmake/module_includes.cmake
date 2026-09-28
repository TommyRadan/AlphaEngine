# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

# The include layering of the engine modules.
#
# First-party code includes a header by its path from the source root
# (<runtime/scene.hpp>), and that one include directory serves every
# target, so the include path alone would let any module reach any other.
# The layering is enforced by a build step instead: before a module
# compiles, check_module_includes.cmake reads the #include lines of its
# sources, finds the module that owns each first-party header they name,
# and fails the build when that module is one the including module does
# not see. A module sees itself and the modules it links, and through
# each of those the modules that one links PUBLIC, which is exactly the
# set whose usage requirements reach its compile. The link graph in the
# root CMakeLists.txt is the one statement of the layering; the check
# reads it back from the targets.

# Records that module `target` owns the headers under each directory
# given (relative to the source root). A header belongs to the module
# with the longest directory that contains it, so rendering_engine/gpu
# can belong to one module and the rest of rendering_engine to another.
function(alpha_module_directories target)
    set_property(TARGET ${target} PROPERTY ALPHA_MODULE_DIRECTORIES ${ARGN})
    set_property(GLOBAL APPEND PROPERTY ALPHA_MODULES ${target})
endfunction()

# Adds the include check of every recorded module: a <module>_include_check
# target that the module depends on, and that reruns whenever one of the
# module's sources changes. Call once, after every module's sources and
# links are in place.
function(alpha_add_module_include_checks)
    get_property(modules GLOBAL PROPERTY ALPHA_MODULES)

    set(owners "")
    foreach(module IN LISTS modules)
        get_target_property(directories ${module} ALPHA_MODULE_DIRECTORIES)
        foreach(directory IN LISTS directories)
            list(APPEND owners "${module}=${directory}")
        endforeach()
    endforeach()

    set(check_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/check_module_includes.cmake")
    set(check_directory "${CMAKE_BINARY_DIR}/module_includes")

    foreach(module IN LISTS modules)
        # The modules this one sees: its direct links, then the PUBLIC
        # links of each module reached. A PRIVATE link of a static library
        # shows up in its interface wrapped in $<LINK_ONLY:...> and, like
        # the third-party libraries, is never a module name, so it is not
        # followed.
        set(visible ${module})
        get_target_property(pending ${module} LINK_LIBRARIES)
        while(pending)
            list(POP_FRONT pending dependency)
            if(dependency IN_LIST modules AND NOT dependency IN_LIST visible)
                list(APPEND visible ${dependency})
                get_target_property(interface ${dependency} INTERFACE_LINK_LIBRARIES)
                if(interface)
                    list(APPEND pending ${interface})
                endif()
            endif()
        endwhile()

        get_target_property(source_directory ${module} SOURCE_DIR)
        get_target_property(target_sources ${module} SOURCES)
        set(sources "")
        foreach(source IN LISTS target_sources)
            if(NOT source MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl)$" OR source MATCHES "\\$<")
                continue()
            endif()
            if(NOT IS_ABSOLUTE "${source}")
                set(source "${source_directory}/${source}")
            endif()
            list(APPEND sources "${source}")
        endforeach()

        set(config "${check_directory}/${module}.cmake")
        set(stamp "${check_directory}/${module}.stamp")
        file(GENERATE OUTPUT "${config}" CONTENT
"set(MODULE \"${module}\")
set(SOURCE_ROOT \"${CMAKE_SOURCE_DIR}\")
set(VISIBLE \"${visible}\")
set(OWNERS \"${owners}\")
set(SOURCES \"${sources}\")
")
        add_custom_command(
            OUTPUT "${stamp}"
            COMMAND ${CMAKE_COMMAND} "-DCONFIG=${config}" "-DSTAMP=${stamp}" -P "${check_script}"
            DEPENDS ${sources} "${config}" "${check_script}"
            COMMENT "Checking the includes of ${module} against its dependencies"
            VERBATIM)
        add_custom_target(${module}_include_check DEPENDS "${stamp}")
        add_dependencies(${module} ${module}_include_check)
    endforeach()
endfunction()
