cmake_minimum_required(VERSION 3.14)
project(EventSystem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories for artifacts (Simplifies DLL handling on Windows)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Option to build shared library (Default: OFF for FetchContent friendliness, but ON for DLL testing)
option(BUILD_SHARED_LIBS "Build shared library" ON)
option(EVENTSYSTEM_BUILD_EXAMPLES "Build example applications" ON)
option(EVENTSYSTEM_BUILD_TESTS "Build unit tests" ON)

# Subdirectories
add_subdirectory(src)

if(EVENTSYSTEM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(EVENTSYSTEM_BUILD_TESTS)
    enable_testing()
    # GoogleTest Setup
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    include(FetchContent)
    set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
      DOWNLOAD_EXTRACT_TIMESTAMP true
    )
    FetchContent_MakeAvailable(googletest)
    
    add_subdirectory(tests)
endif()