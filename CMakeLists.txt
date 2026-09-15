cmake_minimum_required(VERSION 3.16)
project(ra2core CXX)

# 这个工程是 gamemd.exe（红色警戒 2：尤里的复仇）的 C++ 还原骨架，
# 目标是为后续的多核优化提供一个可编译、可测量、可回归的基线。
# 逆向分析产物在 db/ 与 docs/ 下，由 tools/ 里的 Python 脚本生成。

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(ra2core
    src/main.cpp
    src/map/Map.h
    src/map/Map.cpp
    src/ai/PathFinder.cpp
    src/ai/PathFinder.h
    src/engine/FrameQueue.cpp
    src/engine/FrameQueue.h
    src/engine/GameLoop.cpp
    src/engine/GameLoop.h
    src/threading/TaskSystem.cpp
    src/threading/TaskSystem.h
    src/io/FileSystem.cpp
    src/io/FileSystem.h
    src/core/Abstract.h
    src/core/Types.h
    src/core/Subsystems.h
    src/core/Subsystems.cpp
)

target_include_directories(ra2core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ra2core PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ra2core PRIVATE /W4 /EHsc)
    target_compile_definitions(ra2core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ra2core PRIVATE -Wall -Wextra)
endif()
