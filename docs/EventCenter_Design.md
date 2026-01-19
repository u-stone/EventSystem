# EventCenter Design: Type-Based Dispatch & Unified Registry

本文档深入解析 `EventCenter` 的内部设计原理。与 `MessageCenter` 基于字符串 Topic 不同，`EventCenter` 利用 C++ 的类型系统作为事件分发的路由依据，提供了更高的类型安全性和性能。

## 1. 核心设计理念

### 1.1 Type as Topic (类型即主题)
在 `EventCenter` 中，我们不使用字符串来区分事件，而是使用**事件本身的类型**。
- `std::type_index` (包装了 `std::type_info`) 被用作 `std::map` 的键 (Key)。
- 这种方式利用了 RTTI (Run-Time Type Information)，确保了发布者和订阅者在编译期就达成了“契约”。

### 1.2 Unified Registry (统一注册表)
为了支持“订阅一次，随处接收”的特性，我们将订阅关系的管理（Registry）与分发策略（Sync/Async Dispatcher）分离。

*   **EventRegistry (Static State)**: 这是一个静态的、全局共享的存储区。无论用户是通过 `SyncEventCenter` 还是 `AsyncEventCenter`（默认）进行订阅，Handler 都会被存储在这里。
*   **Dispatchers (Singleton)**: `SyncEventCenter` 和 `AsyncEventCenter` 仅仅是**策略类**。它们负责“怎么发”（立即调用还是放入队列），但它们查找订阅者的位置是同一个 `EventRegistry`。

## 2. 存储与类型擦除

虽然 `EventCenter` 是强类型的，但为了统一存储所有类型的 Handler，我们在底层仍然需要类型擦除。

### 2.1 IEventHandler 接口
所有 Handler 最终都被封装为 `IEventHandler` 接口的实现：

```cpp
class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    // 参数是 std::any，包含了具体的 Event 对象
    virtual void Handle(const std::any& event) = 0;
};
```

### 2.2 存储结构 (Internal Storage)
为了确保二进制兼容性 (ABI Stability) 并消除 Windows DLL 导出警告 (C4251)，所有的存储容器均已移至 `EventCenter.cpp` 内部（Internal Linkage）。
头文件中不再暴露 `std::map` 或 `std::vector` 成员。

```cpp
// 伪代码 (位于 .cpp 内部)
namespace {
    struct RegistryStorage {
        std::vector<std::shared_ptr<IEventHandler>> strongRefs;
        std::vector<std::weak_ptr<IEventHandler>> weakRefs;
    };
    // 全局静态存储，对外不可见
    std::map<std::type_index, RegistryStorage> g_interfaceHandlers;
}
```

### 2.3 泛型适配器 (Handler Wrappers)
当用户订阅 `SubscribeEvent<T>(lambda)` 时，我们会在内部创建一个泛型适配器，将 `std::any` 转换回 `T`：

```cpp
// 伪代码
class LambdaEventHandler : public IEventHandler {
    Func m_func;
public:
    void Handle(const std::any& anyEvent) override {
        // 安全转换：因为我们在 map 中是按 type_index 存储的，
        // 所以这里取出来的一定是 T 类型。
        const T& event = std::any_cast<const T&>(anyEvent);
        m_func(event);
    }
};
```

## 3. 生命周期管理

`EventCenter` 提供了极其灵活的生命周期管理策略，这是其设计的另一大亮点。

### 3.1 Strong Reference (强引用)
默认情况下（`SubscribeEvent` 或 `RegisterHandler`），`EventCenter` 会持有 Handler 的 `std::shared_ptr`。
- **效果**: 只要 `EventCenter` 存在（或者直到显式注销），Handler 就一直存活。
- **场景**: 简单的 Lambda 回调，或者“即发即忘”的一次性任务。

### 3.2 Weak Reference (弱引用)
通过 `RegisterWeakHandler`，`EventCenter` 仅持有 `std::weak_ptr`。
- **效果**: `EventCenter` **不** 拥有 Handler 的生命周期。如果外部对象（如某个 UI 窗口）被销毁了，`weak_ptr` 会自动失效。
- **分发时检查**: 在分发事件时，系统会尝试 `lock()`。如果失效，则自动跳过（甚至可以清理）。
- **场景**: 避免“悬垂指针”和“忘记注销”导致的崩溃。特别适合组件生命周期短于 App 生命周期的场景。

### 3.3 Static Handlers (静态函数)
对于无状态的逻辑，支持直接注册类的静态成员函数。
- **优化**: 内部不需要分配复杂的对象，直接存储函数指针。
- **注销**: 支持 `UnregisterStaticEventHandler(handle)` 或 `UnregisterStaticEventHandler<TEvent>()`。

## 4. 异步分发模型

`AsyncEventCenter` 维护了一个后台工作线程。

1.  **Publish**: 用户调用 `PublishEvent(E)`.
2.  **Enqueue**: 事件 `E` 被移动/拷贝到 `std::any`，并封装为一个 `ScheduledEvent` 任务，推入优先级队列 (`priority_queue`)。
    - *优先级队列* 用于支持延时事件 (`PublishEventDelayed`)。
3.  **Worker Thread**:
    - 等待条件变量。
    - 取出队首事件。
    - 检查是否到期 (对于延时事件)。
    - 调用 `EventRegistry::DispatchEvent(type_index, event_data)`。

## 5. 总结

`EventCenter` 的设计权衡了 **类型安全** (Type Safety) 与 **通用性** (Genericity)。
- 利用 `std::type_index` 做路由，保证了 C++ 原生的类型匹配。
- 利用 `std::any` 和 `IEventHandler` 做存储，实现了异构容器。
- 利用 `weak_ptr` 解决了观察者模式中经典的生命周期管理难题。

## 6. 二进制兼容性 (ABI Stability)

为了确保跨 DLL 边界的安全性并消除 C4251 警告，本项目采用了 **PIMPL (Pointer to Implementation)** 惯用语。
- 所有私有成员（特别是 STL 容器如 `std::map`, `std::vector`, `std::mutex`）都被移动到了 `.cpp` 文件中的隐藏结构体或静态变量中。
- 头文件仅暴露纯 C++ 接口和前向声明，极大减少了编译依赖，并保证了 ABI 的稳定性。
