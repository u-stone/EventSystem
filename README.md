# C++17 Asynchronous Event System (NotifyCenter)

## 1. 项目简介
**EventSystem** 是一个基于 C++17 标准开发的高性能、线程安全、支持多范式的异步事件分发系统。它旨在为 C++ 应用程序提供解耦的组件通信机制。

核心组件 `EventCenter` 采用单例模式，内部维护一个后台工作线程（Worker Thread），负责将发布的事件异步分发给所有注册的处理器。系统设计注重**内存安全**、**API 易用性**以及**高并发下的稳定性**。

## 2. 核心特性

*   **完全异步 (Fully Asynchronous)**: 事件发布是非阻塞的（Fire-and-forget），所有事件处理都在独立的后台线程中执行，不会阻塞发布者线程。
*   **多种注册范式 (Multi-Paradigm Registration)**:
    *   **强引用 (Strong)**: `EventCenter` 持有处理器所有权，适合“即发即忘”的生命周期管理。
    *   **弱引用 (Weak)**: `EventCenter` 仅持有弱引用，防止循环引用和内存泄漏，适合外部管理生命周期的对象。
    *   **回调函数 (Callback)**: 支持 Lambda 表达式和 `std::function`，适合轻量级逻辑。
    *   **静态处理器 (Static)**: 支持无状态事件的极简注册模式。
*   **定时与延时投递 (Scheduled Dispatch)**: 支持立即发布、延时发布 (`publish_event_delayed`) 以及指定时间点发布 (`publish_event_at`)。
*   **高性能与低延迟**:
    *   发布事件时在锁外构造对象，最小化临界区耗时。
    *   使用 `std::move` 语义减少不必要的拷贝。
*   **健壮性设计**:
    *   **异常隔离**: 单个处理器的异常崩溃不会影响其他处理器或导致系统崩溃。
    *   **死锁/超时检测**: 自动检测并警告执行时间超过 500ms 的慢速处理器。
    *   **稳定性测试**: 通过了高并发下的压力测试（Throughput）和混沌测试（Stability/Chaos）。
*   **控制能力**: 支持 `cancelAllEvents()` 紧急取消所有未决事件。

## 3. 快速上手

### 3.1 定义事件
事件可以是任意的结构体或类，无需继承特定基类。

```cpp
struct LoginEvent {
    std::string username;
    bool success;
};
```

### 3.2 注册处理器

**方式 A：使用 Lambda 表达式**
```cpp
auto handle = EventCenter::instance().registerHandler<LoginEvent>(
    [](const LoginEvent& event) {
        std::cout << "User " << event.username << " logged in: " << event.success << std::endl;
    }
);
// 注销
EventCenter::instance().unregisterHandler(handle);
```

**方式 B：使用类处理器 (强引用)**
```cpp
class AuthLogger : public IEventHandler {
    void handle(const std::any& eventData) override {
        if (auto* event = std::any_cast<LoginEvent>(&eventData)) {
            // 处理逻辑...
        }
    }
};

// 注册后，EventCenter 会保持该对象存活
EventCenter::instance().registerHandler<LoginEvent>(std::make_shared<AuthLogger>());
```

### 3.3 发布事件

```cpp
// 立即发布
publish_event(LoginEvent{"Alice", true});

// 延时 200ms 发布
publish_event_delayed(LoginEvent{"Bob", false}, std::chrono::milliseconds(200));
```

## 4. 构建与测试

项目包含完整的 CMake 构建脚本和 GoogleTest 单元测试。

**Windows (使用 build.bat):**
只需运行根目录下的 `build.bat` 脚本，它会自动完成以下步骤：
1. 检测 Visual Studio 版本。
2. 配置 CMake。
3. 编译主程序 (`main_app`) 和测试程序 (`test_event_system`)。
4. 运行所有单元测试 (GoogleTest)。
5. 运行演示主程序。

**手动构建:**
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Debug
ctest -C Debug --output-on-failure
```