#
# Copyright (c) 2015-2026 Tomislav Radanovic
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# Script-mode helper behind the `format-check` / `format-fix` targets in the
# root CMakeLists.txt. Runs clang-format over the same files CI's clang-format
# job and scripts/check-style.{ps1,sh} cover: *.cpp / *.hpp / *.h under
# runtime/, core/, rendering_engine/ and external/. Vendored code and build
# output are never touched.
#
# The file list is enumerated here, at run time, rather than at configure
# time, so newly added files are picked up without re-configuring and the
# project's explicit-source-list rule for build targets is untouched.
#
# Invoked as:
#   cmake -DCLANG_FORMAT=<exe> -DSOURCE_ROOT=<repo> [-DFIX=ON]
#         -P cmake/clang_format.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT CLANG_FORMAT)
    message(FATAL_ERROR "clang_format.cmake: CLANG_FORMAT (path to clang-format) is required")
endif()
if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "clang_format.cmake: SOURCE_ROOT (repository root) is required")
endif()
if(NOT EXISTS "${SOURCE_ROOT}/.clang-format")
    message(FATAL_ERROR "clang_format.cmake: .clang-format not found at ${SOURCE_ROOT}")
endif()

set(source_dirs runtime core rendering_engine external)
set(files)
foreach(dir IN LISTS source_dirs)
    if(IS_DIRECTORY "${SOURCE_ROOT}/${dir}")
        file(GLOB_RECURSE dir_files
            "${SOURCE_ROOT}/${dir}/*.cpp"
            "${SOURCE_ROOT}/${dir}/*.hpp"
            "${SOURCE_ROOT}/${dir}/*.h")
        list(APPEND files ${dir_files})
    endif()
endforeach()
list(SORT files)
list(LENGTH files file_count)
if(file_count EQUAL 0)
    message(FATAL_ERROR "clang_format.cmake: no source files found under ${SOURCE_ROOT}")
endif()

if(FIX)
    set(mode_args -i)
    message(STATUS "clang-format: reformatting ${file_count} file(s) in place")
else()
    set(mode_args --dry-run -Werror)
    message(STATUS "clang-format: checking ${file_count} file(s)")
endif()

# Run in batches so the command line stays well under the platform limits
# (Windows caps a single command at ~32k characters).
set(batch_size 64)
set(failed FALSE)
set(index 0)
while(index LESS file_count)
    math(EXPR remaining "${file_count} - ${index}")
    if(remaining LESS batch_size)
        set(count ${remaining})
    else()
        set(count ${batch_size})
    endif()
    list(SUBLIST files ${index} ${count} batch)
    execute_process(
        COMMAND "${CLANG_FORMAT}" --style=file ${mode_args} -- ${batch}
        WORKING_DIRECTORY "${SOURCE_ROOT}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        set(failed TRUE)
    endif()
    math(EXPR index "${index} + ${count}")
endwhile()

if(failed)
    if(FIX)
        message(FATAL_ERROR "clang-format failed while reformatting")
    endif()
    message(FATAL_ERROR "clang-format found style issues; build the format-fix target (or run scripts/check-style.sh --fix) to apply them")
endif()

if(FIX)
    message(STATUS "clang-format: all files reformatted")
else()
    message(STATUS "clang-format: no style issues")
endif()
