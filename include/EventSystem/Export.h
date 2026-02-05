#pragma once

#if defined(EVENTSYSTEM_STATIC)
    #define EVENTSYSTEM_API
#elif defined(_WIN32) || defined(_WIN64)
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

namespace eventsystem {

/**
 * @brief Common publishing modes for both EventCenter and MessageCenter.
 */
enum class PublishMode {
    Async,      // Background thread
    Sync,       // Immediate: Current thread
    MainThread  // Queued: Processed in Update() on the main thread (Default for Game Engines)
};

} // namespace eventsystem
