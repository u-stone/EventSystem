#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifdef EventSystem_EXPORTS
        #define EVENTSYSTEM_API __declspec(dllexport)
    #else
        #define EVENTSYSTEM_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #ifdef EventSystem_EXPORTS
        #define EVENTSYSTEM_API __attribute__((visibility("default")))
    #else
        #define EVENTSYSTEM_API
    #endif
#else
    #define EVENTSYSTEM_API
#endif