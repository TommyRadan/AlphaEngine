cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.Mathematics)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(MATH_LIB_NAME Math)

set(MATH_SOURCE_FILES
        Mathematics/Matrix3.hpp
        Mathematics/Matrix4.hpp
        Mathematics/Vector2.hpp
        Mathematics/Vector3.hpp
        Mathematics/Vector4.hpp
        Mathematics/Math.hpp
        Mathematics/Util.hpp
        Mathematics/Matrix3.cpp
        Mathematics/Matrix4.cpp
        Mathematics/Vector2.cpp
        Mathematics/Vector3.cpp
        Mathematics/Vector4.cpp)

add_library(${MATH_LIB_NAME} STATIC ${MATH_SOURCE_FILES})
