# C++17 Asynchronous Event System (NotifyCenter)

## 1. 项目简介
**EventSystem** 是一个基于 C++17 标准开发的高性能、线程安全、支持多范式的事件分发系统。它提供了两种核心模式，以满足不同场景下的解耦通信需求：

1.  **EventCenter (Type-based)**: 基于强类型的事件分发，利用 C++ 类型系统确保安全性和高性能。
2.  **MessageCenter (String-based)**: 基于字符串 Topic 的观察者模式，适用于轻量级、高度松耦合的通知场景。

系统支持**同步**与**完全异步**两种分发模式，内置后台工作线程处理异步任务，并具备内存安全管理、异常隔离及性能监控等企业级特性。本库支持作为 Windows DLL 导出，并可通过 CMake `FetchContent` 轻松集成。

## 2. 核心特性

*   **双重模式**: 同时支持基于类型的事件 (`EventCenter`) 和基于字符串的消息 (`MessageCenter`)。
*   **完全异步**: 事件发布是非阻塞的，支持延时投递。
*   **DLL 友好**: 妥善处理了 Windows 上的 DLL 导出与单例一致性问题。
*   **命名空间**: 所有核心类及 Helper 工具函数均位于 `eventsystem` 命名空间下。

## 3. 集成方式

### 3.1 通过 FetchContent 集成 (推荐)
在您的 `CMakeLists.txt` 中添加以下代码：

```cmake
include(FetchContent)

FetchContent_Declare(
  EventSystem
  GIT_REPOSITORY https://github.com/YourUsername/EventSystem.git
  GIT_TAG        main # 或者指定的 commit hash / tag
)

FetchContent_MakeAvailable(EventSystem)

# 链接到您的目标
target_link_libraries(your_target PRIVATE eventsystem::eventsystem)
```

### 3.2 手动构建与安装
```bash
mkdir build && cd build
cmake .. -DBUILD_SHARED_LIBS=ON
cmake --build . --config Release
ctest -C Release
```

## 4. 快速上手

### 4.1 EventCenter 用法 (基于类型)
推荐在源文件中使用 `using namespace eventsystem;` 以简化 Helper 函数调用。

```cpp
#include "EventSystem/EventCenter.h"

struct LoginEvent { std::string user; };

void demo() {
    using namespace eventsystem;

    // 订阅 (Helper 函数)
    auto handle = subscribe_event<LoginEvent>([](const LoginEvent& e) {
        std::cout << "User " << e.user << " logged in" << std::endl;
    });

    // 发布 (Helper 函数)
    publish_event(LoginEvent{"Alice"});

    // 延时发布
    publish_event_delayed(LoginEvent{"Bob"}, std::chrono::milliseconds(500));
}
```

### 4.2 MessageCenter 用法 (基于字符串)
```cpp
#include "EventSystem/MessageCenter.h"

void demo_message() {
    // 也可以直接通过命名空间调用
    auto token = eventsystem::subscribe_message("system_status", [](const std::string& msg) {
        std::cout << "Status: " << msg << std::endl;
    });

    eventsystem::publish_message("system_status", "Running");
}
```

## 5. 目录结构
*   `include/EventSystem/`: 公共头文件。
*   `src/`: 库实现文件。
*   `examples/`: 示例程序。
*   `tests/`: 单元测试。