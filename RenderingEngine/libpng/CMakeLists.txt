cmake_minimum_required(VERSION 3.6)
project(libpng C)

set(PNG_LIB_NAME PNG)

set(PNG_SOURCE_FILES
        RenderingEngine/libpng/png.c
        RenderingEngine/libpng/pngerror.c
        RenderingEngine/libpng/pnggccrd.c
        RenderingEngine/libpng/pngget.c
        RenderingEngine/libpng/pngmem.c
        RenderingEngine/libpng/pngpread.c
        RenderingEngine/libpng/pngread.c
        RenderingEngine/libpng/pngrio.c
        RenderingEngine/libpng/pngrtran.c
        RenderingEngine/libpng/pngrutil.c
        RenderingEngine/libpng/pngset.c
        RenderingEngine/libpng/pngtrans.c
        RenderingEngine/libpng/pngvcrd.c
        RenderingEngine/libpng/pngwio.c
        RenderingEngine/libpng/pngwrite.c
        RenderingEngine/libpng/pngwtran.c
        RenderingEngine/libpng/pngwutil.c)

add_library(${PNG_LIB_NAME} STATIC ${PNG_SOURCE_FILES})
