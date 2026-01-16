# EventSystem 用户指南 (User Guide)

本指南详细介绍了 EventSystem 库中两个核心组件的使用方法：**EventCenter** (基于类型) 和 **MessageCenter** (基于字符串 Topic)。

## 1. EventCenter (Type-based)

`EventCenter` 利用 C++ 类型系统作为事件标识。适用于强类型、高性能的内部组件解耦。

### 1.1 定义事件
事件只是普通的 C++ 结构体或类。

```cpp
struct LoginEvent {
    std::string username;
    int64_t timestamp;
};

struct LogoutEvent {
    std::string username;
};
```

### 1.2 订阅事件
使用 `subscribe_event<T>` 订阅特定类型的事件。

```cpp
#include "EventSystem/EventCenter.h"

void demo_subscribe() {
    using namespace eventsystem;

    // 订阅 LoginEvent
    auto handle = subscribe_event<LoginEvent>([](const LoginEvent& e) {
        std::cout << "User " << e.username << " logged in at " << e.timestamp << std::endl;
    });

    // 订阅 LogoutEvent
    subscribe_event<LogoutEvent>([](const LogoutEvent& e) {
        std::cout << "User " << e.username << " logged out." << std::endl;
    });
}
```

### 1.3 发布事件
支持同步和异步发布。

```cpp
void demo_publish() {
    using namespace eventsystem;

    // 1. 异步发布 (推荐): 不阻塞当前线程，放入后台队列执行
    publish_event(LoginEvent{"Alice", 123456789});

    // 2. 同步发布: 立即在当前线程调用所有处理函数
    publish_event_sync(LogoutEvent{"Alice"});

    // 3. 延时发布: 500ms 后执行
    publish_event_delayed(LoginEvent{"Bob", 999}, std::chrono::milliseconds(500));
}
```

### 1.4 注销
使用 `handle` 注销订阅。

```cpp
unsubscribe_event(handle);
```

---

## 2. MessageCenter (String-based)

`MessageCenter` 基于字符串 Topic 进行分发，类似于 MQTT 或常见的观察者模式。
**新特性**：支持任意数量和类型的参数（Variadic Arguments）。

### 2.1 订阅 (Subscribe)

#### 2.1.1 基础用法 (Legacy String)
如果只传递 `std::string`，可以像以前一样简单使用。

```cpp
#include "EventSystem/MessageCenter.h"

// 自动推导为 std::function<void(const std::string&)>
auto token = eventsystem::subscribe_message("log", [](const std::string& msg) {
    std::cout << "Log: " << msg << std::endl;
});
```

#### 2.1.2 多参数与特定类型
使用模板参数明确指定回调签名。

```cpp
// 订阅两个整数参数
// 注意：必须显式指定模板参数 <int, int>
auto token2 = eventsystem::subscribe_message<int, int>("mouse_click", [](int x, int y) {
    std::cout << "Clicked at " << x << ", " << y << std::endl;
});

// 订阅无参数信号
auto token3 = eventsystem::subscribe_message<>("app_start", []() {
    std::cout << "Application Started!" << std::endl;
});
```

### 2.2 发布 (Publish)

发布时的参数类型必须与订阅者的签名**精确匹配**（或能通过 `std::decay` 匹配）。

```cpp
// 1. 异步发布 (默认)
eventsystem::publish_message("log", "System running"); // 自动转换为 std::string
eventsystem::publish_message("mouse_click", 100, 200); // 匹配 <int, int>
eventsystem::publish_message("app_start");             // 匹配 <>

// 2. 同步发布
eventsystem::publish_message_sync("log", "Immediate log");
```

### 2.3 安全性说明
如果发布的参数类型与订阅者不匹配，该订阅者会被**安全忽略**，不会导致崩溃（除了极其特殊的情况），但会打印错误日志。

```cpp
// 订阅者期待 int
subscribe_message<int>("topic", [](int i){});

// 发布者发送 float -> 订阅者不会收到通知
publish_message("topic", 3.14f); 
```

### 2.4 注销
```cpp
// 注销单个订阅
eventsystem::unsubscribe_message("topic", token);

// 注销该 Topic 下的所有订阅者
eventsystem::unsubscribe_message("topic");
```

---

## 3. 高级特性

### 3.1 共享注册表 (Unified Registry)
无论是 `EventCenter` 还是 `MessageCenter`，订阅者只需注册一次。
- `publish_event` (异步) 和 `publish_event_sync` (同步) 都会触发同一个订阅者。
- 无需为同步和异步分别订阅。

### 3.2 线程安全
所有 API 均为线程安全。可以在任意线程进行订阅、发布和注销。
*注意：在回调函数中操作共享数据时，仍需用户自己处理锁。*
