cmake_minimum_required(VERSION 3.6)
project(zlib C)

set(ZLIB_LIB_NAME Zlib)

set(ZLIB_SOURCE_FILES
        RenderingEngine/zlib/adler32.c
        RenderingEngine/zlib/compress.c
        RenderingEngine/zlib/crc32.c
        RenderingEngine/zlib/deflate.c
        RenderingEngine/zlib/inffast.c
        RenderingEngine/zlib/inflate.c
        RenderingEngine/zlib/inftrees.c
        RenderingEngine/zlib/trees.c
        RenderingEngine/zlib/uncompr.c
        RenderingEngine/zlib/zutil.c)

add_library(${ZLIB_LIB_NAME} STATIC ${ZLIB_SOURCE_FILES})
