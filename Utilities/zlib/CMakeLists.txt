cmake_minimum_required(VERSION 3.6)
project(zlib C)

set(ZLIB_LIB_NAME Zlib)

set(ZLIB_SOURCE_FILES
        Utilities/zlib/crc32.h
        Utilities/zlib/deflate.h
        Utilities/zlib/inffast.h
        Utilities/zlib/inflate.h
        Utilities/zlib/inftrees.h
        Utilities/zlib/trees.h
        Utilities/zlib/zutil.h
        Utilities/zlib/inffixed.h
        Utilities/zlib/zconf.h
        Utilities/zlib/zlib.h
        Utilities/zlib/adler32.c
        Utilities/zlib/compress.c
        Utilities/zlib/crc32.c
        Utilities/zlib/deflate.c
        Utilities/zlib/inffast.c
        Utilities/zlib/inflate.c
        Utilities/zlib/inftrees.c
        Utilities/zlib/trees.c
        Utilities/zlib/uncompr.c
        Utilities/zlib/zutil.c)

add_library(${ZLIB_LIB_NAME} STATIC ${ZLIB_SOURCE_FILES})
