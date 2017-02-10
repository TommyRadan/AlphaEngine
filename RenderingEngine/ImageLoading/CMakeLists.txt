cmake_minimum_required(VERSION 3.6)
project(ImageLoading)

add_subdirectory(libjpeg)
add_subdirectory(libpng)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(IMAGELOADING_SOURCE_FILES
        Image.cpp
        Image.hpp
        Exporters/BmpExporter.hpp
        Exporters/BmpExporter.cpp
        Exporters/JpegExporter.hpp
        Exporters/JpegExporter.cpp
        Exporters/PngExporter.hpp
        Exporters/PngExporter.cpp
        Importers/BmpImporter.hpp
        Importers/BmpImporter.cpp
        Importers/JpegImporter.hpp
        Importers/JpegImporter.cpp
        Importers/PngImporter.hpp
        Importers/PngImporter.cpp)

# Main lib
add_library(ImageLoading STATIC ${IMAGELOADING_SOURCE_FILES})

# Add Dependencies
add_dependencies(ImageLoading libjpeg)
add_dependencies(ImageLoading libpng)
