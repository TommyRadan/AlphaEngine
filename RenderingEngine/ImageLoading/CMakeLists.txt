cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.RenderingEngine.ImageLoading)

set(IMAGELOADING_LIB_NAME ImageLoading)

include(RenderingEngine/ImageLoading/libjpeg/CMakeLists.txt)
include(RenderingEngine/ImageLoading/libpng/CMakeLists.txt)

set(IMAGELOADING_SOURCE_FILES
        RenderingEngine/ImageLoading/Image.cpp
        RenderingEngine/ImageLoading/Image.hpp
        RenderingEngine/ImageLoading/Exporters/BmpExporter.hpp
        RenderingEngine/ImageLoading/Exporters/BmpExporter.cpp
        RenderingEngine/ImageLoading/Exporters/JpegExporter.hpp
        RenderingEngine/ImageLoading/Exporters/JpegExporter.cpp
        RenderingEngine/ImageLoading/Exporters/PngExporter.hpp
        RenderingEngine/ImageLoading/Exporters/PngExporter.cpp
        RenderingEngine/ImageLoading/Importers/BmpImporter.hpp
        RenderingEngine/ImageLoading/Importers/BmpImporter.cpp
        RenderingEngine/ImageLoading/Importers/JpegImporter.hpp
        RenderingEngine/ImageLoading/Importers/JpegImporter.cpp
        RenderingEngine/ImageLoading/Importers/PngImporter.hpp
        RenderingEngine/ImageLoading/Importers/PngImporter.cpp)

# Main lib
add_library(${IMAGELOADING_LIB_NAME} STATIC ${IMAGELOADING_SOURCE_FILES})

# Add Dependencies
add_dependencies(${IMAGELOADING_LIB_NAME} ${JPEG_LIB_NAME})
target_link_libraries(${IMAGELOADING_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${JPEG_LIB_NAME}.a)

add_dependencies(${IMAGELOADING_LIB_NAME} ${PNG_LIB_NAME})
target_link_libraries(${IMAGELOADING_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${PNG_LIB_NAME}.a)
