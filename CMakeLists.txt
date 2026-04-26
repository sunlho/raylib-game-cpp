cmake_minimum_required(VERSION 3.30)

project(RaylibGame LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

set(LOCAL_RAYLIB_ROOT "E:/project/raylib-6.0_win64_msvc16" CACHE PATH "Path to local raylib installation")
set(LOCAL_RAYLIB_INCLUDE_DIR "${LOCAL_RAYLIB_ROOT}/include")
set(LOCAL_RAYLIB_IMPLIB "${LOCAL_RAYLIB_ROOT}/lib/raylib.lib")
set(LOCAL_RAYLIB_DLL "${LOCAL_RAYLIB_ROOT}/lib/raylib.dll")

if(NOT EXISTS "${LOCAL_RAYLIB_INCLUDE_DIR}/raylib.h")
    message(FATAL_ERROR "raylib headers not found: ${LOCAL_RAYLIB_INCLUDE_DIR}")
endif()

if(NOT EXISTS "${LOCAL_RAYLIB_IMPLIB}")
    message(FATAL_ERROR "raylib import library not found: ${LOCAL_RAYLIB_IMPLIB}")
endif()

if(NOT EXISTS "${LOCAL_RAYLIB_DLL}")
    message(FATAL_ERROR "raylib dll not found: ${LOCAL_RAYLIB_DLL}")
endif()

add_library(raylib SHARED IMPORTED)
set_target_properties(raylib PROPERTIES
    IMPORTED_IMPLIB "${LOCAL_RAYLIB_IMPLIB}"
    IMPORTED_LOCATION "${LOCAL_RAYLIB_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${LOCAL_RAYLIB_INCLUDE_DIR}"
)

find_package(flecs REQUIRED)
find_package(box2d REQUIRED)
find_package(tmxlite)
find_package(WebP REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/modules/Rendering.cpp
    src/modules/Tilemap.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib flecs::flecs_static box2d::box2d tmxlite::tmxlite libwebp::libwebp winmm)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LOCAL_RAYLIB_DLL}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)


if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
else()
    set(ASSETS_PATH "${CMAKE_CURRENT_BINARY_DIR}/assets")
    file(COPY "${CMAKE_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${ASSETS_PATH}/"
)
