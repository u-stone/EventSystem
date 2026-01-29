# C++17 Asynchronous Event System

## 1. 项目简介
**EventSystem** 是一个基于 C++17 标准开发的高性能、线程安全、支持多范式的事件分发系统。它提供了两种核心模式，以满足不同场景下的解耦通信需求：

1.  **EventCenter (Type-based)**: 基于强类型的事件分发，利用 C++ 类型系统确保安全性和高性能。
2.  **MessageCenter (String-based)**: 基于字符串 Topic 的观察者模式，支持任意参数类型的回调 (Variadic Args)。

系统支持**同步**与**完全异步**两种分发模式，内置后台工作线程处理异步任务，并支持在运行时动态切换默认分发模式。

## 2. 核心特性

*   **双重模式**: 同时支持基于类型的事件 (`EventCenter`) 和基于字符串的消息 (`MessageCenter`)。
*   **灵活参数**: `MessageCenter` 支持 0 到 N 个任意类型的参数传递。
*   **统一订阅**: 订阅者无需关心事件的发布方式（同步或异步），共享同一个注册表。
*   **完全异步**: 提供内置工作线程，支持非阻塞的“即发即忘”模式及延时投递。
*   **动态切换**: 支持在运行时动态切换默认的发布模式（Async/Sync），方便调试与控制。
*   **DLL 友好**: 妥善处理了 Windows 上的 DLL 导出与单例一致性问题 (PIMPL)。

## 3. 文档与指南

*   📖 **[用户使用指南 (User Guide)](docs/User_Guide.md)**: 详细的 API 使用说明和示例代码。
*   ⚙️ **[MessageCenter 设计详解](docs/MessageCenter_Design.md)**: 关于变参模板与类型擦除的实现细节。
*   ⚙️ **[EventCenter 设计详解](docs/EventCenter_Design.md)**: 关于类型分发与注册表的实现细节。

## 4. 集成方式

### 4.1 通过 FetchContent 集成 (推荐)
在您的 `CMakeLists.txt` 中添加以下代码：

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

## 5. 目录结构
*   `include/EventSystem/`: 公共头文件。
*   `src/`: 库实现文件。
*   `examples/`: 示例程序 (`main.cpp`)。
*   `tests/`: 单元测试。
*   `docs/`: 详细文档。