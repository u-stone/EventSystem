add_library(EventSystem
    EventCenter.cpp
    MessageCenter.cpp
)

# Include directories
target_include_directories(EventSystem
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Handle Shared vs Static configurations
if(BUILD_SHARED_LIBS)
    # Shared Library: Define export macro internally
    target_compile_definitions(EventSystem PRIVATE EventSystem_EXPORTS)
    
    # Configure symbol visibility
    set_target_properties(EventSystem PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
else()
    # Static Library: Define macro publicly so consumers see it too
    target_compile_definitions(EventSystem PUBLIC EVENTSYSTEM_STATIC)
endif()

# Alias for easy usage
add_library(eventsystem::eventsystem ALIAS EventSystem)

# =========================================================
# Explicit Static Library Target (for testing/verification)
# =========================================================
add_library(EventSystemStatic STATIC
    EventCenter.cpp
    MessageCenter.cpp
)
target_compile_definitions(EventSystemStatic PUBLIC EVENTSYSTEM_STATIC)
target_include_directories(EventSystemStatic
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_library(eventsystem::static ALIAS EventSystemStatic)