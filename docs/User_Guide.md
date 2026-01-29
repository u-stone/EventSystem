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
使用 `SubscribeEvent<T>` 订阅特定类型的事件。

```cpp
#include "EventSystem/EventCenter.h"

void DemoSubscribe() {
    using namespace eventsystem;

    // 订阅 LoginEvent
    auto handle = SubscribeEvent<LoginEvent>([](const LoginEvent& e) {
        std::cout << "User " << e.username << " logged in at " << e.timestamp << std::endl;
    });

    // 订阅 LogoutEvent
    SubscribeEvent<LogoutEvent>([](const LogoutEvent& e) {
        std::cout << "User " << e.username << " logged out." << std::endl;
    });
}
```

### 1.3 发布事件
支持同步和异步发布。

```cpp
void DemoPublish() {
    using namespace eventsystem;

    // 1. 默认发布 (取决于全局模式，默认为 Async):
    PublishEvent(LoginEvent{"Alice", 123456789});

    // 2. 显式异步发布: 不阻塞当前线程，放入后台队列执行
    PublishEventAsync(LoginEvent{"Alice", 123456789});

    // 3. 显式同步发布: 立即在当前线程调用所有处理函数
    PublishEventSync(LogoutEvent{"Alice"});

    // 4. 延时发布: 500ms 后执行
    PublishEventDelayed(LoginEvent{"Bob", 999}, std::chrono::milliseconds(500));
}
```

### 1.4 注销
使用 `handle` 注销订阅。

```cpp
UnsubscribeEvent(handle);
```

### 1.5 静态成员函数订阅
支持直接注册包含静态 `Handle` 方法的事件类型。

```cpp
struct MyEvent { 
    int id; 
    static void Handle(const MyEvent& e) {
        // 处理逻辑
    }
};

// 注册 (自动绑定 MyEvent::Handle)
auto handle = RegisterStaticEventHandler<MyEvent>();

// 注销单个订阅
UnregisterStaticEventHandler(handle);

// 注销该类型的所有订阅
UnregisterStaticEventHandler<MyEvent>();
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

// 自动推导为 std::function<void(std::string)>
auto token = eventsystem::SubscribeMessage("log", [](const std::string& msg) {
    std::cout << "Log: " << msg << std::endl;
});
```

#### 2.1.2 多参数与特定类型
使用模板参数明确指定回调签名。

```cpp
// 订阅两个整数参数
auto token2 = eventsystem::SubscribeMessage<int, int>("mouse_click", [](int x, int y) {
    std::cout << "Clicked at " << x << ", " << y << std::endl;
});

// 订阅无参数信号
auto token3 = eventsystem::SubscribeMessage<>("app_start", []() {
    std::cout << "Application Started!" << std::endl;
});
```

### 2.2 发布 (Publish)

发布时的参数类型必须与订阅者的签名匹配（系统会自动处理常见转换）。

```cpp
// 1. 默认发布 (取决于全局模式，默认为 Async)
eventsystem::PublishMessage("log", "System running"); 

// 2. 显式异步发布
eventsystem::PublishMessageAsync("mouse_click", 100, 200);

// 3. 显式同步发布
eventsystem::PublishMessageSync("log", "Immediate log");
```

---

## 3. 高级特性与规则

### 3.1 统一注册表
无论是 `EventCenter` 还是 `MessageCenter`，订阅者只需注册一次。同步和异步发布都会触发同一个订阅者。

### 3.2 线程安全
所有 API 均为线程安全。可以在任意线程进行订阅、发布和注销。

### 3.3 打印订阅信息 (Debugging)
用于调试时查看当前所有的订阅关系。
```cpp
EventCenter::PrintSubscriptions();
MessageCenter::Instance().PrintSubscriptions();
```

### 3.4 类型匹配与自动转换规则

`MessageCenter` 引入了多项机制来简化强类型匹配带来的负担，并增强了异步安全性。

#### 3.4.1 自动类型退化 (Type Decay)
为了确保异步安全性并解决 `std::any` 严格匹配问题，系统在订阅时会自动将参数类型进行 **Decay** 处理（去除 `&`、`const`、`volatile`）。
*   如果你写 `SubscribeMessage("topic", [](const std::string& s){...})`，系统内部会将其标准化为 `void(std::string)`。
*   **好处**：这保证了即使发布者提供的是值类型（如异步拷贝后的副本），也能成功匹配。

#### 3.4.2 字符串字面量自动提升 (String Promotion)
在发布消息时，系统会自动将 `const char*` 和 `char*` 类型提升为 `std::string`。
*   **示例**：`PublishMessage("topic", "hello")` 会被视为发布 `std::string` 类型。
*   这使得字面量发布能完美匹配期待 `std::string` 或 `const std::string&` 的订阅者。

#### 3.4.3 精确匹配与安全忽略
除了上述自动转换外，其他类型（如 `int` vs `float`）仍需精确匹配。如果类型不匹配，系统会**安全忽略**该次调用并在 stderr 打印警告，而不会崩溃。

---

## 4. 动态模式切换 (Dynamic Mode Switching)

系统支持在运行时动态切换默认发布模式（Async/Sync）。

### 4.1 模式控制
```cpp
// 方式 1: 使用类方法
MessageCenter::Instance().SetPublishMode(PublishMode::Sync);
EventRegistry::SetPublishMode(PublishMode::Sync);

// 方式 2: 使用全局 C-style API (推荐)
SetMessageCenterPublishMode(PublishMode::Async);
SetEventCenterPublishMode(PublishMode::Async);
```

**注意**：显式命名的 API (如 `PublishEventSync`, `PublishMessageAsync` 等) 不受此模式影响。
