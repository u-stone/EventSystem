add_executable(main_app main.cpp)
target_link_libraries(main_app PRIVATE eventsystem::eventsystem)

# Static Linkage Test App
add_executable(main_static_app main.cpp)
target_link_libraries(main_static_app PRIVATE eventsystem::eventsystem_static)
