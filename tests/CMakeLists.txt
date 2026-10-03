cmake_minimum_required(VERSION 3.20)
project(ImOkTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

# The newest tested ImGui: keep in sync with example/CMakeLists.txt. CI also builds v1.92.6, the
# minimum in imok.h's #error, and v1.92.9b-docking.
set(IMOK_IMGUI_TAG "v1.92.9b" CACHE STRING "Dear ImGui tag the tests build against")
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${IMOK_IMGUI_TAG}
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_executable(imok_tests
    test_main.cpp
    test_types.cpp
    test_srgb.cpp
    test_copy.cpp
    test_oklab.cpp
    test_oklch.cpp
    test_gamut.cpp
    test_gamut_clip.cpp
    test_luminance.cpp
    test_temperature.cpp
    test_tooltip.cpp
    test_intensity.cpp
    test_hex.cpp
    test_toe.cpp
    test_okhsv.cpp
    test_okhsl.cpp
    test_lerp.cpp
    test_draw.cpp
    test_dragdrop.cpp
    test_storage.cpp
    test_surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../imok_color.cpp
)
target_include_directories(imok_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
# SYSTEM: ImGui's header warnings are not ours
target_include_directories(imok_tests SYSTEM PRIVATE ${imgui_SOURCE_DIR})
target_link_libraries(imok_tests PRIVATE doctest::doctest)

add_library(imgui_for_tests STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_include_directories(imgui_for_tests SYSTEM PUBLIC ${imgui_SOURCE_DIR})

# Also builds imok.cpp and imok_demo.cpp with our warnings on GCC and Clang
add_executable(imok_widget_tests
        test_main.cpp
        test_widgets.cpp
        test_imgui_colors.cpp
        test_demo.cpp
        test_viewports.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../imok.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../imok_demo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../imok_color.cpp
)
target_include_directories(imok_widget_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(imok_widget_tests PRIVATE imgui_for_tests doctest::doctest)

# Needs a docking IMOK_IMGUI_TAG
option(IMOK_TEST_DOCKED "Run the widget tests in a docked window" OFF)
if(IMOK_TEST_DOCKED)
    target_compile_definitions(imok_widget_tests PRIVATE IMOK_TEST_DOCKED)
endif()

# ImOk's targets only. Keep in sync with example/CMakeLists.txt.
option(IMOK_WARNINGS_AS_ERRORS "Treat warnings in ImOk's own targets as errors (CI turns this on)" OFF)
foreach(target imok_tests imok_widget_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ${IMOK_WARNINGS_AS_ERRORS})
endforeach()

enable_testing()
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(imok_tests)
doctest_discover_tests(imok_widget_tests)