add_library(EventSystem
    EventCenter.cpp
    MessageCenter.cpp
)

# Define the export macro when building as a shared library
target_compile_definitions(EventSystem PRIVATE EventSystem_EXPORTS)

# Configure symbol visibility (Best practice for cross-platform)
set_target_properties(EventSystem PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    WINDOWS_EXPORT_ALL_SYMBOLS OFF # Enforce explicit export on Windows too
)

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