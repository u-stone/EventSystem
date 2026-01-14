#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifdef EventSystem_EXPORTS
        #define EVENTSYSTEM_API __declspec(dllexport)
    #else
        #define EVENTSYSTEM_API __declspec(dllimport)
    #endif
#else
    #define EVENTSYSTEM_API
#endif
