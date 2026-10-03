cmake_minimum_required(VERSION 3.20)
project(ImOkExample LANGUAGES CXX)

# C++14, extensions off, so C++17 library use is an error
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
# Keep in sync with tests/CMakeLists.txt (the newest tested ImGui). IMOK_EXAMPLE_DOCKING builds
# the same release's docking branch, with docking and viewports on.
option(IMOK_EXAMPLE_DOCKING "Build against Dear ImGui's docking branch, docking and viewports on" OFF)
if(IMOK_EXAMPLE_DOCKING)
    set(IMOK_IMGUI_TAG v1.92.9b-docking)
else()
    set(IMOK_IMGUI_TAG v1.92.9b)
endif()
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${IMOK_IMGUI_TAG}
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

# SYSTEM: ImGui's header warnings are not ours
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi)

set(IMOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(imok STATIC
    ${IMOK_DIR}/imok_color.cpp
    ${IMOK_DIR}/imok.cpp
    ${IMOK_DIR}/imok_demo.cpp
)
target_include_directories(imok PUBLIC ${IMOK_DIR})
target_link_libraries(imok PUBLIC imgui)

# ImOk's targets only. Keep in sync with tests/CMakeLists.txt.
option(IMOK_WARNINGS_AS_ERRORS "Treat warnings in ImOk's own targets as errors (CI turns this on)" OFF)
# No /utf-8, as in users' builds: the sources are ASCII, imok_color.cpp has a BOM
if(MSVC)
    target_compile_options(imok PRIVATE /W4 /permissive-)
else()
    target_compile_options(imok PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
set_target_properties(imok PROPERTIES COMPILE_WARNING_AS_ERROR ${IMOK_WARNINGS_AS_ERRORS})

add_executable(example main.cpp)
target_link_libraries(example PRIVATE imok)