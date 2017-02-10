cmake_minimum_required(VERSION 3.6)
project(Mathematics)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(MATH_SOURCE_FILES
        Matrix3.hpp
        Matrix4.hpp
        Vector2.hpp
        Vector3.hpp
        Vector4.hpp
        Math.hpp
        Util.hpp
        Matrix3.cpp
        Matrix4.cpp
        Vector2.cpp
        Vector3.cpp
        Vector4.cpp)

add_library(Mathematics STATIC ${MATH_SOURCE_FILES})
