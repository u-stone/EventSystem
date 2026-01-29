# C++17 Asynchronous Event System

## 1. 项目简介
**EventSystem** 是一个基于 C++17 标准开发的高性能、线程安全、支持多范式的事件分发系统。它提供了两种核心模式，以满足不同场景下的解耦通信需求：

1.  **EventCenter (Type-based)**: 基于强类型的事件分发，利用 C++ 类型系统确保安全性和高性能。
2.  **MessageCenter (String-based)**: 基于字符串 Topic 的观察者模式，支持任意参数类型的回调 (Variadic Args)。

## 2. 核心特性

*   **双重模式**: 同时支持基于类型的事件 (`EventCenter`) 和基于字符串的消息 (`MessageCenter`)。
*   **鲁棒类型匹配**: `MessageCenter` 支持自动类型退化 (Decay) 和字符串字面量提升 (Promotion)，极大减少了类型不匹配导致的错误。
*   **异步安全**: 强制值拷贝语义，从根本上消除了异步分发中引用失效 (Dangling Reference) 导致的崩溃风险。
*   **动态切换**: 支持在运行时动态切换默认发布模式 (Async/Sync)。
*   **统一订阅**: 订阅者无需关心事件的发布方式，共享同一个注册表。
*   **DLL 友好**: 采用 PIMPL 模式隐藏实现细节，完美解决 Windows 上的 DLL 导出警告 (C4251)。

## 3. 文档与指南

*   📖 **[用户使用指南 (User Guide)](docs/User_Guide.md)**: 详细的 API 使用说明和示例代码。
*   ⚙️ **[MessageCenter 设计详解](docs/MessageCenter_Design.md)**: 关于变参模板、类型擦除、退化与提升的实现细节。
*   ⚙️ **[EventCenter 设计详解](docs/EventCenter_Design.md)**: 关于类型分发与注册表的实现细节。

## 4. 集成方式

### 4.1 通过 FetchContent 集成 (推荐)
```cmake
include(FetchContent)
FetchContent_Declare(
  EventSystem
  GIT_REPOSITORY https://github.com/YourUsername/EventSystem.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(EventSystem)
target_link_libraries(your_target PRIVATE eventsystem::eventsystem)
```

### 4.2 手动构建
```bash
mkdir build && cd build
cmake .. -DBUILD_SHARED_LIBS=ON
cmake --build . --config Release
ctest -C Release
```
