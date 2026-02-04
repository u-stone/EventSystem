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

---

## 5. 生命周期管理 (Lifecycle Management)

在 Windows DLL 环境下，静态对象的自动析构可能引发死锁 (Loader Lock Deadlock)，尤其是当对象内部包含工作线程时。
为了确保跨模块安全，本系统采用了 **显式生命周期管理** 策略。

### 5.1 手动销毁 (推荐)
建议在 `main` 函数退出前，显式调用 `Destroy()` 接口。

```cpp
#include "EventSystem/MessageCenter.h"
#include "EventSystem/EventCenter.h"

int main() {
    // ... 业务逻辑 ...

    // 程序退出前清理
    eventsystem::MessageCenter::Destroy();
    eventsystem::AsyncEventCenter::Destroy(); // 如果使用了 AsyncEventCenter
    eventsystem::SyncEventCenter::Destroy();  // 如果使用了 SyncEventCenter
    return 0;
}
```

### 5.2 Leaky Singleton (默认)
如果您忘记调用 `Destroy()`，系统默认采用 Leaky Singleton 模式（即不释放单例内存）。
*   **优点**：完全避免了 DLL 卸载时的死锁崩溃风险。
*   **缺点**：会有少量内存泄漏（通常操作系统会在进程结束时回收，影响不大）。
*   **结论**：如果您不确定，**什么都不做** 比 **在静态析构函数中调用** 更安全。

---

## 6. 跨模块 (DLL) 开发最佳实践

在涉及多个 DLL（如 Host 程序 + 多个 Plugin DLL）的大型项目中，正确管理 EventSystem 的链接方式和订阅生命周期至关重要。

### 6.1 链接方式 (Linking Strategy)

**强烈推荐**：将 `EventSystem` 编译为**动态链接库 (Shared Library / DLL)**，并让所有模块（Host 和 Plugins）都动态链接到它。

*   **原因**：这确保了整个进程中只有一个 `EventSystem` 单例实例，从而实现跨模块的事件通信。
*   **错误做法**：如果在每个 DLL 中静态链接 (`STATIC`) EventSystem 源码，每个模块将拥有自己独立的单例副本，导致事件无法跨模块互通，且可能引发 CRT 堆隔离问题。

### 6.2 单例销毁责任 (Responsibility)

*   **Host (宿主程序)**：负责在程序退出时调用 `Destroy()`。
*   **Plugin (插件模块)**：**严禁**调用 `Destroy()`。插件只是使用者，不拥有单例的所有权。

### 6.3 避免悬垂回调 (Dangling Callbacks)

当一个 Plugin 被动态卸载 (Unload) 时，如果它注册的回调函数没有被注销，EventSystem 中将残留指向无效内存的指针。一旦触发该事件，程序将立即崩溃。

**解决方案：使用 `ScopedSubscription` (RAII)**

我们提供了 `ScopedSubscription` 辅助类，利用 RAII 机制自动管理订阅生命周期。

```cpp
class MyPluginModule {
public:
    void Init() {
        // 订阅并托管 Token
        auto token = eventsystem::SubscribeMessage("login", ...);
        m_sub = eventsystem::ScopedSubscription("login", token);
    }
    
    // 析构函数自动调用 Unsubscribe，无需手动管理
    ~MyPluginModule() = default;

private:
    eventsystem::ScopedSubscription m_sub;
};
```
**规则**：所有动态加载模块**必须**使用 `ScopedSubscription` 管理其注册的事件，或者在卸载前严格保证手动注销所有事件。