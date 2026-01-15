add_executable(test_event_center test_event_center.cpp)
target_link_libraries(test_event_center PRIVATE eventsystem::eventsystem GTest::gtest_main)

add_executable(test_message_center test_message_center.cpp)
target_link_libraries(test_message_center PRIVATE eventsystem::eventsystem GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_event_center)
gtest_discover_tests(test_message_center)
