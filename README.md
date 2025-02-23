# AlphaEngine

AlphaEngine is a simulation engine written in C++ that uses OpenGL for rendering. It is designed to be modular and extensible.

## Requirements

- CMake 3.16 or higher
- A C++20 compatible compiler
- SDL2
- GLEW
- OpenGL

## Building the Project

To build the project, follow these steps:

1. Clone the repository

2. Create a build directory:
    ```sh
    mkdir build
    ```

3. Navigate to the build directory:
    ```sh
    cd build
    ```

4. Run CMake from the build directory to generate the cache:
    ```sh
    cmake ..
    ```

5. Build the project:
    ```sh
    cmake --build .
    ```

6. Binary will be created in `Binaries` folder of the repository.

## Project Components

### Event Engine

The Event Engine handles all input events, such as keyboard and mouse events. It provides a flexible system for managing and responding to user input.

### Rendering Engine

The Rendering Engine is responsible for all graphical output. It uses OpenGL for rendering and supports various rendering techniques and optimizations. The engine is divided into several subcomponents:

- **Mesh**: Handles the creation and management of 3D models.
- **Renderables**: Manages objects that can be rendered, including 2D and 3D objects.
- **Renderers**: Contains different rendering strategies and techniques.
- **Camera**: Manages the camera view and projection.
- **OpenGL**: Contains OpenGL-specific code and utilities.

### Scene Graph

The Scene Graph manages the hierarchical organization of objects in the scene. It allows for efficient updates and rendering of complex scenes by organizing objects into a tree structure.

## License

AlphaEngine is licensed under the MIT License. See the [LICENSE](./LICENSE.txt) file for more details.
