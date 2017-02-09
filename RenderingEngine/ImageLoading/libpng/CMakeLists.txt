cmake_minimum_required(VERSION 3.6)
project(libpng)

set(PNG_LIB_NAME PNG)

include(Utilities/zlib/CMakeLists.txt)

set(PNG_SOURCE_FILES
        RenderingEngine/imageLoading/libpng/png.h
        RenderingEngine/imageLoading/libpng/pngconf.h
        RenderingEngine/imageLoading/libpng/png.c
        RenderingEngine/imageLoading/libpng/pngerror.c
        RenderingEngine/imageLoading/libpng/pnggccrd.c
        RenderingEngine/imageLoading/libpng/pngget.c
        RenderingEngine/imageLoading/libpng/pngmem.c
        RenderingEngine/imageLoading/libpng/pngpread.c
        RenderingEngine/imageLoading/libpng/pngread.c
        RenderingEngine/imageLoading/libpng/pngrio.c
        RenderingEngine/imageLoading/libpng/pngrtran.c
        RenderingEngine/imageLoading/libpng/pngrutil.c
        RenderingEngine/imageLoading/libpng/pngset.c
        RenderingEngine/imageLoading/libpng/pngtrans.c
        RenderingEngine/imageLoading/libpng/pngvcrd.c
        RenderingEngine/imageLoading/libpng/pngwio.c
        RenderingEngine/imageLoading/libpng/pngwrite.c
        RenderingEngine/imageLoading/libpng/pngwtran.c
        RenderingEngine/imageLoading/libpng/pngwutil.c)

add_library(${PNG_LIB_NAME} STATIC ${PNG_SOURCE_FILES})

add_dependencies(${PNG_LIB_NAME} ${ZLIB_LIB_NAME})
target_link_libraries(${PNG_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${ZLIB_LIB_NAME}.a)
