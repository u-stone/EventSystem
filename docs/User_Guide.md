# EventSystem 用户指南 (User Guide)

本指南详细介绍了 EventSystem 库中两个核心组件的使用方法：**EventCenter** (基于类型) 
和 **MessageCenter** (基于字符串 Topic)。

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
支持同步和异步发布。默认发布模式可通过配置切换。

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

`MessageCenter` 基于字符串 Topic 进行分发。支持任意数量和类型的参数（Variadic Arguments）。

### 2.1 订阅 (Subscribe)

#### 2.1.1 自动推导用法
系统会自动从 Lambda 参数中推导类型，并进行自动退化处理（如 `const string&` 变为 `string`）。

```cpp
#include "EventSystem/MessageCenter.h"

// 自动推导为 std::function<void(std::string)>
auto token = eventsystem::SubscribeMessage("log", [](const std::string& msg) {
    std::cout << "Log: " << msg << std::endl;
});
```

#### 2.1.2 显式签名订阅
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

### 3.1 线程安全
所有 API 均为线程安全。可以在任意线程进行订阅、发布和注销。

### 3.2 调试与状态打印
用于调试时查看当前所有的订阅关系。
```cpp
PrintEventSubscriptions();   // EventCenter 订阅状态
PrintMessageSubscriptions(); // MessageCenter 订阅状态
```

### 3.3 类型匹配与自动转换规则

`MessageCenter` 引入了多项机制来简化强类型匹配负担，并增强异步安全性。

#### 3.3.1 自动类型退化 (Type Decay)
系统在订阅时会自动将参数类型进行 **Decay** 处理（去除 `&`、`const` 等）。
*   如果你写 `[](const std::string& s){...}`，内部会标准化为 `void(std::string)`。
*   **好处**：保证了即使发布者提供的是值类型（如异步拷贝后的副本），也能成功匹配。

#### 3.3.2 字符串字面量自动提升 (String Promotion)
发布时系统会自动将 `const char*` 提升为 `std::string`。
*   **示例**：`PublishMessage("topic", "hello")` 会完美匹配期待 `std::string` 的订阅者。

---

## 4. 动态模式切换 (Dynamic Mode Switching)

系统支持在运行时动态切换默认发布模式（Async/Sync）。

### 4.1 模式控制
```cpp
// 方式 1: 使用类方法
EventCenter::Instance().SetPublishMode(PublishMode::Sync);
MessageCenter::Instance().SetPublishMode(PublishMode::Sync);

// 方式 2: 使用辅助接口 (推荐)
SetEventCenterPublishMode(PublishMode::Async);
SetMessageCenterPublishMode(PublishMode::Async);
```

---

## 5. 生命周期管理 (Lifecycle Management)

在 Windows DLL 环境下，静态对象的自动析构可能引发死锁 (Loader Lock Deadlock)。
为了确保跨模块安全，建议在程序结束前进行显式清理。

### 5.1 手动销毁 (推荐)
建议在 `main` 函数退出前，显式调用 `Destroy` 接口。

```cpp
#include "EventSystem/MessageCenter.h"
#include "EventSystem/EventCenter.h"

int main() {
    // ... 业务逻辑 ...

    // 程序退出前清理 (停止线程，释放单例)
    eventsystem::DestroyMessageCenter();
    eventsystem::DestroyEventCenter();
    return 0;
}
```

---

## 6. 跨模块 (DLL) 开发最佳实践

### 6.1 链接方式 (Linking Strategy)
**强烈推荐**：将 `EventSystem` 编译为**动态链接库 (Shared Library)**。
*   这确保了整个进程中只有一个单例实例，从而实现 Host 和各插件间的事件通信。

### 6.2 避免悬垂回调 (Dangling Callbacks)
当插件被卸载时，必须注销其所有回调，否则触发事件将导致崩溃。

**解决方案：使用 `ScopedSubscription` (RAII)**
```cpp
class MyPlugin {
public:
    void Init() {
        auto token = eventsystem::SubscribeMessage("login", ...);
        m_sub = eventsystem::ScopedSubscription("login", token);
    }
private:
    eventsystem::ScopedSubscription m_sub; // 析构时自动注销
};
```
