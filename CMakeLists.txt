cmake_minimum_required(VERSION 3.20)

project(open_goldsrc_exe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(goldsrc
    src/main.cpp
    src/engine/BspMap.cpp
    src/engine/Console.cpp
    src/engine/Engine.cpp
    src/engine/FileSystem.cpp
    src/engine/LaunchOptions.cpp
    src/engine/Renderer.cpp
    src/engine/SoundSystem.cpp
    src/engine/StudioModel.cpp
    src/engine/WadFile.cpp
)

target_include_directories(goldsrc PRIVATE src)

if (WIN32)
    target_link_libraries(goldsrc PRIVATE opengl32 gdi32 user32 winmm)
endif()

if (MSVC)
    target_compile_options(goldsrc PRIVATE /W4 /permissive-)
else()
    target_compile_options(goldsrc PRIVATE -Wall -Wextra -Wpedantic)
endif()
