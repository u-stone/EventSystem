# Set the minimum required version of CMake and project details
cmake_minimum_required(VERSION 3.14)
project(NotifyCenter CXX)

# Set the C++ standard to 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Include Directories ---
# Add the 'include' directory to the include path
include_directories(include)


# --- Google Test Framework ---
# Enable testing for the project. Must be called before adding tests.
enable_testing()

# Force Google Test to use shared CRT (MD/MDd) to match the application
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Use FetchContent to download Google Test automatically if not found.
# We set FETCHCONTENT_BASE_DIR to a directory outside the build tree to cache
# the download, avoiding re-downloads on every clean build.
include(FetchContent)
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# For projects >= 3.11, set FETCHCONTENT_QUIET to FALSE to see progress
set(FETCHCONTENT_QUIET FALSE) 
FetchContent_GetProperties(googletest)
if(NOT googletest_POPULATED)
  FetchContent_Populate(googletest)
  add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
endif()


# --- Main Application ---
# Add the main application executable from the 'app' directory
add_executable(main_app app/main.cpp)


# --- Unit Tests ---
# Add the test executable from the 'test' directory
add_executable(test_event_system tests/EventSystem_test.cpp)

# Link the test executable against GTest
target_link_libraries(test_event_system GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_event_system)
