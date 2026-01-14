add_library(EventSystem
    EventCenter.cpp
    MessageCenter.cpp
)

# Define the export macro when building as a shared library
target_compile_definitions(EventSystem PRIVATE EventSystem_EXPORTS)

# Include directories
target_include_directories(EventSystem
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Alias for easy usage
add_library(eventsystem::eventsystem ALIAS EventSystem)

if(MSVC)
    # Suppress warning C4251 (class needs to have dll-interface to be used by clients of class)
    # This is common when exporting classes with std members (like std::map, std::vector)
    # Since this is a FetchContent-oriented library, clients usually build from source, mitigating ABI risks.
    target_compile_options(EventSystem PRIVATE /wd4251)
endif()
