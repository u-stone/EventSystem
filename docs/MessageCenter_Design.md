# MessageCenter Design: Variadic Templates & Type Erasure

本文档详细记录了 `MessageCenter` 如何利用 modern C++ (C++17) 特性实现对任意回调函数签名（0参数、1参数、多参数）的支持。

## 1. 核心目标
我们需要一个能够处理**基于字符串Topic**但**负载类型可变**的事件系统。
例如，同一个系统应该能支持：
- Topic "ping" -> `void()`
- Topic "log" -> `void(string)`
- Topic "mouse_event" -> `void(int x, int y)`

## 2. 关键技术实现

### 2.1 存储机制：Type Erasure (类型擦除)
由于 C++ 是强类型语言，`std::vector` 只能存储相同类型的对象。为了在一个容器中存储不同签名的回调函数（如 `std::function<void()>` 和 `std::function<void(int)>`），我们使用了 **`std::any`**。
此外，为了隐藏 `std::unordered_map` 等 STL 容器（解决 DLL 导出警告），我们采用了 **Pimpl (Pointer to Implementation)** 惯用语。

```cpp
// 位于 .cpp 内部的 Impl 结构
struct MessageCenter::Impl {
    struct SubscriberEntry {
        SubscriptionToken id;
        std::any callback; // 实际上存储的是 std::function<void(Args...)>
    };
    std::unordered_map<std::string, std::vector<SubscriberEntry>> m_subscriptions;
};
```
通过 `std::any`，我们将具体的函数类型信息擦除，统一存储为 `std::any` 对象。

### 2.2 订阅机制：Variadic Templates (变参模板)
订阅接口使用变参模板来捕获用户期望的回调签名。

```cpp
template <typename... Args>
SubscriptionToken Subscribe(const std::string& topic, std::function<void(Args...)> callback) {
    // ...
    // 将具体类型的 std::function 存入 std::any
    // PIMPL: SubscribeInternal(topic, std::any(callback))
}
```
**关键点：**
*   **显式模板参数**: 对于 Lambda 表达式，编译器无法直接推导 `std::function` 的模板参数。因此，用户通常需要显式指定类型：
    ```cpp
    // 正确：显式指定参数类型
    Subscribe<int, int>("coords", [](int x, int y){ ... });
    ```
    或者是构造好 `std::function` 后传入。

### 2.3 分发机制：类型恢复与安全调用
在 `Publish` 时，调用者提供了具体的参数。编译器推导出参数类型 `Args...`，我们利用这些类型尝试将 `std::any` 还原回原始的函数指针。

```cpp
template <typename... Args>
void Dispatch(const std::string& topic, Args... args) {
    // ... 获取 callbacks ...
    for (const auto& anyCb : callbacksToInvoke) {
        // 尝试转换回精确的函数签名
        using FunctionType = std::function<void(Args...)>;
        if (auto* func = std::any_cast<FunctionType>(&anyCb)) {
            (*func)(args...); // 类型匹配，执行调用
        }
        // 如果类型不匹配，any_cast 返回 nullptr，安全跳过
    }
}
```
**细节解释：**
*   **Exact Match**: `std::any_cast` 要求类型完全匹配。如果存储的是 `void(int)` 但发布的是 `void(float)`，转换会失败（返回 nullptr），从而实现了运行时的类型安全检查。这意味着 "Topic" 只是第一层过滤，"函数签名" 是第二层过滤。

### 2.4 异步处理：Universal References 与 Closure
对于异步发布，我们需要将参数打包并延迟执行。这里涉及到了参数生命周期的管理。

```cpp
template <typename... Args>
void PublishAsync(const std::string& topic, Args&&... args) {
    // 1. 使用 std::decay 去除引用和 const，确保按值捕获（Copy/Move）
    // 2. 使用 std::make_tuple 将所有参数打包
    auto task = [this, topic, args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        // 3. 在 Worker 线程中解包 (std::apply) 并调用 Dispatch
        std::apply([this, &topic](auto&&... unpackedArgs) {
            this->Dispatch<typename std::decay<Args>::type...>(
                topic, std::forward<decltype(unpackedArgs)>(unpackedArgs)...
            );
        }, std::move(args));
    };
    
    // 将闭包任务推入队列
    EnqueueTask(std::move(task));
}
```
**关键点：**
*   **`Args&&` (万能引用)**: 允许接收左值或右值。
*   **`std::forward`**: 完美转发，保留参数的左/右值属性。
*   **`std::decay`**: 在异步场景下非常重要。如果传入的是 `const char*`（字符串字面量），必须 decay 成 `std::string`（如果用户传递了 string）或者保持指针？注意：`std::decay` 对于数组会退化为指针。
    *   *特别注意*: 这里的实现假设用户传递的参数是能够安全拷贝/移动的。如果传递的是裸指针，用户需确保指针在异步执行时依然有效。通常建议传递值类型（如 `std::string`, `int`, `struct`）。
*   **Closure (闭包)**: 我们构建了一个无参数的 lambda `std::function<void()>` 存入队列。这使得 Worker 线程不需要知道具体的参数类型，只需执行 `task()` 即可。

## 3. Lambda 推导问题与解决方案
C++17 中，无法直接从 Lambda 表达式推导 `std::function<void(Args...)>` 中的 `Args...`。
因此，我们引入了 `SubscribeMessage` 辅助函数和 `function_traits`：

1.  **Helper Template**:
    ```cpp
    template <typename... Args, typename Callback>
    SubscriptionToken SubscribeMessage(const std::string& topic, Callback&& callback) {
        // 如果 Args... 为空，则利用 Traits 推导 Lambda 参数类型
        // 如果 Args... 不为空，则强制构造 std::function<void(Args...)>
        // 最终调用 Instance().Subscribe<Args...>(...)
    }
    ```
    使用示例：`SubscribeMessage("topic", [](int i){})` (自动推导为 `<int>`)。

## 4. 动态发布模式 (Dynamic Publish Mode)
系统引入了 `SetPublishMode(PublishMode::Sync/Async)`。
`PublishMessage` (无后缀) 会检查该模式：
- `Sync`: 直接调用 `Dispatch`，在当前线程执行。
- `Async`: 调用 `PublishAsync`，入队到工作线程。

这为调试和灵活控制提供了极大的便利。

## 5. 总结
该设计在保持接口简洁的同时，极大地扩展了灵活性：
1.  **灵活性**: 支持任意数量和类型的参数。
2.  **安全性**: 编译期（模板实例化）和运行期（any_cast）双重保证类型安全。
3.  **解耦**: 异步队列仅存储无类型闭包 (`std::function<void()>`)，将复杂的类型处理封装在发布端。
