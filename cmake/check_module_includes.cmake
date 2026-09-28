# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

# Checks the #include lines of one module's sources against the modules
# it sees (see module_includes.cmake, which runs this script as a build
# step). CONFIG names the file module_includes.cmake generated for the
# module:
#
#   MODULE       the module's target name
#   SOURCE_ROOT  the source tree's root, the first-party include directory
#   VISIBLE      the modules it may include from, itself among them
#   OWNERS       module=directory pairs: which module owns which directory
#   SOURCES      the absolute paths of the module's sources and headers
#
# A quoted include is looked up beside the including file first, then
# under SOURCE_ROOT; an angle-bracket include under SOURCE_ROOT only. An
# include that names no file there (a standard or third-party header) is
# not a first-party one and is skipped. STAMP is touched when every include
# is allowed; otherwise each offending line is reported and the script
# fails.

cmake_minimum_required(VERSION 3.22)

include("${CONFIG}")

# The module that owns `path` (relative to SOURCE_ROOT), or empty.
function(owner_of path out)
    set(owner "")
    set(owner_length 0)
    foreach(pair IN LISTS OWNERS)
        string(FIND "${pair}" "=" split)
        string(SUBSTRING "${pair}" 0 ${split} module)
        math(EXPR directory_start "${split} + 1")
        string(SUBSTRING "${pair}" ${directory_start} -1 directory)
        string(LENGTH "${directory}" directory_length)
        string(FIND "${path}" "${directory}/" position)
        if(position EQUAL 0 AND directory_length GREATER owner_length)
            set(owner "${module}")
            set(owner_length ${directory_length})
        endif()
    endforeach()
    set(${out} "${owner}" PARENT_SCOPE)
endfunction()

# The 1-based number of the first line of `file` that is `line`.
function(line_number_of file line out)
    file(READ "${file}" content)
    string(FIND "${content}" "${line}" position)
    string(SUBSTRING "${content}" 0 ${position} before)
    string(REGEX MATCHALL "\n" newlines "${before}")
    list(LENGTH newlines count)
    math(EXPR number "${count} + 1")
    set(${out} ${number} PARENT_SCOPE)
endfunction()

list(JOIN VISIBLE ", " visible_text)
set(violations 0)
foreach(source IN LISTS SOURCES)
    file(STRINGS "${source}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]")
    get_filename_component(source_directory "${source}" DIRECTORY)
    foreach(line IN LISTS include_lines)
        string(REGEX MATCH "#[ \t]*include[ \t]*([<\"])([^>\"]+)" match "${line}")
        set(delimiter "${CMAKE_MATCH_1}")
        set(header "${CMAKE_MATCH_2}")

        set(resolved "")
        if(delimiter STREQUAL "\"" AND EXISTS "${source_directory}/${header}")
            set(resolved "${source_directory}/${header}")
        elseif(EXISTS "${SOURCE_ROOT}/${header}")
            set(resolved "${SOURCE_ROOT}/${header}")
        endif()
        if(resolved STREQUAL "" OR IS_DIRECTORY "${resolved}")
            continue()
        endif()
        cmake_path(NORMAL_PATH resolved)
        file(RELATIVE_PATH relative "${SOURCE_ROOT}" "${resolved}")
        if(relative MATCHES "^\\.\\./")
            continue()
        endif()

        owner_of("${relative}" owner)
        if(owner STREQUAL "" OR owner IN_LIST VISIBLE)
            continue()
        endif()

        line_number_of("${source}" "${line}" number)
        message(NOTICE "${source}:${number}: error: ${MODULE} may not include ${relative}: it belongs to ${owner}, "
                       "which ${MODULE} does not depend on (${MODULE} sees ${visible_text})")
        math(EXPR violations "${violations} + 1")
    endforeach()
endforeach()

if(violations GREATER 0)
    message(FATAL_ERROR "${MODULE}: ${violations} include(s) from outside its dependencies; "
                        "the module dependencies are stated in the root CMakeLists.txt")
endif()

file(TOUCH "${STAMP}")
