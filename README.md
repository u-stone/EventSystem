# C++17 Asynchronous Event System (NotifyCenter)

## 1. 项目简介
**EventSystem** 是一个基于 C++17 标准开发的高性能、线程安全、支持多范式的事件分发系统。它提供了两种核心模式，以满足不同场景下的解耦通信需求：

1.  **EventCenter (Type-based)**: 基于强类型的事件分发，利用 C++ 类型系统确保安全性和高性能。
2.  **MessageCenter (String-based)**: 基于字符串 Topic 的观察者模式，适用于轻量级、高度松耦合的通知场景。

系统支持**同步**与**完全异步**两种分发模式，内置后台工作线程处理异步任务，并具备内存安全管理、异常隔离及性能监控等企业级特性。

## 2. 核心特性

### 通用特性
*   **双重模式**: 同时支持基于类型的事件 (`EventCenter`) 和基于字符串的消息 (`MessageCenter`)。
*   **同步/异步可选**: 每个组件均提供同步 (`Sync`) 和异步 (`Async`) 实例，满足不同实时性要求。
*   **健壮性设计**:
    *   **异常隔离**: 单个处理器的异常不会影响系统整体或其他订阅者。
    *   **性能监控**: 自动检测并警告执行时间超过 500ms 的慢速处理器。
*   **线程安全**: 所有注册和发布操作均是线程安全的。

### EventCenter (Type-based)
*   **多种注册范式**: 
    *   **强引用 (Strong)**: 自动管理处理器生命周期。
    *   **弱引用 (Weak)**: 防止循环引用，适合外部管理生命周期的对象。
    *   **回调 (Callback)**: 支持 Lambda 和 `std::function`。
*   **定时投递**: 支持立即发布、延时发布 (`publish_event_delayed`) 以及指定时间点发布 (`publish_event_at`)。

### MessageCenter (String-based)
*   **极简解耦**: 无需定义事件类，仅凭字符串 Topic 即可订阅和发布。
*   **订阅令牌**: 订阅返回 `SubscriptionToken`，用于精确注销。

## 3. 快速上手

### 3.1 EventCenter 用法 (基于类型)
适用于复杂逻辑、强类型约束的场景。

#### 定义与注册
```cpp
struct LoginEvent { std::string user; };

// 注册异步回调 (默认)
auto handle = EventCenter::instance().registerHandler<LoginEvent>([](const LoginEvent& e) {
    std::cout << "User " << e.user << " logged in (Async)" << std::endl;
});

// 便捷注册接口 (等效于上述代码)
auto handle2 = subscribe_event<LoginEvent>([](const LoginEvent& e) {
    std::cout << "User " << e.user << " logged in (Async via Tool)" << std::endl;
});

// 注册同步回调
SyncEventCenter::instance().registerHandler<LoginEvent>([](const LoginEvent& e) {
    std::cout << "Immediate processing for " << e.user << std::endl;
});

// 便捷同步注册
auto sync_handle = subscribe_event_sync<LoginEvent>([](const LoginEvent& e) {
    std::cout << "Immediate processing (Sync via Tool)" << std::endl;
});
```

#### 发布事件
```cpp
// 异步发布 (不阻塞)
publish_event(LoginEvent{"Alice"});

// 延时 500ms 异步发布
publish_event_delayed(LoginEvent{"Bob"}, std::chrono::milliseconds(500));

// 同步发布 (在当前线程立即执行)
publish_event_sync(LoginEvent{"Charlie"});

// 注销订阅
unsubscribe_event(handle2);
unsubscribe_event_sync(sync_handle);
```

### 3.2 MessageCenter 用法 (基于字符串)
适用于简单的通知、全局状态更新等场景。

#### 订阅消息
```cpp
// 订阅异步消息 (默认)
auto token = subscribe_message("system_ready", [](const std::string& msg) {
    std::cout << "Message received: " << msg << std::endl;
});

// 订阅同步消息
auto sync_token = subscribe_message_sync("config_update", [](const std::string& msg) {
    // 立即处理配置变更
});
```

#### 发布消息
```cpp
// 异步发布
publish_message("system_ready", "All modules loaded");

// 同步发布
publish_message_sync("config_update", "new_config_path.json");

// 注销
unsubscribe_message("system_ready", token);
```

## 4. 构建与测试

项目包含完整的 CMake 构建脚本和 GoogleTest 单元测试。

**Windows (使用 build.bat):**
直接运行 `build.bat`。该脚本会自动配置 CMake、编译 `main_app` 及测试程序，并执行所有单元测试。

**手动构建:**
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
ctest -C Release --output-on-failure
```

## 5. 目录结构
*   `include/`: 头文件 (`EventSystem.h`, `MessageCenter.h`)。
*   `app/`: 演示程序入口。
*   `tests/`: 单元测试代码。
*   `external/`: 外部依赖 (GoogleTest)。
