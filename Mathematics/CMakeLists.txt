cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.Mathematics)

set(MATH_LIB_NAME Math)

set(MATH_SOURCE_FILES
    Mathematics/Matrix3.cpp
    Mathematics/Matrix4.cpp
    Mathematics/Vector2.cpp
    Mathematics/Vector3.cpp
    Mathematics/Vector4.cpp)

add_library(${MATH_LIB_NAME} STATIC ${MATH_SOURCE_FILES})
