cmake_minimum_required(VERSION 3.6)
project(zlib C)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(ZLIB_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/crc32.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/deflate.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/inffast.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/inflate.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/inftrees.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/trees.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/zutil.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/inffixed.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/zconf.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/zlib.h
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/adler32.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/compress.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/crc32.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/deflate.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/inffast.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/inflate.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/inftrees.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/trees.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/uncompr.c
        ${CMAKE_SOURCE_DIR}/Utilities/zlib/zutil.c)

add_library(zlib STATIC ${ZLIB_SOURCE_FILES})
set_target_properties(zlib PROPERTIES PREFIX "")
