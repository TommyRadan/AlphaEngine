cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.Mathematics)

set(CMAKE_CXX_STANDARD 11)

set(MATH_SOURCE_FILES
    Mathematics/Matrix3.cpp
    Mathematics/Matrix4.cpp
    Mathematics/Vector2.cpp
    Mathematics/Vector3.cpp
    Mathematics/Vector4.cpp)

set(LIBRARY_OUTPUT_PATH ../Binaries/)
add_library(Math STATIC ${MATH_SOURCE_FILES})
