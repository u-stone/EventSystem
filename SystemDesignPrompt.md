# Role
你是一位世界级的 C++ 系统架构师，精通 C++17 标准、多线程编程（std::thread, std::mutex, std::condition_variable）以及设计模式。

# Goal
请设计并实现一个名为 `EventSystem` 的高性能、线程安全、支持多种编程范式的异步事件分发系统。

# Core Requirements (核心需求)
1.  **单例模式**: `EventCenter` 必须是线程安全的单例。
2.  **异步工作线程**: 内部维护一个 Worker Thread 处理事件队列。支持“懒加载”（Lazy Loading），即第一次发布事件时才启动线程。
3.  **优雅退出**: 析构时必须处理完剩余事件或安全停止线程，杜绝崩溃。
4.  **构建系统**: 提供 `CMakeLists.txt` (集成 GoogleTest) 和 Windows 下的 `build.bat` 一键构建脚本。

# API Requirements (API 设计)
1.  **事件发布**:
    *   `publish_event<T>(event)`: 立即异步发布。
    *   `publish_event_delayed<T>(event, delay)`: 延时发布。
    *   `publish_event_at<T>(event, time_point)`: 指定时间发布。
    *   **优化要求**: 在锁外构造事件对象（`std::any`），减少锁持有时间，防止发布者阻塞。

2.  **处理器注册 (需支持 4 种模式)**:
    *   **Strong (强引用)**: `registerHandler(shared_ptr<IEventHandler>)`，系统持有所有权。
    *   **Weak (弱引用)**: `registerWeakHandler(shared_ptr<IEventHandler>)`，系统不持有所有权，防止循环引用。
    *   **Callback (回调)**: `registerHandler(lambda)`，返回句柄用于注销。
    *   **Static (静态)**: `registerStaticEventHandler<T>()`，自动注册事件结构体内的 `static void handle()` 方法。

3.  **管理与控制**:
    *   `unregisterHandler(...)`: 支持按指针或句柄注销。
    *   `unregisterAllHandlers<T>()`: 注销某事件的所有处理器。
    *   `cancelAllEvents()`: **(新功能)** 立即清空所有待处理（Pending）和已调度（Scheduled）的事件。

# Robustness & Safety (健壮性要求)
1.  **异常隔离**: 在 `dispatchEvent` 中，必须用 `try-catch` 包裹每个处理器的执行。如果一个处理器抛出异常，不能影响其他处理器，且需打印错误日志。
2.  **性能监控**: 如果某个处理器的执行时间超过 **500ms**，应在控制台输出警告，帮助定位死锁或性能瓶颈。
3.  **线程安全**: 必须保证在事件分发过程中（Dispatching），其他线程可以安全地进行注册（Register）或注销（Unregister）操作，不会导致迭代器失效或死锁。

# Deliverables (交付物)
1.  **`EventSystem.h`**: 完整的头文件实现。
2.  **`EventSystem_test.cpp`**: 使用 GoogleTest 编写的单元测试，需覆盖：
    *   单例唯一性。
    *   各类注册/注销逻辑。
    *   弱引用生命周期（对象销毁后不应收到事件）。
    *   延时/定时事件的准确性。
    *   `cancelAllEvents` 的有效性。
    *   异常隔离测试（一个 Handler 崩溃不影响另一个）。
3.  **`main.cpp`**: 一个综合演示程序，必须包含 **8个步骤**：
    *   Step 1-6: 演示上述 API 的基本用法。
    *   Step 7: **Throughput Stress Test** (多线程高并发发布事件，验证吞吐量)。
    *   Step 8: **Stability/Chaos Test** (在发布事件的同时，由其他线程疯狂注册/注销 Handler，验证系统在极端竞争条件下的稳定性)。