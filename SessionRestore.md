# EventSystem Project Restoration Context

**Project Status**: Refactored as a professional tool library.
- **Structure**: Standard layout (`include/EventSystem`, `src`, `examples`, `tests`).
- **Windows Support**: Added `Export.h` and DLL export logic.
- **Namespace**: Lowercase `eventsystem`.
- **Integration**: Supports `FetchContent` and modern CMake targets (`eventsystem::eventsystem`).
- **Refactoring**: Renamed `EventSystem.h` to `EventCenter.h` for clarity.

**Instructions for Gemini**: Parse the latest files in `include/EventSystem` and `src` to resume.

## Target Name
- Library: `eventsystem::eventsystem` (Alias for `EventSystem`)

## Namespace
- `eventsystem`

## Headers
- `EventSystem/EventCenter.h`
- `EventSystem/MessageCenter.h`
- `EventSystem/Export.h`

## Source
- `src/EventCenter.cpp`
- `src/MessageCenter.cpp`
