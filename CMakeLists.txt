cmake_minimum_required(VERSION 3.30)

project(RaylibGame LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

find_package(raylib REQUIRED)
find_package(flecs REQUIRED)
find_package(box2d REQUIRED)
find_package(tmxlite REQUIRED)
find_package(WebP REQUIRED)
find_package(pfr REQUIRED)
find_package(imgui REQUIRED)

FetchContent_Declare(
    rlimgui
    URL https://github.com/raylib-extras/rlImGui/archive/refs/tags/Raylib_6_0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(rlimgui)

add_library(rlimgui STATIC
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR})
target_link_libraries(rlimgui PUBLIC raylib imgui::imgui)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/modules/Camera.cpp
    src/modules/Console/Console.cpp
    src/modules/Console/ConsoleUI.cpp
    src/modules/Console/Register.cpp
    src/modules/Movement.cpp
    src/modules/Rendering.cpp
    src/modules/Physics.cpp
    src/modules/Stairs/Stairs.cpp

    src/modules/Map/Map.cpp
    src/modules/Map/MapCache.cpp
    src/modules/Map/MapLoader.cpp
    src/modules/Map/MapRendering.cpp

    src/modules/Character/Character.cpp
    src/modules/Character/CharacterAnimation.cpp
    src/modules/Character/CharacterCore.cpp
    src/modules/Character/CharacterPhysics.cpp
    src/modules/Character/CharacterRendering.cpp
    src/modules/Character/CharacterSprites.cpp

    src/modules/Tilemap/Tilemap.cpp
    src/modules/Tilemap/TilemapChunks.cpp
    src/modules/Tilemap/TilemapPhysics.cpp
    src/modules/Tilemap/TilemapTextures.cpp

    src/modules/Debug/DebugDraw.cpp
    src/modules/Debug/PhysicsDebugDraw.cpp
    src/modules/Debug/Screenshot.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib flecs::flecs_static box2d::box2d tmxlite::tmxlite libwebp::libwebp pfr::pfr imgui::imgui rlimgui)

if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE /ignore:4099)
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
else()
    set(ASSETS_PATH "${CMAKE_CURRENT_BINARY_DIR}/assets")
    file(COPY "${CMAKE_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${ASSETS_PATH}/"
)
