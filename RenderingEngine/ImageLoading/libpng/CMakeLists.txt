cmake_minimum_required(VERSION 3.6)
project(libpng)

include(${CMAKE_SOURCE_DIR}/Utilities/zlib/CMakeLists.txt)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(PNG_SOURCE_FILES
        png.h
        pngconf.h
        png.c
        pngerror.c
        pnggccrd.c
        pngget.c
        pngmem.c
        pngpread.c
        pngread.c
        pngrio.c
        pngrtran.c
        pngrutil.c
        pngset.c
        pngtrans.c
        pngvcrd.c
        pngwio.c
        pngwrite.c
        pngwtran.c
        pngwutil.c)

add_library(libpng STATIC ${PNG_SOURCE_FILES})
set_target_properties(libpng PROPERTIES PREFIX "")
add_dependencies(libpng zlib)
